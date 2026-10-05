#ifndef __EXTI_H
#define __EXTI_H

#include "stm32f4xx.h"



void Exti_Key_Init(void);

#endif

/*
 * ========== F1标准库 vs F4 HAL库 — EXTI 外部中断 主要变化 ==========
 *
 * 1. 配置流程完全不同（最大变化！）
 *    F1 两步走:
 *       ① GPIO_EXTILineConfig(GPIO_PortSourceGPIOx, GPIO_PinSourcex)
 *          把 GPIO 引脚连接到 EXTI 线
 *       ② EXTI_Init(&EXTI_InitStructure)
 *          配置 EXTI 线号、触发模式、使能
 *
 *    F4 一步完成:
 *       HAL_GPIO_Init() 时把 Mode 设为 GPIO_MODE_IT_RISING（或 FALLING / RISING_FALLING）
 *       GPIO 和 EXTI 绑在一起配了，不需要单独的 EXTI_Init()
 *
 * 2. 触发模式写在 GPIO Init 里
 *    F1: EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising
 *    F4: init.Mode = GPIO_MODE_IT_RISING
 *
 * 3. 中断标志操作
 *    F1: EXTI_GetITStatus(EXTI_Line0)  /  EXTI_ClearITPendingBit(EXTI_Line0)
 *    F4: __HAL_GPIO_EXTI_GET_IT(GPIO_PIN_0)  /  __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0)
 *        变成了宏，传 GPIO_PIN 而不是 EXTI_Line
 *
 * 4. NVIC 配置简化
 *    F1: NVIC_InitTypeDef + NVIC_Init(&nvic)  结构体赋值再调函数
 *    F4: HAL_NVIC_SetPriority(IRQn, 抢占, 子优先) + HAL_NVIC_EnableIRQ(IRQn)
 *        两行搞定
 *
 * 5. HAL 多了回调机制（可选使用）
 *    F1: ISR 里手动判断哪个 EXTI 线触发的
 *    F4: 可重写 HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
 *        同一个回调处理所有 EXTI 引脚，通过 GPIO_Pin 参数区分
 *
 * 当前工程：
 *   KEY1(PA0)  → EXTI0_IRQn → 上升沿触发 → 翻转红灯
 *   KEY2(PC13) → EXTI15_10_IRQn → 上升沿触发 → 翻转绿灯
 *   NVIC Priority Group 4（仅抢占优先级 0~15）
 *
 * 踩坑：
 *   - PA0→EXTI0 不共用 / PC13→EXTI15_10 共用（跟其他 PCx 共享一个 ISR）
 *   - ISR 里先读中断标志判断是不是自己，干活后末尾清标志
 *   - ISR 内别调 HAL_Delay()
 */


