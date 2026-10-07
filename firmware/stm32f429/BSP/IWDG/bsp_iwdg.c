#include "bsp_iwdg.h"

static IWDG_HandleTypeDef iwdg_handle;
/*STM32F4 的独立看门狗 IWDG 时钟通常来自 LSI ≈ 32 kHz。(1500+1)×64​ /32000=3.002s必须在大约 3 秒以内成功执行一次，否则 IWDG 会复位 MCU。*/
uint8_t BSP_IWDG_Init(void)
{
    iwdg_handle.Instance = IWDG;
    iwdg_handle.Init.Prescaler = IWDG_PRESCALER_64;
    iwdg_handle.Init.Reload = 1500u;

    return (uint8_t)(HAL_IWDG_Init(&iwdg_handle) == HAL_OK);
}

uint8_t BSP_IWDG_Refresh(void)
{
    return (uint8_t)(HAL_IWDG_Refresh(&iwdg_handle) == HAL_OK);
}
