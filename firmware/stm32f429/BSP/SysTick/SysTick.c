#include "./SysTick/SysTick.h"

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 25;
    osc.PLL.PLLN       = 360;
    osc.PLL.PLLP       = RCC_PLLP_DIV2;
    osc.PLL.PLLQ       = 7;
    HAL_RCC_OscConfig(&osc);

    clk.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5);
}

/* 野火风格延时计数器 — 每次中断减 1，每 10us 减一次 */
static __IO uint32_t TimingDelay;

/**
  * @brief  配置 SysTick 为 10us 中断一次
  * @note   必须在 HAL_Init() 之后调用，因为 HAL_Init 已配好 1ms，
  *         这里覆盖为 10us 以获得更高精度
  *         LOAD = 180MHz / 100000 = 1800 → (1800+1)/180M ≈ 10us
  */
void SysTick_Init(void)
{
    /* 先切换到工程要求的 180 MHz 系统时钟 */
    SystemClock_Config();

    if (HAL_SYSTICK_Config(SystemCoreClock / 100000))
    {
        /* 装载值超 24 位上限（0xFFFFFF），理论上 180M/100000=1800 远小于上限，不会进这里 */
        while (1);
    }
    TimingDelay = 0;
}

/**
  * @brief  微秒级延时（参数为 10us 的倍数）
  * @param  nTime: Delay_us(1) = 10us, Delay_us(100) = 1ms
  */
void Delay_us(__IO uint32_t nTime)
{
    TimingDelay = nTime;
    while (TimingDelay != 0);
}

/**
  * @brief  SysTick 中断统一入口
  * @note   在 stm32f4xx_it.c 的 SysTick_Handler() 中调用，替代原 HAL_IncTick()
  *         - 每次中断：TimingDelay 减 1（10us 精度延时）
  *         - 每 100 次中断：调 HAL_IncTick()（保持 uwTick 仍按 1ms 递增）
  */
void SysTick_ISR_Handler(void)
{
    /* 每次 10us 中断：野火风格延时计数器减 1 */
    if (TimingDelay != 0x00)
    {
        TimingDelay--;
    }
    /* HAL_IncTick 已搬到 TIM7，这里不再调用 */
}
