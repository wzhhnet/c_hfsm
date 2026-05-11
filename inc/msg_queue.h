/**
 * @file  msg_queue.h
 * @brief 基于优先级的线程安全消息队列
 *
 * 特性：
 *   - 最大堆实现，O(log n) 入队 / 出队
 *   - 同等优先级按 FIFO 顺序出队（单调递增序列号）
 *   - pthread mutex + condvar，支持阻塞/超时/非阻塞三种出队模式
 *   - 无动态分配（堆内存在 mq_create 时一次性分配）
 */

#ifndef MSG_QUEUE_H
#define MSG_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 类型定义
 * ---------------------------------------------------------------------- */

/** 优先级等级（值越大优先级越高）*/
typedef enum {
    MQ_PRIO_LOW    = 0,
    MQ_PRIO_NORMAL = 1,
    MQ_PRIO_HIGH   = 2,
    MQ_PRIO_URGENT = 3,
} mq_priority_t;

/** 消息参数：整数值或指针，二选一 */
typedef union {
    uintptr_t value;   /**< 传递整型参数（如枚举、标志位）*/
    void     *ptr;     /**< 传递堆内存指针（生命周期由调用方管理）*/
} mq_param_t;

/** 消息体（出队后的完整消息）*/
typedef struct {
    uint32_t      msg_id;    /**< 消息 ID */
    mq_param_t    param;     /**< 消息参数 */
    mq_priority_t priority;  /**< 消息优先级 */
    /* seq 字段仅供内部排序，调用方无需关注 */
} mq_msg_t;

/** 返回值 */
typedef enum {
    MQ_OK      =  0,   /**< 成功 */
    MQ_FULL    = -1,   /**< 队列已满（push 时）*/
    MQ_EMPTY   = -2,   /**< 队列为空（非阻塞 pop 时）*/
    MQ_TIMEOUT = -3,   /**< 等待超时 */
    MQ_EINVAL  = -4,   /**< 参数错误 */
} mq_err_t;

/** 不透明句柄 */
typedef struct mq_handle mq_t;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/**
 * 创建消息队列
 * @param capacity  队列最大容量（元素个数，> 0）
 * @return 成功返回句柄，失败返回 NULL
 */
mq_t *mq_create(size_t capacity);

/**
 * 销毁消息队列并释放资源
 * 调用前需确保没有线程阻塞在 mq_pop 上（否则行为未定义）
 */
void mq_destroy(mq_t *mq);

/**
 * 入队（非阻塞）
 * @param mq        队列句柄
 * @param msg_id    消息 ID
 * @param param     消息参数
 * @param priority  优先级
 * @return MQ_OK / MQ_FULL / MQ_EINVAL
 */
mq_err_t mq_push(mq_t *mq, uint32_t msg_id, mq_param_t param, mq_priority_t priority);

/**
 * 出队（支持阻塞 / 超时 / 非阻塞）
 * @param mq          队列句柄
 * @param out         [out] 存放出队消息
 * @param timeout_ms  超时时间（毫秒）
 *                      < 0  : 永久阻塞直到有消息
 *                        0  : 非阻塞，队空立即返回 MQ_EMPTY
 *                      > 0  : 最多等待 timeout_ms 毫秒
 * @return MQ_OK / MQ_EMPTY / MQ_TIMEOUT / MQ_EINVAL
 */
mq_err_t mq_pop(mq_t *mq, mq_msg_t *out, int timeout_ms);

/**
 * 获取当前队列中消息数量（线程安全）
 */
size_t mq_size(mq_t *mq);

/**
 * 检查队列是否为空（线程安全快照，结果仅供参考）
 */
int mq_is_empty(mq_t *mq);

#ifdef __cplusplus
}
#endif

#endif /* MSG_QUEUE_H */
