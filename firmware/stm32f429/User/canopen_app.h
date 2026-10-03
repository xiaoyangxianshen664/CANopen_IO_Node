#ifndef CANOPEN_APP_H
#define CANOPEN_APP_H

#include "co_types.h"

/* 初始化 CANopen 节点、对象字典和各协议服务。 */
co_status_t Canopen_App_Init(void);

/* 把收到的帧交给各 CANopen 服务。 */
co_status_t Canopen_App_NmtReceive(const can_frame_t *frame);
co_status_t Canopen_App_SdoReceive(const can_frame_t *frame);
co_status_t Canopen_App_PdoReceive(const can_frame_t *frame);

/* 推进 Heartbeat 和 PDO 的周期处理。 */
co_status_t Canopen_App_ProcessHeartbeat(uint32_t elapsed_ms);
co_status_t Canopen_App_ProcessPdo(uint32_t elapsed_ms);

/* 最近一次需要应用关注的协议或传输错误；CO_IGNORED 不会写入。 */
extern volatile co_status_t canopen_last_error;

#endif
