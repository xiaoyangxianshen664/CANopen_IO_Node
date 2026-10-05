#include "stm32f4xx_hal.h"
#include <stdio.h>
#include "./LED/LED.h"
#include "./Key/Key.h"
#include "./Exti/Exti.h"
#include "./Usart/Usart.h"
#include "./TIM6/TIM6.h"
#include "./TIM7/TIM7.h"
#include "./SysTick/SysTick.h"
#include "./ADC/ADC_Multi.h"
#include "bsp_can.h"
#include "./freertos_demo.h"

int main(void)
{
    /* 硬件初始化必须在调度器启动前完成。 */
    HAL_Init();
    SysTick_Init();
    TIM7_Init();                              /* TIM7：HAL 1ms 时基。 */
    LED_Init();
    Key_Init();
    ADC_Multi_Init();                         /* ADC1 双通道 DMA：PC3、PA4。 */
    Usart1_Init(115200);
    Usart1_DMA_Init();
    TIM6_Init();
    CAN_Config();

    HAL_UART_AbortReceive(&huart1);
    HAL_UART_Receive_DMA(&huart1, dma_rx_buf, DMA_RX_BUF_SIZE);
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);

    printf("FreeRTOS Start!\n");

    /* 任务创建和调度器启动统一由 freertos_demo.c 管理。 */
    freertos_demo();

    /* freertos_demo() 正常情况下不会返回。 */
    for (;;) {}
}

