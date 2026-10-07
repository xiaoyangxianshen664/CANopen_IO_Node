#ifndef __BSP_CAN_H
#define __BSP_CAN_H

#include "stm32f4xx_hal.h"
#include "co_types.h"

/* CANopen 节点使用 CAN1，PB8/PB9 采用 AF9 复用。 */
#define CANx                 CAN1
#define CAN_RX_IRQ           CAN1_RX0_IRQn
#define CAN_RX_PIN           GPIO_PIN_8
#define CAN_TX_PIN           GPIO_PIN_9
#define CAN_GPIO_PORT        GPIOB
#define CAN_AF_PORT          GPIO_AF9_CAN1
#define CAN_NODE_BITRATE     500000UL

/* CAN 外设句柄，由 BSP 初始化并供中断入口使用。 */
extern CAN_HandleTypeDef Can_Handle;

/* CAN 传输层运行统计，由诊断任务读取。 */
extern volatile uint32_t can_rx_overflow_count;
extern volatile uint32_t can_tx_busy_count;
extern volatile uint32_t can_error_count;
extern volatile uint32_t can_bus_off_count;
extern volatile uint32_t can_rx_frame_count;

/**
 * @brief 初始化 CAN1 GPIO、位时序、过滤器和接收中断。
 * @param 无。
 * @return 无。
 * @note APB1=45 MHz 时，Prescaler=6、BS1=11TQ、BS2=3TQ 对应 500 kbit/s。
 * @example CAN_Config();
 */
void CAN_Config(void);

/**
 * @brief 将协议层标准 CAN 帧提交到 CAN 发送邮箱。
 * @param user 传输层私有参数，当前未使用。
 * @param frame 协议层待发送帧，只读取其内容。
 * @return CO_OK、CO_ERR_TX_BUSY 或具体参数/发送错误。
 * @note 返回 CO_OK 只表示已经提交到邮箱，不表示总线已收到 ACK。
 * @example CAN_SendFrame(NULL, &frame);
 */
co_status_t CAN_SendFrame(void *user, const can_frame_t *frame);

/**
 * @brief 从 CAN 接收环形队列取出一帧协议帧。
 * @param frame 输出接收帧的地址。
 * @return 1 表示取到一帧，0 表示参数无效或队列为空。
 * @example if (CAN_ReceiveFrame(&frame)) { 处理接收帧; }
 */
uint8_t CAN_ReceiveFrame(can_frame_t *frame);

#endif
