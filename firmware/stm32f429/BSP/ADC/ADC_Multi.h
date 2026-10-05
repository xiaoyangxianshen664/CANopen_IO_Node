#ifndef __ADC_MULTI_H
#define __ADC_MULTI_H

#include "stm32f4xx_hal.h"

/* ADC1 两路模拟输入：PC3 电位器、PA4 温度传感器。 */
#define ADCx ADC1
#define ADCx_CLK_ENABLE() __HAL_RCC_ADC1_CLK_ENABLE()
#define ADC_TRIGGER_TIM TIM3
#define ADC_TRIGGER_TIM_CLK() __HAL_RCC_TIM3_CLK_ENABLE()
#define ADC_TRIGGER_TIM_PSC 8999U
#define ADC_TRIGGER_TIM_ARR 9U
#define ADC_CH_COUNT 2U                                          // 扫描通道数量: CH1 + CH2
#define ADC_SAMPLES_PER_BATCH 32U                                // 每个半区存放的完整采样组数
#define ADC_HALF_BUF_SIZE (ADC_CH_COUNT * ADC_SAMPLES_PER_BATCH) // 半区元素数 = 2 * 32 = 64

#define ADC_CH1_GPIO_CLK() __HAL_RCC_GPIOC_CLK_ENABLE()
#define ADC_CH1_PORT GPIOC
#define ADC_CH1_PIN GPIO_PIN_3
#define ADC_CH1_CHANNEL ADC_CHANNEL_13

#define ADC_CH2_GPIO_CLK() __HAL_RCC_GPIOA_CLK_ENABLE()
#define ADC_CH2_PORT GPIOA
#define ADC_CH2_PIN GPIO_PIN_4
#define ADC_CH2_CHANNEL ADC_CHANNEL_4

/* ADC1 使用 DMA2 Stream0 Channel0 循环搬运。 */
#define ADC_DMA_STREAM DMA2_Stream0
#define ADC_DMA_CHANNEL DMA_CHANNEL_0
#define ADC_DMA_CLK_ENABLE() __HAL_RCC_DMA2_CLK_ENABLE()
#define ADC_BUF_SIZE (2U * ADC_HALF_BUF_SIZE) // 整缓冲区元素数 = 128

/* DMA 通知位: bit0 表示前半区已填满，bit1 表示后半区已填满。 */
#define ADC_READY_FIRST_HALF (1UL << 0)  // 前半区(索引 0   ~ 63) 可读
#define ADC_READY_SECOND_HALF (1UL << 1) // 后半区(索引 64  ~ 127) 可读

extern uint16_t adc_buf[ADC_BUF_SIZE]; // 供其他模块(如处理任务)访问采样缓冲区

/* DMA 半传输/全传输中断的应用层通知钩子。 */
void adc_dma_notify_from_isr(uint32_t area_flag);

/**
 * @brief 初始化 ADC1 双通道扫描和循环 DMA。
 * @param 无。
 * @return 无。
 * @note DMA 每个半区保存两路各 ADC_SAMPLES_PER_BATCH 个原始样本。
 */
void ADC_Multi_Init(void);

/**
 * @brief 读取指定 DMA 半区的两路 ADC 平均值。
 * @param half [输入] 0 表示前半区，1 表示后半区。
 * @param ai1 [输出] PC3 电位器平均值，范围 0~4095。
 * @param ai2 [输出] PA4 温度传感器平均值，范围 0~4095。
 * @return 1 表示读取成功，0 表示参数无效。
 * @note 调用者只能选择已经由 DMA 回调通知完成的半区。
 */
uint8_t ADC_Multi_ReadAverage(uint8_t half, uint16_t *ai1, uint16_t *ai2);

#endif
