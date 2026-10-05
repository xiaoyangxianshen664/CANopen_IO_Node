#ifndef __USART_H
#define __USART_H

#include "stm32f4xx.h"

/* USART1: PA9=TX, PA10=RX */
#define USART1_TX_PIN GPIO_PIN_9
#define USART1_RX_PIN GPIO_PIN_10
#define USART1_GPIO_PORT GPIOA
#define USART1_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
#define USART1_CLK_ENABLE() __HAL_RCC_USART1_CLK_ENABLE()

void Usart1_Init(uint32_t baudrate);

extern UART_HandleTypeDef huart1;

/* ── DMA 句柄 ── */
extern DMA_HandleTypeDef hdma_usart1_tx;
extern DMA_HandleTypeDef hdma_usart1_rx;

/* 中断接收缓冲区（ISR 填，main 读） */
extern uint8_t usart1_rx_buf[50];
extern uint8_t usart1_rx_len;
extern volatile uint8_t usart1_rx_flag;

/* ── DMA 接收缓冲区 ── */
#define DMA_RX_BUF_SIZE  128
extern uint8_t dma_rx_buf[DMA_RX_BUF_SIZE];
extern volatile uint8_t dma_rx_flag;
extern uint16_t dma_rx_len;

void Usart1_DMA_Init(void);
void Usart_SendString(uint8_t *str);
void Usart_SendString_DMA(uint8_t *str, uint16_t len);

#endif

/*
 * ========== F1标准库 vs F4 HAL库 — UART/USART 主要变化 ==========
 *
 * 1. 多了 Handle（句柄）
 *    F1: 无句柄，所有参数直接传给函数
 *    F4: 必须定义 UART_HandleTypeDef huart（全局或 static），所有操作都通过它
 *        句柄像"身份证"，记录了这个串口的所有状态
 *
 * 2. 初始化方式不同
 *    // F1 标准库
 *    USART_InitTypeDef cfg;
 *    USART_Init(USART1, &cfg);
 *
 *    // F4 HAL库
 *    huart.Instance        = USART1;        // ← 必须设！
 *    huart.Init.BaudRate   = 115200;
 *    huart.Init.WordLength = UART_WORDLENGTH_8B;
 *    huart.Init.StopBits   = UART_STOPBITS_1;
 *    huart.Init.Parity     = UART_PARITY_NONE;
 *    huart.Init.Mode       = UART_MODE_TX_RX;
 *    HAL_UART_Init(&huart);
 *
 * 3. 发送/接收：标准库逐字节，HAL 可以整包
 *    // F1 标准库：自己写循环一个个发
 *    for (i=0; i<len; i++) {
 *        USART_SendData(USART1, buf[i]);
 *        while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
 *    }
 *
 *    // F4 HAL库：一行搞定
 *    HAL_UART_Transmit(&huart, buf, len, timeout);
 *    阻塞发送，timeout 防止卡死
 *
 * 4. 中断模式更统一
 *    // F1: 手动 USART_ITConfig() 开中断 + ISR 里查标志位判断是收还是发
 *    // F4:
 *    HAL_UART_Receive_IT(&huart, &ch, 1);  // 启动中断接收
 *    // 接收完成后自动调用 HAL_UART_RxCpltCallback(&huart) ← 重写这个就行
 *
 * 5. printf 重定向：写法一样
 *    F1/F4 都是在 usart.c 里重写 fputc() / __io_putchar()，没区别
 *
 * 当前工程: USART1, PA9(TX) / PA10(RX)
 *
 * 踩坑: F4 HAL 的 UART_HandleTypeDef 必须是全局/static，
 *       不能用局部变量（HAL 内部会引用它的地址）
 */
