#include "./Usart/Usart.h" // 自己的头文件，定义了 PA9=TX PA10=RX 等引脚宏
#include <stdio.h>         // 标准IO库，提供 FILE 类型和 printf 函数声明

UART_HandleTypeDef huart1; // USART1 的 HAL 句柄（全局变量），存串口所有配置，fputc 也要用它

/* 中断接收缓冲区（ISR 里往里填，main 里读走） */
uint8_t usart1_rx_buf[50];       /* 接收缓冲区 */
uint8_t usart1_rx_len;           /* 一帧收到的字节数 */
volatile uint8_t usart1_rx_flag; /* 帧完成标志：1=有新帧待处理 */

/*
 * ──── 单字节接收暂存（static，本文件内部用）────
 * HAL_Receive_IT 每收完 1 字节就把数据写到这里，然后进 RxCpltCallback
 */
static uint8_t rx_byte;





/* ── DMA 句柄 + 缓冲区 ── */
DMA_HandleTypeDef hdma_usart1_tx;
DMA_HandleTypeDef hdma_usart1_rx;
uint8_t  dma_rx_buf[DMA_RX_BUF_SIZE];
volatile uint8_t  dma_rx_flag;
uint16_t dma_rx_len;





void Usart1_Init(uint32_t baudrate)
{
    USART1_GPIO_CLK_ENABLE(); // ① 开启 GPIOA 时钟 —— 让 PA9/PA10 引脚通电
    USART1_CLK_ENABLE();      // ② 开启 USART1 外设时钟 —— 串口模块本身也要通电

    GPIO_InitTypeDef g = {0};       // 定义 GPIO 初始化结构体，{0} 清零所有字段
    g.Mode = GPIO_MODE_AF_PP;       // 模式：复用推挽输出 —— 引脚控制权交给 USART1
    g.Pull = GPIO_PULLUP;           // 上下拉：上拉 —— TX 空闲时保持高电平（串口协议要求）
    g.Speed = GPIO_SPEED_FREQ_HIGH; // 速度：高速 —— 对串口来说不重要但不影响
    g.Alternate = GPIO_AF7_USART1;  // 复用功能选 AF7 —— STM32F4 上 USART1 的复用编号

    g.Pin = USART1_TX_PIN;               // 选中 PA9 引脚
    HAL_GPIO_Init(USART1_GPIO_PORT, &g); // 初始化 PA9 为 USART1_TX

    g.Pin = USART1_RX_PIN;               // 选中 PA10（Alternate 字段仍是 AF7，不用改）
    HAL_GPIO_Init(USART1_GPIO_PORT, &g); // 初始化 PA10 为 USART1_RX

    huart1.Instance = USART1;                        // 指定硬件是 USART1
    huart1.Init.BaudRate = baudrate;                 // 波特率由函数参数传入（如 115200）
    huart1.Init.WordLength = UART_WORDLENGTH_8B;     // 数据位 8 位
    huart1.Init.StopBits = UART_STOPBITS_1;          // 停止位 1 位
    huart1.Init.Parity = UART_PARITY_NONE;           // 无校验位
    huart1.Init.Mode = UART_MODE_TX_RX;              // 收发双工模式
    huart1.Init.OverSampling = UART_OVERSAMPLING_16; // 过采样 16 倍（标准配置）
    HAL_UART_Init(&huart1);                          // 把上面配置写入 USART1 硬件寄存器

    /* ① NVIC 层面：告诉 CPU，USART1 中断能通到内核 */
    HAL_NVIC_SetPriority(USART1_IRQn, 0, 0); // 抢占优先级 0（最高）
    HAL_NVIC_EnableIRQ(USART1_IRQn);         /* 使能 USART1 中断通道 */

		/* ② 外设层面：告诉 USART1，哪些事件触发中断 */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE); /* 接收中断 */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE); /* 空闲中断 */

    /* ③ 启动第一次接收——告诉 HAL库，串口1， 收 1 字节存到 rx_byte */
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

/*
 * DMA 初始化：TX=DMA2_Stream7_Ch4, RX=DMA2_Stream5_Ch4
 */
