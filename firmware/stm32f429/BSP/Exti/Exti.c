#include "./Exti/Exti.h"
#include "./Key/Key.h"

/* 1. GPIO 引脚 — 设为中断模式
   2. NVIC — 使能中断通道 + 设优先级
   3. stm32f4xx_it.c — 写中断服务函数
   4. 中断服务函数声明 — 放在头文件或 stm32f4xx_it.h
   */

void Exti_Key_Init(void)
{
    KEY1_GPIO_CLK_ENABLE();
    KEY2_GPIO_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.Pin   = KEY1_PIN;
    GPIO_InitStructure.Mode  = GPIO_MODE_IT_RISING; // 上升沿触发中断
    GPIO_InitStructure.Pull  = GPIO_NOPULL;         // 不上拉不下拉
    HAL_GPIO_Init(KEY1_GPIO_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Pin = KEY2_PIN;
    HAL_GPIO_Init(KEY2_GPIO_PORT, &GPIO_InitStructure);

    /* KEY1 (PA0) 中断优先级 & 使能 */
    HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);

    /* KEY2 (PC13) 中断优先级 & 使能 — 走 EXTI15_10 共用通道 */
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/*
HAL_Init() 里默认设的是 NVIC_PRIORITYGROUP_4：
抢占优先级 4 bit  →  0 ~ 15
子优先级   0 bit  →  始终为 0

两个中断优先级全是 0，确实不会互相打断。但硬件内部有固定的自然优先级作为平局裁决：
中断	IRQ 号	自然优先级
EXTI0	6	更高
EXTI15_10	40	更低
如果两个按键同一时钟周期触发，CPU 先响应 IRQ 号小的 EXTI0，EXTI15_10 排队等着。
*/
