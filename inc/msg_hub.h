/**
 * @file  msg_hub.h
 * @brief 基于消息队列的发布/订阅 Hub
 *
 * 订阅者模型：
 *   - hub_subscriber_t 是核心管理单元，对应一个逻辑订阅者
 *   - 一次 hub_subscriber_create 可绑定多个 msg_id
 *   - 一个订阅者只收到一条 SUB_ACK（注册确认），与订阅了几个 msg_id 无关
 *   - hub_subscriber_destroy 一次性注销该订阅者的所有 msg_id 绑定
 *
 * SUB_ACK 语义：
 *   订阅者注册成功 → 收到唯一一条 HUB_MSG_ID_SUB_ACK，作为"第一条消息"
 *   msg->param.ptr 指向该订阅者自身（hub_subscriber_t *），可用于回调内自识别
 *
 * 保留的 msg_id（不可在业务中使用）：
 *   UINT32_MAX     - HUB_MSG_ID_ALL      订阅通配符 / 内部 stop sentinel
 *   UINT32_MAX - 1 - HUB_MSG_ID_SUB_ACK  订阅确认（仅出现在回调参数中）
 *   UINT32_MAX - 2 - 内部 ACK 路由消息   仅在队列内部流转，用户不可见
 */

#ifndef MSG_HUB_H
#define MSG_HUB_H

#include "msg_queue.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 常量
 * ---------------------------------------------------------------------- */

/** 订阅通配符：订阅所有业务消息 */
#define HUB_MSG_ID_ALL      UINT32_MAX

/**
 * 订阅确认消息的 msg_id，仅出现在回调的 mq_msg_t 中，从不进入队列。
 * msg->param.ptr 为该订阅者的 hub_subscriber_t 指针（只读，勿释放）。
 */
#define HUB_MSG_ID_SUB_ACK  (UINT32_MAX - 1u)

/* -------------------------------------------------------------------------
 * 类型
 * ---------------------------------------------------------------------- */

typedef struct hub_handle     hub_t;
typedef struct hub_subscriber hub_subscriber_t;

/**
 * 回调函数类型。
 * 在 Hub 内部线程上执行，不应长时间阻塞。
 * 回调内可安全调用 hub_publish / hub_subscriber_create / hub_subscriber_destroy。
 *
 * @param msg        消息（只读）
 * @param user_data  注册时传入的用户数据
 */
typedef void (*hub_callback_t)(const mq_msg_t *msg, void *user_data);

/* -------------------------------------------------------------------------
 * Hub 生命周期
 * ---------------------------------------------------------------------- */

/**
 * 创建 Hub 并启动内部调度线程。
 * @param queue_capacity  消息队列容量（建议预留订阅者数量的余量供 ACK 使用）
 */
hub_t *hub_create(size_t queue_capacity);

/**
 * 销毁 Hub，等待内部线程退出后释放所有资源。
 * 调用前所有订阅者应已 destroy，或接受回调不再触发。
 */
void hub_destroy(hub_t *hub);

/* -------------------------------------------------------------------------
 * 订阅者生命周期
 * ---------------------------------------------------------------------- */

/**
 * 创建订阅者并注册到 Hub。
 *
 * @param hub           Hub 句柄
 * @param msg_ids       要订阅的 msg_id 数组
 *                        - 传入单个 HUB_MSG_ID_ALL 以订阅所有业务消息
 *                        - 不可包含保留值（HUB_MSG_ID_SUB_ACK 等）
 * @param msg_id_count  数组长度（> 0）
 * @param cb            回调函数（不可为 NULL）
 * @param user_data     透传给回调的用户数据
 * @return 成功返回订阅者句柄，失败返回 NULL
 *
 * 注册成功后，订阅者收到的第一条消息保证是 HUB_MSG_ID_SUB_ACK。
 */
hub_subscriber_t *hub_subscribe(hub_t             *hub,
                                const uint32_t    *msg_ids,
                                size_t             msg_id_count,
                                hub_callback_t     cb,
                                void              *user_data);

/**
 * 销毁订阅者，注销其全部 msg_id 绑定并释放资源。
 * 调用后 sub 指针不可再使用。
 */
void hub_unsubscribe(hub_subscriber_t *sub);

/**
 * 查询订阅者已订阅的 msg_id 列表（线程安全快照）。
 *
 * @param sub      订阅者句柄
 * @param out      输出缓冲区（可为 NULL，此时仅返回数量）
 * @param out_cap  缓冲区容量
 * @return 该订阅者实际订阅的 msg_id 数量
 *
 * 通配符订阅者返回 1，out[0] == HUB_MSG_ID_ALL。
 */
size_t hub_subscribed_list(const hub_subscriber_t *sub,
                           uint32_t               *out,
                           size_t                  out_cap);

/* -------------------------------------------------------------------------
 * 发布
 * ---------------------------------------------------------------------- */

/**
 * 发布消息（非阻塞入队）。
 * msg_id 不可使用保留值（UINT32_MAX, UINT32_MAX-1, UINT32_MAX-2）。
 */
mq_err_t hub_publish(hub_t *hub, uint32_t msg_id,
                     mq_param_t param, mq_priority_t priority);

/** 队列中待处理的消息数量 */
size_t hub_pending(hub_t *hub);

#ifdef __cplusplus
}
#endif

#endif /* MSG_HUB_H */
