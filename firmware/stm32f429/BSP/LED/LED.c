#include "./LED/LED.h"

void LED_Init(void)
{
    LED_R_GPIO_CLK_ENABLE();

    /* 先写高电平，再配置为输出——防止初始化瞬间灯闪亮 */
    LED_R(1);
    LED_G(1);
    LED_B(1);
		/* LED 控制宏：LED_R(0) 亮, LED_R(1) 灭 */

    GPIO_InitTypeDef LED_InitStruct = {0};
    LED_InitStruct.Pin   = LED_R_PIN | LED_G_PIN | LED_B_PIN;
    LED_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    LED_InitStruct.Pull  = GPIO_NOPULL;
    LED_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_R_GPIO_PORT, &LED_InitStruct);
}
