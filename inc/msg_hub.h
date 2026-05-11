/**
 * @file  msg_hub.h
 * @brief 基于消息队列的发布/订阅 Hub
 *
 * 特性：
 *   - 内部单调度线程，消费 msg_queue 并分发到已注册的订阅者
 *   - 按 msg_id 精确订阅，或用 HUB_MSG_ID_ALL 订阅全部消息
 *   - 支持多个订阅者绑定同一 msg_id
 *   - 订阅成功后，订阅者会收到一条 HUB_MSG_ID_SUB_ACK 确认消息，
 *     该消息保证是该订阅者收到的第一条消息
 *   - 回调在 Hub 内部线程上下文执行，回调中可安全调用 hub_publish/subscribe/unsubscribe
 *   - hub_destroy 优雅停止：stop flag + sentinel 消息唤醒阻塞的 pop
 *
 * 线程模型：
 *   外部线程 ─── hub_publish() ──▶ msg_queue ──▶ [dispatch thread] ──▶ callbacks
 *   任意线程均可调用 hub_subscribe / hub_unsubscribe / hub_publish
 *
 * SUB_ACK 有序性说明：
 *   ACK 消息以 URGENT 优先级入队，保证先于 NORMAL/HIGH/LOW 消息送达。
 *   若订阅时队列中已存在其他 URGENT 消息，ACK 会排在它们之后（按 FIFO 序）。
 *   实际使用中，subscribe 后再 publish 的消息均晚于 ACK 入队，顺序有保证。
 *
 * 保留的 msg_id（不可在业务中使用）：
 *   UINT32_MAX     - 内部 stop sentinel
 *   UINT32_MAX - 1 - 内部 SUB_ACK 路由消息
 */

#ifndef MSG_HUB_H
#define MSG_HUB_H

#include "msg_queue.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 保留 msg_id 常量
 * ---------------------------------------------------------------------- */

/**
 * 传入 hub_subscribe 的通配符，订阅所有业务消息。
 * 通配符订阅者不会收到其他订阅者的 SUB_ACK，只收到自己的 ACK。
 */
#define HUB_MSG_ID_ALL      UINT32_MAX

/**
 * 订阅确认消息的 msg_id。
 * 订阅者通过 msg->msg_id == HUB_MSG_ID_SUB_ACK 识别该消息。
 * msg->param.value 包含对应的 hub_sub_id_t（订阅句柄）。
 */
#define HUB_MSG_ID_SUB_ACK  (UINT32_MAX - 1u)

/* -------------------------------------------------------------------------
 * 类型定义
 * ---------------------------------------------------------------------- */

/**
 * 订阅回调函数类型。
 * @param msg       完整消息（只读）。首次调用时 msg_id == HUB_MSG_ID_SUB_ACK，
 *                  param.value 为该订阅的 sub_id。
 * @param user_data 注册时传入的用户数据
 */
typedef void (*hub_callback_t)(const mq_msg_t *msg, void *user_data);

/** 订阅句柄，用于 unsubscribe；0 为无效值 */
typedef uint32_t hub_sub_id_t;

/** 不透明 Hub 句柄 */
typedef struct hub_handle hub_t;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/**
 * 创建 Hub 并启动内部调度线程
 * @param queue_capacity  内部消息队列容量（> 0，需预留 ACK 所需空间）
 * @return 成功返回句柄，失败返回 NULL
 */
hub_t *hub_create(size_t queue_capacity);

/**
 * 停止调度线程并销毁 Hub。
 * 等待内部线程退出后再释放资源；调用后所有订阅自动失效。
 */
void hub_destroy(hub_t *hub);

/**
 * 订阅消息
 *
 * 订阅成功后，订阅者将收到一条 HUB_MSG_ID_SUB_ACK 消息作为第一条回调，
 * 其中 param.value 为本次返回的 sub_id。
 *
 * @param hub      Hub 句柄
 * @param msg_id   要订阅的消息 ID；传入 HUB_MSG_ID_ALL 则订阅所有业务消息
 * @param cb       回调函数（不可为 NULL）
 * @param user_data 透传给回调的用户数据（可为 NULL）
 * @return 成功返回非零订阅句柄，失败返回 0
 */
hub_sub_id_t hub_subscribe(hub_t *hub, uint32_t msg_id,
                            hub_callback_t cb, void *user_data);

/**
 * 取消订阅
 * @param hub     Hub 句柄
 * @param sub_id  由 hub_subscribe 返回的句柄
 */
void hub_unsubscribe(hub_t *hub, hub_sub_id_t sub_id);

/**
 * 发布消息（非阻塞入队）
 *
 * @param hub       Hub 句柄
 * @param msg_id    消息 ID（不应使用 UINT32_MAX 或 UINT32_MAX-1，为内部保留值）
 * @param param     消息参数
 * @param priority  消息优先级
 * @return MQ_OK / MQ_FULL / MQ_EINVAL
 */
mq_err_t hub_publish(hub_t *hub, uint32_t msg_id,
                      mq_param_t param, mq_priority_t priority);

/**
 * 获取当前队列中待处理的消息数量
 */
size_t hub_pending(hub_t *hub);

#ifdef __cplusplus
}
#endif

#endif /* MSG_HUB_H */
