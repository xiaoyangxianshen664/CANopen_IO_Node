#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "./SysTick/SysTick.h"
#include "./TIM7/TIM7.h"
#include "./Usart/Usart.h"
#include "./CAN/bsp_can.h"

/* ARM_CM4F 端口实现了该函数，但旧版头文件没有对外声明。 */
extern void xPortSysTickHandler(void);

extern DMA_HandleTypeDef hdma_usart1_tx;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_adc;

/******************************************************************************/
/*           Cortex-M4 系统异常处理                                           */
/******************************************************************************/

/**
 * @brief  SysTick 中断 �?FreeRTOS 调度 + TimingDelay 延时
 */
void SysTick_Handler(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        xPortSysTickHandler();      /* FreeRTOS: 任务调度、vTaskDelay 时基 */
    }
    SysTick_ISR_Handler();          /* TimingDelay �?1（Delay_us/Delay_ms 仍可用） */
}

/**
 * @brief  HardFault —�?调试时在这里打断点，快速定位问�? */
void HardFault_Handler(void)
{
    while (1)
    {
    }
}

/******************************************************************************/
/* 其余系统异常（NMI/MemManage/BusFault/UsageFault/SVC/PendSV/...�?          */
/* 启动文件中已�?WEAK 默认实现（B . 死循环），不需要在此重复�?             */
/* 用到外设中断时，在此文件中追加对应的 xxx_IRQHandler 即可�?               */
/******************************************************************************/

/******************************************************************************/
/*                          GPIO 外部中断服务函数                              */
/******************************************************************************/

#include "./LED/LED.h"

/**
 * @brief  KEY1 (PA0) 外部中断 —�?红灯翻转
 */
void EXTI0_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_0) != RESET)
    {
        /* 软件消抖（ISR 内不能用 HAL_Delay�?*/
        for (volatile uint32_t i = 0; i < 200000; i++)
            ;

        if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_0) == RESET)
            return; // 抖动，直接退�?
        if (HAL_GPIO_ReadPin(LED_R_GPIO_PORT, LED_R_PIN) == GPIO_PIN_RESET)
            LED_R(1);
        else
            LED_R(0);
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0);
    }
}

/**
 * @brief  KEY2 (PC13) 外部中断 —�?绿灯翻转
 */
void EXTI15_10_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_13) != RESET)
    {
        /* 软件消抖（ISR 内不能用 HAL_Delay�?*/
        for (volatile uint32_t i = 0; i < 200000; i++)
            ;

        if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_13) == RESET)
            return; // 抖动，直接退�?
        if (HAL_GPIO_ReadPin(LED_G_GPIO_PORT, LED_G_PIN) == GPIO_PIN_RESET)
            LED_G(1);
        else
            LED_G(0);

        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_13);
    }
}

/******************************************************************************/
/*                          UART 串口中断服务函数                               */
/******************************************************************************/

/* 接收缓冲区（extern �?BSP/Usart/Usart.c�?*/
extern uint8_t usart1_rx_buf[50];
extern uint8_t usart1_rx_len;
extern volatile uint8_t usart1_rx_flag;

extern uint8_t dma_rx_buf[128];
extern volatile uint8_t dma_rx_flag;
extern uint16_t dma_rx_len;

extern UART_HandleTypeDef huart1;

/**
 * @brief  USART1 中断 —�?RXNE + IDLE
 */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);

    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE) != RESET)
    {
        __HAL_UART_CLEAR_IDLEFLAG(&huart1); // 清硬�?IDLE 标志

        /* 判断 DMA 模式还是 IT 模式 */
        uint16_t ndtr = __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);
        if (ndtr < DMA_RX_BUF_SIZE)
        {
            dma_rx_len = DMA_RX_BUF_SIZE - ndtr;
            if (dma_rx_len > 0)
                dma_rx_flag = 1;
            HAL_UART_DMAStop(&huart1);
            HAL_UART_Receive_DMA(&huart1, dma_rx_buf, DMA_RX_BUF_SIZE);
            __HAL_UART_CLEAR_IDLEFLAG(&huart1);
        }
        else
        {
            usart1_rx_flag = 1;
        }
    }
}

/* ── DMA 中断服务 ── */
void DMA2_Stream7_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart1_tx);
}

void DMA2_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart1_rx);
}

/**
 * @brief ADC1 DMA2 Stream0 中断服务函数。
 * @note 该入口负责驱动 ADC 半缓冲和满缓冲回调，任务层再读取稳定采样窗口。
 */
void DMA2_Stream0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_adc);
}

/******************************************************************************/
/*                          TIM 定时器中断服务函�?                             */
/******************************************************************************/

/* TIM6 句柄（extern �?BSP/TIM6/TIM6.c�?*/
extern TIM_HandleTypeDef htim6;

/**
 * @brief  TIM6 基本定时器中�?—�?周期 500ms
 *         调用 HAL_TIM_IRQHandler �?内部会调 HAL_TIM_PeriodElapsedCallback
 */
void TIM6_DAC_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim6);
}

/* ── TIM7 HAL 时基中断 —�?HAL_IncTick ── */
extern TIM_HandleTypeDef htim7;

void TIM7_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim7);
}

/* CAN FIFO0 接收中断和状态变化中断统一放在本文件。 */
void CAN1_RX0_IRQHandler(void)
{
    HAL_CAN_IRQHandler(&Can_Handle);
}

void CAN1_SCE_IRQHandler(void)
{
    HAL_CAN_IRQHandler(&Can_Handle);
}
