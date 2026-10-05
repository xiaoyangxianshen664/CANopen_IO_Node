#ifndef __SYSTICK_H
#define __SYSTICK_H

#include "stm32f4xx.h"
static void SystemClock_Config(void);
/* SysTick 初始化 — 配置为 10us 中断一次 */
void SysTick_Init(void);

/* 微秒级延时 — 参数为 10us 的倍数（Delay_us(1) = 10us） */
void Delay_us(__IO uint32_t nTime);

/* 毫秒级延时 — 宏展开为 Delay_us(100*x)，100 × 10us = 1ms */
#define Delay_ms(x) Delay_us(100*x)

/* SysTick 中断统一入口 — 在 stm32f4xx_it.c 的 SysTick_Handler 中调用 */
void SysTick_ISR_Handler(void);

#endif

/*
 * ========== SysTick 延时原理 ==========
 *
 * 1. 硬件层
 *    SysTick 是 Cortex-M4 内核内置的 24 位递减定时器，LOAD 寄存器决定重装载值。
 *    每个 HCLK 周期 VAL 自动减 1，减到 0 触发中断并硬件自动重装。
 *    上电默认关闭（CTRL=0），必须软件使能。
 *
 * 2. 配置层
 *    SysTick_Init() 调用 HAL_SYSTICK_Config(SystemCoreClock / 100000)
 *    F429@180MHz: LOAD = 1800 → 中断周期 = (1800+1)/180M ≈ 10us
 *
 * 3. 中断服务层
 *    SysTick_ISR_Handler() 做两件事：
 *    - 每次中断调用 TimingDelay_Decrement()，给 Delay_us 用（10us 精度）
 *    - 每 100 次中断调用一次 HAL_IncTick()，保持 uwTick 仍按 1ms 递增
 *      这样 HAL_Delay() 不受影响
 *
 * 4. 应用层
 *    Delay_us(n)：设 TimingDelay=n，死等中断来减到 0
 *    Delay_ms(n)：展开为 Delay_us(100*n)，100 × 10us = 1ms
 *
 *    时间换算：1s = 1000ms = 1,000,000us，都是 1000 进制
 */
