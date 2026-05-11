/**
 * @file  msg_hub.c
 * @brief 发布/订阅 Hub 实现
 *
 * 核心结构：
 *
 *   hub_subscriber_t（对外不透明）
 *     ├── id            : 内部唯一 ID，仅用于 ACK 路由
 *     ├── msg_ids[]     : 订阅的 msg_id 列表（创建时一次分配，不再变更）
 *     ├── is_wildcard   : 是否订阅全部消息
 *     ├── active        : 0 = tombstone
 *     ├── callback/user_data
 *     └── hub           : 反向指针，供 hub_subscriber_destroy 使用
 *
 *   hub_t
 *     ├── mq            : 优先级消息队列
 *     ├── subs[]        : hub_subscriber_t* 指针数组（NULL = 空槽）
 *     └── sub_lock      : 保护 subs[]
 *
 * dispatch 流程：
 *   mq_pop → msg_id == ACK_ROUTE? → dispatch_ack (按 id 精确投递 SUB_ACK)
 *                                 → dispatch_msg (按 msg_id 广播)
 *
 * 快照机制（防死锁）：
 *   持锁期间将匹配的 (callback, user_data) 复制到栈上数组，
 *   释放锁后执行回调，回调内可安全调用任何 hub API。
 *
 * hub_subscriber_destroy 安全性：
 *   将 active 置 0（持锁），之后 dispatch 不再引用该订阅者，
 *   快照已持有的是值拷贝，释放 sub 结构完全安全。
 */

#define _POSIX_C_SOURCE 200809L

#include "msg_hub.h"
#include "msg_queue.h"

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * 内部常量
 * ---------------------------------------------------------------------- */

#define HUB_MSG_ID_STOP       UINT32_MAX        /* == HUB_MSG_ID_ALL */
#define HUB_MSG_ID_ACK_ROUTE  (UINT32_MAX - 2u) /* 队列内路由信标，用户不可见 */

#define SUBS_INIT_CAP   8
#define SNAP_STACK_CAP  32

/* -------------------------------------------------------------------------
 * 订阅者结构体（对外不透明）
 * ---------------------------------------------------------------------- */

struct hub_subscriber {
    uint32_t         id;            /**< 内部唯一 ID，用于 ACK 路由 */
    hub_t           *hub;           /**< 反向指针 */
    hub_callback_t   callback;
    void            *user_data;
    uint32_t        *msg_ids;       /**< 堆分配，创建后只读 */
    size_t           msg_id_count;
    int              is_wildcard;
    int              active;        /**< 0 = tombstone */
};

/* -------------------------------------------------------------------------
 * Hub 结构体
 * ---------------------------------------------------------------------- */

struct hub_handle {
    mq_t               *mq;
    pthread_t           thread;
    atomic_int          stop;

    pthread_mutex_t     sub_lock;
    hub_subscriber_t  **subs;       /**< 指针数组，NULL 表示空槽 */
    size_t              sub_count;  /**< 含 NULL 槽的总长度 */
    size_t              sub_cap;
    uint32_t            next_id;    /**< 单调递增，生成订阅者内部 ID */
};

/* -------------------------------------------------------------------------
 * 快照缓冲区
 * ---------------------------------------------------------------------- */

typedef struct {
    hub_callback_t  cb;
    void           *user_data;
} cb_snap_t;

typedef struct {
    cb_snap_t  stack_buf[SNAP_STACK_CAP];
    cb_snap_t *data;
    size_t     count;
    int        on_heap;
} snapshot_t;

static int snap_init(snapshot_t *s, size_t needed)
{
    s->count = 0; s->on_heap = 0;
    if (needed <= SNAP_STACK_CAP) { s->data = s->stack_buf; return 0; }
    s->data = malloc(needed * sizeof(cb_snap_t));
    if (!s->data) return -1;
    s->on_heap = 1;
    return 0;
}

static void snap_free(snapshot_t *s)
{
    if (s->on_heap) { free(s->data); s->data = NULL; }
}

/* -------------------------------------------------------------------------
 * 订阅者匹配：检查 msg_id 是否在订阅者的列表中
 * ---------------------------------------------------------------------- */

static int subscriber_matches(const hub_subscriber_t *s, uint32_t msg_id)
{
    if (!s->active) return 0;
    if (s->is_wildcard) return 1;
    for (size_t i = 0; i < s->msg_id_count; i++)
        if (s->msg_ids[i] == msg_id) return 1;
    return 0;
}

/* -------------------------------------------------------------------------
 * Hub 内部槽位管理（持 sub_lock）
 * ---------------------------------------------------------------------- */

/** 找到一个 NULL 槽或扩容，返回可用下标，失败返回 -1 */
static int subs_reserve_slot(hub_t *hub)
{
    for (size_t i = 0; i < hub->sub_count; i++)
        if (hub->subs[i] == NULL) return (int)i;

    if (hub->sub_count >= hub->sub_cap) {
        size_t new_cap = hub->sub_cap * 2;
        hub_subscriber_t **tmp = realloc(hub->subs,
                                         new_cap * sizeof(hub_subscriber_t *));
        if (!tmp) return -1;
        hub->subs    = tmp;
        hub->sub_cap = new_cap;
    }
    return (int)hub->sub_count++;
}

