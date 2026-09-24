#ifndef CO_NMT_H
#define CO_NMT_H
#include "co_core.h"
#include "co_device_od.h"

typedef enum
{
    CO_NMT_START = 0x01,              // 进入 Operational状态
    CO_NMT_STOP = 0x02,               // 进入 Stopped状态
    CO_NMT_ENTER_PREOP = 0x80,        // 进入 Pre-operational状态
    CO_NMT_RESET_NODE = 0x81,         // 重新初始化并发送 Boot-up（节点复位）
    CO_NMT_RESET_COMMUNICATION = 0x82 // 复位通信部分并发送 Boot-up（通信复位）
} co_nmt_command_t;

/* Called after a state change. Must not re-enter protocol APIs.
 * On INITIALIZATION/non-operational states the application applies safe
 * physical outputs; retained OD output commands are not a GPIO policy.
 * Reset also clears future service timers/queues here. No HAL in this layer. */
typedef void (*co_nmt_notify_fn)(void *user, co_nmt_state_t state); // 用于保存“状态变化通知回调函数”的地址

typedef struct
{
    co_context_t *node;
    co_device_od_t *device;
    co_nmt_notify_fn notify;
    void *user;
} co_nmt_t;

/* Bind initialized, stable-address objects. Does not send a frame.
 * Caller serializes all accesses; notify may be NULL for PC-only use. */
co_status_t co_nmt_init(co_nmt_t *nmt, co_context_t *node,
                        co_device_od_t *device, co_nmt_notify_fn notify, void *user);
/* Complete initialization: submit Boot-up once, then enter Pre-op.
 * Busy/failure leaves INITIALIZATION; retry this API, not the reset command.
 * CO_OK means accepted by transport, not CAN ACK. */
co_status_t co_nmt_bootup(co_nmt_t *nmt);
/* Validates/filters the frame itself. Unknown commands and unrelated frames
 * return CO_IGNORED. No direct response for Start/Stop/Pre-op.
 * Resets restore OD scope, notify INITIALIZATION, then attempt Boot-up. */
co_status_t co_nmt_receive(co_nmt_t *nmt, const can_frame_t *frame);

#endif
