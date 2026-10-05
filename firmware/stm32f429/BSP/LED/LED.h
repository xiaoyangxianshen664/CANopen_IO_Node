#ifndef __LED_H
#define __LED_H

#include "stm32f4xx.h"


#define LED_R_PIN GPIO_PIN_10
#define LED_R_GPIO_PORT GPIOH
#define LED_R_GPIO_CLK_ENABLE() __HAL_RCC_GPIOH_CLK_ENABLE()

#define LED_G_PIN GPIO_PIN_11
#define LED_G_GPIO_PORT GPIOH
#define LED_G_GPIO_CLK_ENABLE() __HAL_RCC_GPIOH_CLK_ENABLE()

#define LED_B_PIN GPIO_PIN_12
#define LED_B_GPIO_PORT GPIOH
#define LED_B_GPIO_CLK_ENABLE() __HAL_RCC_GPIOH_CLK_ENABLE()

/* LED 控制宏：LED_R(0) 亮, LED_R(1) 灭 */
#define LED_R(a) HAL_GPIO_WritePin(LED_R_GPIO_PORT, LED_R_PIN, (GPIO_PinState)(a))
#define LED_G(a) HAL_GPIO_WritePin(LED_G_GPIO_PORT, LED_G_PIN, (GPIO_PinState)(a))
#define LED_B(a) HAL_GPIO_WritePin(LED_B_GPIO_PORT, LED_B_PIN, (GPIO_PinState)(a))

// 板子的推挽输出引脚，低电平亮，高电平灭 ，因此LED_R(0) 亮, LED_R(1) 灭
void LED_Init(void);

#endif

/*
 * ========== F1标准库 vs F4 HAL库 — GPIO 主要变化 ==========
 *
 * 1. Mode 取值变了
 *    F1 标准库: GPIO_Mode_Out_PP / GPIO_Mode_IPU
 *    F4 HAL库:  GPIO_MODE_OUTPUT_PP / GPIO_MODE_INPUT，取值完全不同
 *
 * 2. 多了 Pull（上下拉）配置
 *    F1: 上下拉并到 Mode 里（GPIO_Mode_IPU / GPIO_Mode_Out_PP）
 *    F4: Mode 只管输入/输出/复用，上下拉单独配 Pull（GPIO_NOPULL / GPIO_PULLUP）
 *
 * 3. 写引脚统一成三参数
 *    F1: GPIO_SetBits(GPIOx, GPIO_Pin_x)  /  GPIO_ResetBits(GPIOx, GPIO_Pin_x)
 *    F4: HAL_GPIO_WritePin(GPIOx, GPIO_Pin_x, GPIO_PIN_SET/RESET)
 *
 * 4. 多了内置翻转
 *    F1: 无内置，需读-取反-写
 *    F4: HAL_GPIO_TogglePin(GPIOx, GPIO_Pin_x)
 *
 * 5. 速度枚举名变了
 *    F1: GPIO_Speed_50MHz
 *    F4: GPIO_SPEED_FREQ_LOW / MEDIUM / HIGH / VERY_HIGH
 *
 * 6. F4 多了 AF（复用功能）
 *    普通推挽输出不需要；用作 TIM PWM/UART 等外设引脚时才设为 GPIO_MODE_AF_PP
 *    本工程仅 GPIO 输出点灯，暂不涉及 AF
 *
 * 当前工程 LED: PH10(R) / PH11(G) / PH12(B)，低电平亮，高电平灭
 */

/* LED 引脚定义 — 红(R) PH10, 绿(G) PH11, 蓝(B) PH12, 低电平亮 */
