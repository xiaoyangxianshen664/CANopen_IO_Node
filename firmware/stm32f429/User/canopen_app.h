#ifndef CANOPEN_APP_H
#define CANOPEN_APP_H

#include "co_types.h"
#include "co_emcy.h"

/* 初始化 CANopen 节点、对象字典和各协议服务。 */
co_status_t Canopen_App_Init(void);

/* 把收到的帧交给各 CANopen 服务。 */
co_status_t Canopen_App_NmtReceive(const can_frame_t *frame);
co_status_t Canopen_App_SdoReceive(const can_frame_t *frame);
co_status_t Canopen_App_PdoReceive(const can_frame_t *frame);

/* 推进 Heartbeat 和 PDO 的周期处理。 */
co_status_t Canopen_App_ProcessHeartbeat(uint32_t elapsed_ms);
co_status_t Canopen_App_ProcessPdo(uint32_t elapsed_ms);

/* 把板级输入同步到对象字典，供 PDO/SDO 读取。 */
co_status_t Canopen_App_UpdateInputs(uint8_t di, uint16_t ai1, uint16_t ai2);

/* 读取对象字典中的 DO 命令，供板级输出执行。 */
co_status_t Canopen_App_GetOutputs(uint8_t *outputs);

/* Safety output override and serialized EMCY service entry points. */
uint8_t Canopen_App_IsOperational(void);
void Canopen_App_SetSafeOutput(uint8_t active);
co_status_t Canopen_App_ReportEmcy(uint16_t error_code, uint8_t error_register,
                                   const uint8_t manufacturer[CO_EMCY_MANUFACTURER_LENGTH]);
co_status_t Canopen_App_ClearEmcy(void);
co_status_t Canopen_App_ProcessEmcy(void);

/* 最近一次需要应用关注的协议或传输错误；CO_IGNORED 不会写入。 */
extern volatile co_status_t canopen_last_error;

#endif