/* -------------------------------------------------------------------------
 * dispatch：ACK 定向投递
 * ---------------------------------------------------------------------- */

static void dispatch_ack(hub_t *hub, uint32_t target_id)
{
    cb_snap_t snap;
    hub_subscriber_t *found = NULL;

    pthread_mutex_lock(&hub->sub_lock);
    for (size_t i = 0; i < hub->sub_count; i++) {
        hub_subscriber_t *s = hub->subs[i];
        if (s && s->active && s->id == target_id) { found = s; break; }
    }
    if (found) { snap.cb = found->callback; snap.user_data = found->user_data; }
    pthread_mutex_unlock(&hub->sub_lock);

    if (!found) return; /* 已 destroy，静默丢弃 */

    /*
     * 构造 SUB_ACK 消息：
     *   msg_id      = HUB_MSG_ID_SUB_ACK
     *   param.ptr   = 订阅者句柄（只读，供回调内自识别）
     *   priority    = URGENT（与队列中的 ACK_ROUTE 保持一致）
     *
     * 注意：found 在锁外被读取（只取 ptr 值传给回调），
     * 若此时 hub_subscriber_destroy 并发执行：
     *   destroy 会先拿锁将 active 置 0 并将槽位清 NULL，
     *   但不会在锁内 free found（free 发生在锁释放之后）。
     * 这里 snap 已持有 (cb, user_data) 的值拷贝，
     * ACK 消息里 param.ptr = found 仅作标识用，
     * 回调执行时 found 可能已被 destroy——文档中说明回调内不可 free sub。
     *
     * 如需彻底避免悬空指针，可将 param.value 改为内部 id（uint32_t），
     * 由调用方通过 id 反查自己持有的 subscriber 指针。
     */
    mq_msg_t ack;
    ack.msg_id      = HUB_MSG_ID_SUB_ACK;
    ack.param.ptr   = found;   /* 回调内只读，不可释放 */
    ack.priority    = MQ_PRIO_URGENT;

    snap.cb(&ack, snap.user_data);
}

/* -------------------------------------------------------------------------
 * dispatch：业务消息广播
 * ---------------------------------------------------------------------- */

static void dispatch_msg(hub_t *hub, const mq_msg_t *msg)
{
    snapshot_t snap;

    pthread_mutex_lock(&hub->sub_lock);

    size_t match = 0;
    for (size_t i = 0; i < hub->sub_count; i++)
        if (hub->subs[i] && subscriber_matches(hub->subs[i], msg->msg_id))
            match++;

    if (match == 0) { pthread_mutex_unlock(&hub->sub_lock); return; }

    if (snap_init(&snap, match) != 0) {
        pthread_mutex_unlock(&hub->sub_lock);
        fprintf(stderr, "[msg_hub] snapshot OOM, msg_id=%u dropped\n", msg->msg_id);
        return;
    }

    for (size_t i = 0; i < hub->sub_count; i++) {
        hub_subscriber_t *s = hub->subs[i];
        if (s && subscriber_matches(s, msg->msg_id))
            snap.data[snap.count++] = (cb_snap_t){ s->callback, s->user_data };
    }

    pthread_mutex_unlock(&hub->sub_lock);

    for (size_t i = 0; i < snap.count; i++)
        snap.data[i].cb(msg, snap.data[i].user_data);

    snap_free(&snap);
}

/* -------------------------------------------------------------------------
 * 调度线程
 * ---------------------------------------------------------------------- */