void Usart1_DMA_Init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* TX DMA：内存→外设 */
    hdma_usart1_tx.Instance                 = DMA2_Stream7;// — 选哪台运输车
    hdma_usart1_tx.Init.Channel             = DMA_CHANNEL_4;//— 选哪条运输线
    hdma_usart1_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;// 内存 → 外设（发送），TX：buf 里的数据搬到串口 DR 寄存器
    hdma_usart1_tx.Init.PeriphInc           = DMA_PINC_DISABLE;//外设地址不要递增,固定的
    hdma_usart1_tx.Init.MemInc              = DMA_MINC_ENABLE;//"内存"就是你的发送缓冲区。你要把 buf[0], buf[1], buf[2]... 依次发出去，所以每搬一个字节，地址要 +1。
    hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;//外设端的搬运单位：选1字节
    hdma_usart1_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;//内存端的搬运单位：内存也 1 字节
    hdma_usart1_tx.Init.Mode                = DMA_NORMAL;//普通还是循环模式：选普通模式，搬完指定数量就停。比如你叫它搬 15 字节，搬完它就下班了
    hdma_usart1_tx.Init.Priority            = DMA_PRIORITY_LOW;//多台车抢总线时谁先走，发送慢点没事，接收偏不能丢——所以 TX 用 LOW，RX 用 HIGH。
    hdma_usart1_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;//要不要用 FIFO 缓冲：F429 有 FIFO（F103 没有），但串口逐字节传输用不到。关了就行，数据直接从源到目的地。
    HAL_DMA_Init(&hdma_usart1_tx);

    /* RX DMA：外设→内存 */
    hdma_usart1_rx.Instance                 = DMA2_Stream5;// — 选哪台运输车
    hdma_usart1_rx.Init.Channel             = DMA_CHANNEL_4;//— 选哪条运输线
    hdma_usart1_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;//外设 → 内存	RX：串口 DR 的数据搬到 dma_rx_buf
    hdma_usart1_rx.Init.PeriphInc           = DMA_PINC_DISABLE;//外设地址不要递增,固定的
    hdma_usart1_rx.Init.MemInc              = DMA_MINC_ENABLE;//"内存"就是你的发送缓冲区。你要把 buf[0], buf[1], buf[2]... 依次发出去，所以每搬一个字节，地址要 +1。
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;//外设端的搬运单位：选1字节
    hdma_usart1_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;//内存端的搬运单位：内存也 1 字节
    hdma_usart1_rx.Init.Mode                = DMA_NORMAL;					//普通还是循环模式：选普通模式，搬完指定数量就停。比如你叫它搬 15 字节，搬完它就下班了
    hdma_usart1_rx.Init.Priority            = DMA_PRIORITY_HIGH;//多台车抢总线时谁先走，发送慢点没事，接收偏不能丢——所以 TX 用 LOW，RX 用 HIGH。
    hdma_usart1_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;//要不要用 FIFO 缓冲：F429 有 FIFO（F103 没有），但串口逐字节传输用不到。关了就行，数据直接从源到目的地。
    HAL_DMA_Init(&hdma_usart1_rx);

    __HAL_LINKDMA(&huart1, hdmatx, hdma_usart1_tx);
    __HAL_LINKDMA(&huart1, hdmarx, hdma_usart1_rx);

    /* DMA 中断 */
    HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);
    HAL_NVIC_SetPriority(DMA2_Stream5_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream5_IRQn);
}

/* 发送字符串（逐字节阻塞发送，遇 '\0' 停止） */
void Usart_SendString(uint8_t *str)
{
    unsigned int k = 0;
    do
    {
        HAL_UART_Transmit(&huart1, (uint8_t *)(str + k), 1, 1000);
        k++;
    } while (*(str + k) != '\0');
}

/* 重定向 scanf → 串口阻塞接收一个字节 */
int fgetc(FILE *f)
{
    int ch;
    HAL_UART_Receive(&huart1, (uint8_t *)&ch, 1, 1000);
    return ch;
}

int fputc(int ch, FILE *f)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF);//等60s左右，保证数据一定能完整发完
    return ch; // 返回发送的字符，printf 内部用来判断是否成功
}

// 接收函数
/**
 * @brief  HAL 接收完成回调（每收到 1 字节自动调一次）
 *         把收到的字节存入 usart1_rx_buf，然后重新启动下一次接收
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        if (usart1_rx_len < sizeof(usart1_rx_buf)) /* 防止溢出 */
        {
            usart1_rx_buf[usart1_rx_len++] = rx_byte; /* 存字节，计数 +1 */
        }
        HAL_UART_Receive_IT(&huart1, &rx_byte, 1); /* 继续收下一个 */
    }
}

/* ── DMA 发送完成回调 ── */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        printf("DMA TX done\n");
    }
}

/* ── DMA 发送函数 ── */
void Usart_SendString_DMA(uint8_t *str, uint16_t len)
{
    HAL_UART_Transmit_DMA(&huart1, str, len);
}
