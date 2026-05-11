/**
 * @file  msg_queue.c
 * @brief 基于优先级的线程安全消息队列实现
 *
 * 数据结构：二叉最大堆
 *   排序键：(priority DESC, seq ASC)
 *     - priority 越大越先出队
 *     - priority 相同时，seq 越小越先出队（FIFO）
 *
 * 复杂度：
 *   push : O(log n)
 *   pop  : O(log n)
 *   size : O(1)
 */

#define _POSIX_C_SOURCE 200809L
#include "msg_queue.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * 内部节点（堆元素，含 seq 字段）
 * ---------------------------------------------------------------------- */
typedef struct {
    uint32_t      msg_id;
    mq_param_t    param;
    mq_priority_t priority;
    uint64_t      seq;     /**< 单调递增，保证同优先级 FIFO */
} mq_node_t;

/* -------------------------------------------------------------------------
 * 句柄定义
 * ---------------------------------------------------------------------- */
struct mq_handle {
    mq_node_t      *heap;         /**< 堆数组，capacity 个元素 */
    size_t          size;         /**< 当前元素个数 */
    size_t          capacity;     /**< 最大容量 */
    uint64_t        seq_counter;  /**< 序列号生成器 */
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;    /**< pop 阻塞等待的条件变量 */
};

/* -------------------------------------------------------------------------
 * 堆操作辅助函数（均在持锁状态下调用）
 * ---------------------------------------------------------------------- */

/**
 * 比较：a 是否应在 b 之前出队（即 a 的优先级更高）
 *   priority 大的优先；相同 priority 下 seq 小的优先（FIFO）
 */
static inline int node_has_priority(const mq_node_t *a, const mq_node_t *b)
{
    if (a->priority != b->priority)
        return a->priority > b->priority;
    return a->seq < b->seq;
}

static inline void node_swap(mq_node_t *a, mq_node_t *b)
{
    mq_node_t tmp = *a;
    *a = *b;
    *b = tmp;
}

/** 上浮：新元素插入堆尾后调用 */
static void sift_up(mq_node_t *heap, size_t i)
{
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (node_has_priority(&heap[i], &heap[parent])) {
            node_swap(&heap[i], &heap[parent]);
            i = parent;
        } else {
            break;
        }
    }
}

/** 下沉：堆顶弹出、末尾元素移到堆顶后调用 */
static void sift_down(mq_node_t *heap, size_t size, size_t i)
{
    while (1) {
        size_t best  = i;
        size_t left  = 2 * i + 1;
        size_t right = 2 * i + 2;

        if (left  < size && node_has_priority(&heap[left],  &heap[best])) best = left;
        if (right < size && node_has_priority(&heap[right], &heap[best])) best = right;

        if (best == i)
            break;

        node_swap(&heap[i], &heap[best]);
        i = best;
    }
}

/* -------------------------------------------------------------------------
 * 公共 API
 * ---------------------------------------------------------------------- */

mq_t *mq_create(size_t capacity)
{
    if (capacity == 0)
        return NULL;

    mq_t *mq = (mq_t *)calloc(1, sizeof(mq_t));
    if (!mq)
        return NULL;

    mq->heap = (mq_node_t *)malloc(capacity * sizeof(mq_node_t));
    if (!mq->heap) {
        free(mq);
        return NULL;
    }

    mq->capacity    = capacity;
    mq->size        = 0;
    mq->seq_counter = 0;

    pthread_mutex_init(&mq->lock, NULL);
    pthread_cond_init(&mq->not_empty, NULL);

    return mq;
}

void mq_destroy(mq_t *mq)
{
    if (!mq)
        return;

    /* 广播唤醒所有阻塞在 pop 的线程，由调用方确保它们能安全退出 */
    pthread_mutex_lock(&mq->lock);
    pthread_cond_broadcast(&mq->not_empty);
    pthread_mutex_unlock(&mq->lock);

    pthread_cond_destroy(&mq->not_empty);
    pthread_mutex_destroy(&mq->lock);
    free(mq->heap);
    free(mq);
}

mq_err_t mq_push(mq_t *mq, uint32_t msg_id, mq_param_t param, mq_priority_t priority)
{
    if (!mq)
        return MQ_EINVAL;

    pthread_mutex_lock(&mq->lock);

    if (mq->size >= mq->capacity) {
        pthread_mutex_unlock(&mq->lock);
        return MQ_FULL;
    }

    /* 写入堆尾，然后上浮 */
    mq_node_t *node = &mq->heap[mq->size];
    node->msg_id   = msg_id;
    node->param    = param;
    node->priority = priority;
    node->seq      = mq->seq_counter++;

    sift_up(mq->heap, mq->size);
    mq->size++;

    /* 唤醒一个阻塞在 pop 的线程 */
    pthread_cond_signal(&mq->not_empty);
    pthread_mutex_unlock(&mq->lock);

    return MQ_OK;
}

mq_err_t mq_pop(mq_t *mq, mq_msg_t *out, int timeout_ms)
{
    if (!mq || !out)
        return MQ_EINVAL;

    struct timespec deadline;

    /* 提前计算超时绝对时间（在锁外，避免持锁调用系统调用）*/
    if (timeout_ms > 0) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec  += (time_t)(timeout_ms / 1000);
        deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
    }

    pthread_mutex_lock(&mq->lock);

    /* 等待队列非空 */
    while (mq->size == 0) {
        if (timeout_ms == 0) {
            /* 非阻塞模式 */
            pthread_mutex_unlock(&mq->lock);
            return MQ_EMPTY;
        }

        int rc;
        if (timeout_ms < 0) {
            /* 永久阻塞 */
            rc = pthread_cond_wait(&mq->not_empty, &mq->lock);
        } else {
            /* 有限超时 */
            rc = pthread_cond_timedwait(&mq->not_empty, &mq->lock, &deadline);
        }

        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&mq->lock);
            return MQ_TIMEOUT;
        }
        /* EINTR 或其它：重新检查条件（spurious wakeup 安全）*/
    }

    /* 取堆顶（最高优先级消息）*/
    const mq_node_t *top = &mq->heap[0];
    out->msg_id   = top->msg_id;
    out->param    = top->param;
    out->priority = top->priority;

    /* 将末尾元素移到堆顶，然后下沉 */
    mq->size--;
    if (mq->size > 0) {
        mq->heap[0] = mq->heap[mq->size];
        sift_down(mq->heap, mq->size, 0);
    }

    pthread_mutex_unlock(&mq->lock);
    return MQ_OK;
}

size_t mq_size(mq_t *mq)
{
    if (!mq)
        return 0;

    pthread_mutex_lock(&mq->lock);
    size_t s = mq->size;
    pthread_mutex_unlock(&mq->lock);
    return s;
}

int mq_is_empty(mq_t *mq)
{
    return mq_size(mq) == 0;
}
