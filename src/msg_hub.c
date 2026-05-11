/**
 * @file  msg_hub.c
 * @brief 发布/订阅 Hub 实现（含 SUB_ACK 机制）
 *
 * SUB_ACK 实现方式：
 *   hub_subscribe() 注册订阅者后，立即向队列推一条内部 ACK 路由消息：
 *     msg_id  = HUB_MSG_ID_ACK_ROUTE   (内部值，不暴露给客户端)
 *     param   = sub_id                 (目标订阅者)
 *     priority= URGENT                 (确保先于后续业务消息处理)
 *
 *   dispatch 线程收到 ACK_ROUTE 后：
 *     - 按 sub_id 精确定位订阅者（仅该订阅者，不触发通配符）
 *     - 构造 HUB_MSG_ID_SUB_ACK 消息回调给它
 *     - 若订阅者已 unsubscribe，则静默丢弃
 *
 * 内部保留 msg_id：
 *   HUB_MSG_ID_STOP      (UINT32_MAX)       stop sentinel
 *   HUB_MSG_ID_ACK_ROUTE (UINT32_MAX - 2)   ACK 路由，内部使用
 *   HUB_MSG_ID_SUB_ACK   (UINT32_MAX - 1)   对外可见的 ACK msg_id（见头文件）
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

#define HUB_MSG_ID_STOP       UINT32_MAX
#define HUB_MSG_ID_ACK_ROUTE  (UINT32_MAX - 2u)  /* 内部路由，客户端不可见 */

#define SUB_INIT_CAP    8
#define SNAP_STACK_CAP  32

/* -------------------------------------------------------------------------
 * 内部类型
 * ---------------------------------------------------------------------- */

typedef struct {
    hub_callback_t  cb;
    void           *user_data;
} cb_snapshot_t;

typedef struct {
    hub_sub_id_t    sub_id;
    uint32_t        msg_id;
    int             is_wildcard;
    int             active;
    hub_callback_t  callback;
    void           *user_data;
} hub_subscriber_t;

struct hub_handle {
    mq_t               *mq;
    pthread_t           thread;
    atomic_int          stop;

    pthread_mutex_t     sub_lock;
    hub_subscriber_t   *subs;
    size_t              sub_count;
    size_t              sub_cap;
    uint32_t            next_sub_id;
};

/* -------------------------------------------------------------------------
 * 快照缓冲区（优先栈，必要时堆回退）
 * ---------------------------------------------------------------------- */

typedef struct {
    cb_snapshot_t  stack_buf[SNAP_STACK_CAP];
    cb_snapshot_t *data;
    size_t         count;
    int            heap_allocated;
} snapshot_t;

static int snapshot_init(snapshot_t *s, size_t needed)
{
    s->count = 0;
    s->heap_allocated = 0;
    if (needed <= SNAP_STACK_CAP) {
        s->data = s->stack_buf;
    } else {
        s->data = (cb_snapshot_t *)malloc(needed * sizeof(cb_snapshot_t));
        if (!s->data) return -1;
        s->heap_allocated = 1;
    }
    return 0;
}

static void snapshot_free(snapshot_t *s)
{
    if (s->heap_allocated) { free(s->data); s->data = NULL; }
}

/* -------------------------------------------------------------------------
 * 订阅者槽管理
 * ---------------------------------------------------------------------- */

static int subs_reserve_slot(hub_t *hub)
{
    for (size_t i = 0; i < hub->sub_count; i++)
        if (!hub->subs[i].active) return (int)i;

    if (hub->sub_count >= hub->sub_cap) {
        size_t new_cap = hub->sub_cap * 2;
        hub_subscriber_t *tmp = (hub_subscriber_t *)realloc(
            hub->subs, new_cap * sizeof(hub_subscriber_t));
        if (!tmp) return -1;
        hub->subs    = tmp;
        hub->sub_cap = new_cap;
    }
    return (int)hub->sub_count++;
}

