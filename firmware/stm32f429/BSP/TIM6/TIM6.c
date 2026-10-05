#include "./TIM6/TIM6.h"

/* TIM6 句柄（非 static，供 it.c extern 引用） */
TIM_HandleTypeDef htim6;

/**
 * @brief  TIM6 基本定时器初始化 — 500ms 周期中断
 */
void TIM6_Init(void)
{
    /* 1. 使能 TIM6 时钟 */
    TIM6_CLK_ENABLE();

    /* 2. 配置基本定时器 */
    htim6.Instance         = TIM6;
    htim6.Init.Prescaler   = TIM6_PSC;
    htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim6.Init.Period      = TIM6_ARR;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&htim6);

    /* 3. 配置 NVIC 中断 */
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 1, 0);  /* 抢占优先级 1 */
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);

    /* 4. 启动定时器中断 */
    HAL_TIM_Base_Start_IT(&htim6);
}

/**
 * @brief  HAL 定时器"周期到达"回调（所有 TIM 共用）
 *         在 TIM6_DAC_IRQHandler 内被 HAL_TIM_IRQHandler 调用
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6)
    {
        /* TIM6: 500ms 周期，用户代码 */
    }
    else if (htim->Instance == TIM7)
    {
        HAL_IncTick();  /* TIM7: 1ms HAL 时基，替代 SysTick */
    }
}