static void *dispatch_thread(void *arg)
{
    hub_t   *hub = (hub_t *)arg;
    mq_msg_t msg;

    while (1) {
        if (mq_pop(hub->mq, &msg, -1) != MQ_OK) continue;

        if (msg.msg_id == HUB_MSG_ID_STOP ||
            atomic_load_explicit(&hub->stop, memory_order_acquire))
            break;

        if (msg.msg_id == HUB_MSG_ID_ACK_ROUTE)
            dispatch_ack(hub, (uint32_t)msg.param.value);
        else
            dispatch_msg(hub, &msg);
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * Hub 生命周期
 * ---------------------------------------------------------------------- */

hub_t *hub_create(size_t queue_capacity)
{
    if (!queue_capacity) return NULL;

    hub_t *hub = calloc(1, sizeof(hub_t));
    if (!hub) return NULL;

    hub->mq = mq_create(queue_capacity);
    if (!hub->mq) { free(hub); return NULL; }

    hub->subs = calloc(SUBS_INIT_CAP, sizeof(hub_subscriber_t *));
    if (!hub->subs) { mq_destroy(hub->mq); free(hub); return NULL; }

    hub->sub_cap  = SUBS_INIT_CAP;
    hub->sub_count = 0;
    hub->next_id   = 1;

    atomic_init(&hub->stop, 0);
    pthread_mutex_init(&hub->sub_lock, NULL);

    if (pthread_create(&hub->thread, NULL, dispatch_thread, hub) != 0) {
        pthread_mutex_destroy(&hub->sub_lock);
        free(hub->subs); mq_destroy(hub->mq); free(hub);
        return NULL;
    }
    return hub;
}

void hub_destroy(hub_t *hub)
{
    if (!hub) return;
    atomic_store_explicit(&hub->stop, 1, memory_order_release);
    mq_param_t p = {0};
    mq_push(hub->mq, HUB_MSG_ID_STOP, p, MQ_PRIO_URGENT);
    pthread_join(hub->thread, NULL);
    pthread_mutex_destroy(&hub->sub_lock);
    /* 若调用方未 destroy 所有订阅者，这里清理剩余指针（不 free sub，由调用方负责）*/
    free(hub->subs);
    mq_destroy(hub->mq);
    free(hub);
}

/* -------------------------------------------------------------------------
 * 订阅者生命周期
 * ---------------------------------------------------------------------- */

hub_subscriber_t *hub_subscribe(hub_t          *hub,
                                const uint32_t *msg_ids,
                                size_t          msg_id_count,
                                hub_callback_t  cb,
                                void           *user_data)
{
    if (!hub || !msg_ids || !msg_id_count || !cb) return NULL;

    /* 验证 msg_id 列表，拒绝内部保留值（SUB_ACK、ACK_ROUTE）*/
    int is_wildcard = 0;
    for (size_t i = 0; i < msg_id_count; i++) {
        if (msg_ids[i] == HUB_MSG_ID_ACK_ROUTE ||
            msg_ids[i] == HUB_MSG_ID_SUB_ACK)
            return NULL;
        if (msg_ids[i] == HUB_MSG_ID_ALL)
            is_wildcard = 1;
    }

    /* 分配订阅者结构 */
    hub_subscriber_t *sub = calloc(1, sizeof(hub_subscriber_t));
    if (!sub) return NULL;

    if (!is_wildcard) {
        sub->msg_ids = malloc(msg_id_count * sizeof(uint32_t));
        if (!sub->msg_ids) { free(sub); return NULL; }
        memcpy(sub->msg_ids, msg_ids, msg_id_count * sizeof(uint32_t));
    }
    /* is_wildcard 时 msg_ids 为 NULL，msg_id_count 无意义 */

    sub->msg_id_count = is_wildcard ? 0 : msg_id_count;
    sub->is_wildcard  = is_wildcard;
    sub->callback     = cb;
    sub->user_data    = user_data;
    sub->active       = 1;
    sub->hub          = hub;

    /* 注册到 Hub */
    pthread_mutex_lock(&hub->sub_lock);
    int idx = subs_reserve_slot(hub);
    if (idx < 0) {
        pthread_mutex_unlock(&hub->sub_lock);
        free(sub->msg_ids); free(sub);
        return NULL;
    }
    sub->id      = hub->next_id++;
    hub->subs[idx] = sub;
    uint32_t ack_target_id = sub->id;
    pthread_mutex_unlock(&hub->sub_lock);

    /* 推 ACK_ROUTE（URGENT），param.value 携带内部 id */
    mq_param_t p;
    p.value = (uintptr_t)ack_target_id;
    mq_push(hub->mq, HUB_MSG_ID_ACK_ROUTE, p, MQ_PRIO_URGENT);

    return sub;
}

void hub_unsubscribe(hub_subscriber_t *sub)
{
    if (!sub) return;

    hub_t *hub = sub->hub;

    pthread_mutex_lock(&hub->sub_lock);
    sub->active = 0;
    /* 从指针数组中清除，归还空槽 */
    for (size_t i = 0; i < hub->sub_count; i++) {
        if (hub->subs[i] == sub) { hub->subs[i] = NULL; break; }
    }
    pthread_mutex_unlock(&hub->sub_lock);

    /* 锁外释放（dispatch 快照只持有值拷贝，不再引用 sub 结构）*/
    free(sub->msg_ids);
    free(sub);
}

size_t hub_subscribed_list(const hub_subscriber_t *sub,
                           uint32_t               *out,
                           size_t                  out_cap)
{
    if (!sub) return 0;

    if (sub->is_wildcard) {
        if (out && out_cap >= 1) out[0] = HUB_MSG_ID_ALL;
        return 1;
    }

    size_t n = sub->msg_id_count;
    if (out) {
        size_t copy = n < out_cap ? n : out_cap;
        memcpy(out, sub->msg_ids, copy * sizeof(uint32_t));
    }
    return n;
}

/* -------------------------------------------------------------------------
 * 发布
 * ---------------------------------------------------------------------- */

mq_err_t hub_publish(hub_t *hub, uint32_t msg_id,
                     mq_param_t param, mq_priority_t priority)
{
    if (!hub) return MQ_EINVAL;
    if (msg_id == HUB_MSG_ID_STOP     ||
        msg_id == HUB_MSG_ID_ACK_ROUTE ||
        msg_id == HUB_MSG_ID_SUB_ACK)
        return MQ_EINVAL;
    return mq_push(hub->mq, msg_id, param, priority);
}

size_t hub_pending(hub_t *hub)
{
    return hub ? mq_size(hub->mq) : 0;
}