/* -------------------------------------------------------------------------
 * dispatch：处理 ACK_ROUTE 消息（定向回调单个订阅者）
 *
 * 构造一条对外可见的 HUB_MSG_ID_SUB_ACK 消息，仅回调 sub_id 对应的订阅者。
 * 通配符订阅者不参与此流程（每人只收自己的 ACK）。
 * ---------------------------------------------------------------------- */

static void dispatch_ack(hub_t *hub, hub_sub_id_t target_sub_id)
{
    cb_snapshot_t snap_entry;
    int found = 0;

    pthread_mutex_lock(&hub->sub_lock);
    for (size_t i = 0; i < hub->sub_count; i++) {
        hub_subscriber_t *s = &hub->subs[i];
        if (s->active && s->sub_id == target_sub_id) {
            snap_entry.cb        = s->callback;
            snap_entry.user_data = s->user_data;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&hub->sub_lock);

    if (!found) return;  /* 已 unsubscribe，静默丢弃 */

    /* 构造对外可见的 ACK 消息 */
    mq_msg_t ack_msg;
    ack_msg.msg_id        = HUB_MSG_ID_SUB_ACK;
    ack_msg.param.value   = (uintptr_t)target_sub_id;
    ack_msg.priority      = MQ_PRIO_URGENT;

    snap_entry.cb(&ack_msg, snap_entry.user_data);
}

/* -------------------------------------------------------------------------
 * dispatch：处理普通业务消息
 * ---------------------------------------------------------------------- */

static void dispatch_msg(hub_t *hub, const mq_msg_t *msg)
{
    snapshot_t snap;

    pthread_mutex_lock(&hub->sub_lock);

    size_t match = 0;
    for (size_t i = 0; i < hub->sub_count; i++) {
        const hub_subscriber_t *s = &hub->subs[i];
        if (s->active && (s->is_wildcard || s->msg_id == msg->msg_id))
            match++;
    }

    if (match == 0) { pthread_mutex_unlock(&hub->sub_lock); return; }

    if (snapshot_init(&snap, match) != 0) {
        pthread_mutex_unlock(&hub->sub_lock);
        fprintf(stderr, "[msg_hub] snapshot OOM, msg_id=%u dropped\n", msg->msg_id);
        return;
    }

    for (size_t i = 0; i < hub->sub_count; i++) {
        const hub_subscriber_t *s = &hub->subs[i];
        if (s->active && (s->is_wildcard || s->msg_id == msg->msg_id))
            snap.data[snap.count++] = (cb_snapshot_t){ s->callback, s->user_data };
    }

    pthread_mutex_unlock(&hub->sub_lock);

    for (size_t i = 0; i < snap.count; i++)
        snap.data[i].cb(msg, snap.data[i].user_data);

    snapshot_free(&snap);
}

/* -------------------------------------------------------------------------
 * 调度线程
 * ---------------------------------------------------------------------- */

static void *dispatch_thread(void *arg)
{
    hub_t   *hub = (hub_t *)arg;
    mq_msg_t msg;

    while (1) {
        mq_err_t rc = mq_pop(hub->mq, &msg, -1);
        if (rc != MQ_OK) continue;

        if (msg.msg_id == HUB_MSG_ID_STOP ||
            atomic_load_explicit(&hub->stop, memory_order_acquire))
            break;

        if (msg.msg_id == HUB_MSG_ID_ACK_ROUTE) {
            /* 定向 ACK：只回调 param.value 指定的订阅者 */
            dispatch_ack(hub, (hub_sub_id_t)msg.param.value);
        } else {
            dispatch_msg(hub, &msg);
        }
    }

    return NULL;
}

/* -------------------------------------------------------------------------
 * 公共 API
 * ---------------------------------------------------------------------- */

hub_t *hub_create(size_t queue_capacity)
{
    if (queue_capacity == 0) return NULL;

    hub_t *hub = (hub_t *)calloc(1, sizeof(hub_t));
    if (!hub) return NULL;

    hub->mq = mq_create(queue_capacity);
    if (!hub->mq) { free(hub); return NULL; }

    hub->subs = (hub_subscriber_t *)malloc(SUB_INIT_CAP * sizeof(hub_subscriber_t));
    if (!hub->subs) { mq_destroy(hub->mq); free(hub); return NULL; }

    hub->sub_cap     = SUB_INIT_CAP;
    hub->sub_count   = 0;
    hub->next_sub_id = 1;

    atomic_init(&hub->stop, 0);
    pthread_mutex_init(&hub->sub_lock, NULL);

    if (pthread_create(&hub->thread, NULL, dispatch_thread, hub) != 0) {
        pthread_mutex_destroy(&hub->sub_lock);
        free(hub->subs);
        mq_destroy(hub->mq);
        free(hub);
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
    free(hub->subs);
    mq_destroy(hub->mq);
    free(hub);
}

hub_sub_id_t hub_subscribe(hub_t *hub, uint32_t msg_id,
                            hub_callback_t cb, void *user_data)
{
    if (!hub || !cb) return 0;

    /*
     * 拦截仅用于内部队列路由的 msg_id。
     * HUB_MSG_ID_ALL(UINT32_MAX) == HUB_MSG_ID_STOP，但作为订阅过滤器合法，
     * is_wildcard 标志负责处理；只有 ACK_ROUTE 对订阅目标完全无意义。
     */
    if (msg_id == HUB_MSG_ID_ACK_ROUTE)
        return 0;

    pthread_mutex_lock(&hub->sub_lock);

    int idx = subs_reserve_slot(hub);
    if (idx < 0) { pthread_mutex_unlock(&hub->sub_lock); return 0; }

    hub_subscriber_t *s = &hub->subs[idx];
    s->sub_id      = hub->next_sub_id++;
    s->msg_id      = msg_id;
    s->is_wildcard = (msg_id == HUB_MSG_ID_ALL) ? 1 : 0;
    s->active      = 1;
    s->callback    = cb;
    s->user_data   = user_data;

    hub_sub_id_t id = s->sub_id;

    pthread_mutex_unlock(&hub->sub_lock);

    /*
     * 向队列推 ACK_ROUTE 消息（URGENT）。
     * dispatch 线程按 sub_id 精确找到该订阅者并回调 HUB_MSG_ID_SUB_ACK。
     * 注：此时订阅者已在 subs[] 中，dispatch 线程可安全查找。
     */
    mq_param_t p;
    p.value = (uintptr_t)id;
    mq_push(hub->mq, HUB_MSG_ID_ACK_ROUTE, p, MQ_PRIO_URGENT);

    return id;
}

void hub_unsubscribe(hub_t *hub, hub_sub_id_t sub_id)
{
    if (!hub || sub_id == 0) return;

    pthread_mutex_lock(&hub->sub_lock);
    for (size_t i = 0; i < hub->sub_count; i++) {
        if (hub->subs[i].sub_id == sub_id) {
            hub->subs[i].active = 0;
            break;
        }
    }
    pthread_mutex_unlock(&hub->sub_lock);
}

mq_err_t hub_publish(hub_t *hub, uint32_t msg_id,
                      mq_param_t param, mq_priority_t priority)
{
    if (!hub) return MQ_EINVAL;

    /* 拦截所有内部保留 msg_id */
    if (msg_id == HUB_MSG_ID_STOP     ||
        msg_id == HUB_MSG_ID_ACK_ROUTE ||
        msg_id == HUB_MSG_ID_SUB_ACK)
        return MQ_EINVAL;

    return mq_push(hub->mq, msg_id, param, priority);
}

size_t hub_pending(hub_t *hub)
{
    if (!hub) return 0;
    return mq_size(hub->mq);
}
