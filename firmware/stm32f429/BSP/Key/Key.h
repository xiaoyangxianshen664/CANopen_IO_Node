#ifndef __KEY_H
#define __KEY_H

#include "stm32f4xx.h"



/* 按键引脚定义 */
#define KEY1_PIN GPIO_PIN_0
#define KEY1_GPIO_PORT GPIOA
#define KEY1_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()

#define KEY2_PIN GPIO_PIN_13
#define KEY2_GPIO_PORT GPIOC
#define KEY2_GPIO_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()

// 按键默认下拉，默认（未按下）板子有外部下拉电阻 ：IO 口读到 低电平 (0)
/* 按键按下为高电平 */

#define KEY_ON 1
#define KEY_OFF 0

void Key_Init(void);
uint8_t Key_Scan(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin); // 按键扫描

#endif

/*
 * ========== F1标准库 vs F4 HAL库 — GPIO 输入（按键）==========
 *
 * 1. 上下拉方式变了
 *    F1: 模式枚举里自带上下拉，如 GPIO_Mode_IPU（输入上拉）、GPIO_Mode_IPD（输入下拉）
 *    F4: Mode 统一用 GPIO_MODE_INPUT，上下拉单独通过 Pull 配置：
 *        GPIO_PULLUP  / GPIO_NOPULL  / GPIO_PULLDOWN
 *        在 HAL_GPIO_Init() 结构体里设 Pull 成员
 *
 * 2. 读引脚
 *    F1: uint8_t val = GPIO_ReadInputDataBit(GPIOx, GPIO_Pin_x);
 *    F4: GPIO_PinState val = HAL_GPIO_ReadPin(GPIOx, GPIO_Pin_x);
 *        返回值是 GPIO_PIN_SET(1) / GPIO_PIN_RESET(0)，不是 0/1
 *
 * 3. 去抖方式
 *    F1: 同 F4，ISR 里不能用 HAL_Delay，可用 for(volatile ...) 或定时器消抖
 *    F4: 同，额外注意 HAL_Delay 依赖 SysTick，ISR 里卡死
 *
 * 当前工程: KEY1(PA0) / KEY2(PC13)，默认外部下拉，按下为高电平
 * 板子有外部下拉电阻，读引脚读到低电平 = 未按下
 *
 * 踩坑提醒: ISR 内消抖别调 HAL_Delay()，用 for(volatile ...) 或硬件定时器
 */
 


