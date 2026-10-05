#ifndef __TIM6_H
#define __TIM6_H

#include "stm32f4xx.h"



#define TIM6_PSC            8999        /* 预分频器：90MHz/(8999+1) = 10kHz */
#define TIM6_ARR            4999        /* 自动重装：(4999+1)/10kHz = 0.5s */
#define TIM6_CLK_ENABLE()   __HAL_RCC_TIM6_CLK_ENABLE()

void TIM6_Init(void);

#endif


/*
 * ========== F1标准库 vs F4 HAL库 — 定时器 TIM 主要变化 ==========
 *
 * 1. 中断向量名变了（大坑！）
 *    // F1 标准库
 *    #define BASIC_TIM_IRQn    TIM6_IRQn
 *
 *    // F4 HAL库（TIM6 和 DAC 共用一个中断向量！）
 *    #define BASIC_TIM_IRQn    TIM6_DAC_IRQn       // ← 注意！不是 TIM6_IRQn
 *
 * 2. 定时器时钟计算变了（F4 有自动 x2 规则）
 *    F1:  TIMxCLK = PCLK1（直通）
 *    F4:  TIMxCLK = PCLK1 × 2（当 APB1 预分频 ≠ 1 时）
 *         如果 APB1 预分频 = 1，则不乘 2（直通）
 *
 *    当前工程: SYS_CLK=180MHz, APB1=180/4=45MHz, TIM6_CLK=45×2=90MHz
 *    周期：T = (PSC+1) × (ARR+1) / TIM6_CLK = 9000×5000/90MHz = 0.5s
 *
 * 3. 多了 Handle（句柄）
 *    F1: 无句柄，TIM_TimeBaseInit(TIM6, &cfg) 直接传
 *    F4: 必须定义 TIM_HandleTypeDef htim6（全局或 static）
 *        初始化: HAL_TIM_Base_Init(&htim6)
 *        启动:   HAL_TIM_Base_Start_IT(&htim6)
 *
 * 4. API 风格完全不同
 *    操作          F1 标准库                          F4 HAL库
 *    初始化        TIM_TimeBaseInit(TIM6, &cfg)       HAL_TIM_Base_Init(&htim6)  + 需设 .Instance
 *    开启          TIM_Cmd(TIM6, ENABLE)              HAL_TIM_Base_Start_IT(&htim6)
 *    停止          TIM_Cmd(TIM6, DISABLE)             HAL_TIM_Base_Stop_IT(&htim6)
 *    NVIC          NVIC_Init(&cfg)                    HAL_NVIC_SetPriority() + HAL_NVIC_EnableIRQ()
 *    中断回调      无（ISR 里手动写）                   HAL_TIM_PeriodElapsedCallback(&htim6) 重写即可
 *
 * 5. TIM6 是基本定时器，无 GPIO 引脚
 *    不像通用定时器，TIM6/7 只有时基和中断，不能输出 PWM 和编码器
 *    F4 上 TIM6 挂 APB1，时钟计算注意 x2 规则
 *
 * 当前工程: TIM6, 周期 500ms, 纯定时中断, 无 GPIO 输出
 *
 * 踩坑清单:
 *   - 中断向量名: F4 用 TIM6_DAC_IRQn，不是 TIM6_IRQn
 *   - 时钟 x2: APB1≠1 时自动 x2，算错 ARR/PSC 会导致周期不对
 *   - Handle 必须全局/static，放栈上会野指针
 *   - ISR 里调 HAL_TIM_IRQHandler(&htim6)，它会自动调 PeriodElapsedCallback
 */

/* TIM6 基本定时器配置 — 周期 500ms，无 GPIO 输出引脚 */
/* SYS_CLK = 180MHz, APB1 = 45MHz, TIM6_CLK = 2×APB1 = 90MHz           */
/* 周期公式: T = (PSC+1) × (ARR+1) / TIM6_CLK = 9000×5000/90MHz = 0.5s */

