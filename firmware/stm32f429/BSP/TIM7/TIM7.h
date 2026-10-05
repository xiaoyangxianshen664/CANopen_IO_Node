#ifndef __TIM7_H
#define __TIM7_H

#include "stm32f4xx.h"

/* TIM7 预分频：90MHz / 9000 = 10KHz */
#define TIM7_PSC  9000-1
/* 自动重装：10KHz / 10 = 1000Hz = 1ms */
#define TIM7_ARR  10-1

void TIM7_Init(void);

#endif

/*
 * TIM7 基本定时器 — 替代 SysTick 给 HAL 当 1ms 时基
 *
 * FreeRTOS 接管 SysTick 后，HAL_Delay() 需要另一个定时器来维持 uwTick。
 * TIM7 跟 TIM6 一样是基本定时器，只有 Prescaler + Period，不占 IO。
 *
 * 时钟：TIM7 挂 APB1，APB1=45MHz，预分频≠1 所以 TIM7_CLK=2×45M=90MHz
 * 周期：T = (PSC+1)×(ARR+1)/90M = 9000×10/90M = 1ms
 */
