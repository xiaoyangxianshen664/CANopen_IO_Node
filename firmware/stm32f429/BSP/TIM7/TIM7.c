#include "./TIM7/TIM7.h"

TIM_HandleTypeDef htim7;

/**
 * @brief  TIM7 初始化 — 1ms 周期，替代 SysTick 给 HAL 当时基
 * @note   中断里只调 HAL_IncTick()，不干别的
 */
void TIM7_Init(void)
{
    __HAL_RCC_TIM7_CLK_ENABLE();

    htim7.Instance         = TIM7;
    htim7.Init.Prescaler   = TIM7_PSC;                      /* 9000-1 → 10KHz 计数      */
    htim7.Init.Period      = TIM7_ARR;                      /* 10-1 → 1000Hz = 1ms 中断  */
    htim7.Init.CounterMode = TIM_COUNTERMODE_UP;
    HAL_TIM_Base_Init(&htim7);

    HAL_NVIC_SetPriority(TIM7_IRQn, 15, 0);                 /* 最低优先级（仅喂 HAL，不急） */
    HAL_NVIC_EnableIRQ(TIM7_IRQn);

    HAL_TIM_Base_Start_IT(&htim7);
}
