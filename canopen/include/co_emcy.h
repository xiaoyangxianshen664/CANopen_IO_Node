#ifndef CO_EMCY_H
#define CO_EMCY_H

#include "co_device_od.h"
/*1：报文参数*/
#define CO_EMCY_COB_BASE UINT32_C(0x080) // CO_EMCY_COB_BASE 是 EMCY 的 CAN-ID 基址。实际 ID 要加节点号：节点 1 是 0x080 + 1 = 0x081
#define CO_EMCY_DATA_LENGTH 8u           // CO_EMCY_DATA_LENGTH 表示 EMCY 帧固定有 8 个数据字节
#define CO_EMCY_MANUFACTURER_LENGTH 5u   // 其中前 3 字节放错误码和 Error Register，剩下 5 字节是厂商自定义信息，所以长度是 5

/*2：故障码：这些值是放进 EMCY 帧 前两个字节的 Error Code，用来说明“发生了什么故障”。*/
#define CO_EMCY_ERROR_GENERIC UINT16_C(0x1000)         // 0x1000：表示通用错误。
#define CO_EMCY_ERROR_CAN UINT16_C(0x8100)             // 0x8100：CANopen 错误码中属于监控类的范围，用于 CAN 通信故障。
#define CO_EMCY_ERROR_CAN_RX_OVERFLOW UINT16_C(0xFF01) // 0xFF01 到 0xFF06：项目自定义的故障码，分别对应接收溢出、Bus-off、通信超时、ADC、输出和看门狗复位。
#define CO_EMCY_ERROR_BUS_OFF UINT16_C(0xFF02)
#define CO_EMCY_ERROR_COMM_TIMEOUT UINT16_C(0xFF03)
#define CO_EMCY_ERROR_ADC UINT16_C(0xFF04)
#define CO_EMCY_ERROR_OUTPUT UINT16_C(0xFF05)
#define CO_EMCY_ERROR_WATCHDOG_RESET UINT16_C(0xFF06)
/*注意，0xFF03 和 0xFF05 目前只是定义了故障码；对应的通信超时检测和输出故障反馈尚未接入，不能因为有宏就认为检测功能已经完成。*/

/*3：Error Register 位：这些值对应 EMCY 帧的 第 3 个字节，每一位代表一类错误。它们可以按位组合：*/
#define CO_EMCY_REGISTER_GENERIC UINT8_C(0x01)       // 通用错误       0x01  bit0
#define CO_EMCY_REGISTER_CURRENT UINT8_C(0x02)       // 电流错误       0x02  bit1
#define CO_EMCY_REGISTER_VOLTAGE UINT8_C(0x04)       // 电压错误       0x04  bit2
#define CO_EMCY_REGISTER_TEMPERATURE UINT8_C(0x08)   // 温度错误       0x08  bit3
#define CO_EMCY_REGISTER_COMMUNICATION UINT8_C(0x10) // 通信错误       0x10  bit4
#define CO_EMCY_REGISTER_MANUFACTURER UINT8_C(0x80)  // 厂商自定义错误 0x80  bit7
/*通信故障时，通信位是 0x10。代码还会自动加上通用错误位 0x01，合起来就是 0x11，假如此时发生了Bus-off故障，那么EMCY 帧 前三个字节就是0xFF02（错误发）+0x11（Error Register）*/

typedef struct
{
    co_context_t *node;                                       // node 指向 CANopen 节点上下文。EMCY 模块需要通过它取得 Node-ID，并调用节点的发送函数tx
    co_device_od_t *device;                                   // device 指向设备对象字典。报告或清除故障时，需要更新 0x1001:00 Error Register。
    can_frame_t pending_frame;                                // pending_frame 保存暂时没能发送出去的完整 CAN 帧。
    uint8_t pending_is_reset;                                 // 标记待重试帧的类型：0 表示故障报告帧，1 表示故障清除帧
    uint8_t pending;                                          // 是否有待重试的帧：0 表示无待发帧，1 表示 pending_frame 中有待重发报文
    uint8_t active;                                           // 是否有已成功报告、尚未清除的故障：0 表示没有，1 表示有
    uint16_t active_error_code;                               // 记录最近一次成功发送、仍处于活动状态的故障的错误码（EMCY 错误代码），2字节内容
    uint8_t active_error_register;                            // 记录该活动故障对应的Error Register 位，1字节内容
    uint8_t active_manufacturer[CO_EMCY_MANUFACTURER_LENGTH]; // 记录该活动故障的厂商自定义错误字段，5字节内容
} co_emcy_t;

co_status_t co_emcy_init(co_emcy_t *emcy, co_context_t *node,
                         co_device_od_t *device);

/* Report one active event. A nonzero error code is required. */
co_status_t co_emcy_report(co_emcy_t *emcy, uint16_t error_code,
                           uint8_t error_register,
                           const uint8_t manufacturer[CO_EMCY_MANUFACTURER_LENGTH]);

/* Emit the CANopen error-reset frame and clear 0x1001. */
co_status_t co_emcy_clear(co_emcy_t *emcy);

/* Retry a frame retained after a busy or failed transport submission. */
co_status_t co_emcy_process(co_emcy_t *emcy);

#endif
