#include "./ADC/ADC_Multi.h"

/* 应用层提供强定义；该弱实现保证 ADC 驱动可独立链接。 */
__weak void adc_dma_notify_from_isr(uint32_t area_flag)
{
    (void)area_flag;
}

ADC_HandleTypeDef hadc1;                   // ADC1 句柄
DMA_HandleTypeDef hdma_adc;                // DMA 句柄
static TIM_HandleTypeDef htim_adc_trigger; // 触发用定时器句柄(静态，仅本文件可见）
uint16_t adc_buf[ADC_BUF_SIZE];            // DMA 目标缓冲区，存放原始 ADC 采样值

/**
 * @brief 初始化 ADC1 双通道扫描和循环 DMA。
 * @param 无。
 * @return 无。
 * @note Rank1 为 PC3，Rank2 为 PA4；DMA 半区完成后由回调通知任务。
 */
void ADC_Multi_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};             // GPIO 配置结构体，={0} 表示全部字段清零
    ADC_ChannelConfTypeDef channel = {0};         // ADC 通道配置结构体
    TIM_MasterConfigTypeDef trigger_config = {0}; // 定时器主模式(TRGO)配置结构体

    /* ---- 1. 使能相关外设时钟 ---- */
    ADC_CH1_GPIO_CLK();   // 本质是__HAL_RCC_GPIOC_CLK_ENABLE()
    ADC_CH2_GPIO_CLK();   // 本质是__HAL_RCC_GPIOA_CLK_ENABLE()
    ADCx_CLK_ENABLE();    // 本质是使能 ADC1 外设时钟 (__HAL_RCC_ADC1_CLK_ENABLE)
    ADC_DMA_CLK_ENABLE(); // 本质是使能 DMA2 外设时钟 (__HAL_RCC_DMA2_CLK_ENABLE)

    /* ---- 2. 配置 GPIO 为模拟输入 ---- */
    gpio_init.Mode = GPIO_MODE_ANALOG;       // ADC专有模式
    gpio_init.Pull = GPIO_NOPULL;            // 浮空: 模拟引脚不允许上/下拉，否则会分压
    gpio_init.Pin = ADC_CH1_PIN;             // 通道1 引脚号 (如 GPIO_PIN_0)
    HAL_GPIO_Init(ADC_CH1_PORT, &gpio_init); // 应用到端口 (如 GPIOC)
    gpio_init.Pin = ADC_CH2_PIN;             // 复用同一结构体配置通道2 引脚
    HAL_GPIO_Init(ADC_CH2_PORT, &gpio_init); // 应用到端口 (如 GPIOA)

    /* ---- 3. 配置 DMA: 外设(ADC) -> 内存(adc_buf)，环形模式 ---- */
    hdma_adc.Instance = ADC_DMA_STREAM;                          // DMA 流 (如 DMA2_Stream0)
    hdma_adc.Init.Channel = ADC_DMA_CHANNEL;                     // DMA 通道 (与 ADC1 请求源绑定，如 DMA_CHANNEL_0)
    hdma_adc.Init.Direction = DMA_PERIPH_TO_MEMORY;              // 传输方向: 外设到内存
    hdma_adc.Init.PeriphInc = DMA_PINC_DISABLE;                  // 外设地址不递增 (始终读 ADC_DR 固定地址)
    hdma_adc.Init.MemInc = DMA_MINC_ENABLE;                      // 内存地址递增 (依次写入 adc_buf[0], [1], ...)
    hdma_adc.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD; // 外设数据宽度 16bit (ADC_DR 低 16 位有效)
    hdma_adc.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;    // 内存数据宽度 16bit (匹配 uint16_t)
    hdma_adc.Init.Mode = DMA_CIRCULAR;                           // 环形模式: 到缓冲区末尾后自动回卷到开头
    hdma_adc.Init.Priority = DMA_PRIORITY_HIGH;                  // 流优先级: 高 (采样不能丢)
    hdma_adc.Init.FIFOMode = DMA_FIFOMODE_DISABLE;               // 关闭 FIFO，直接传输 (降低延迟，12bit 数据无需打包)
    HAL_DMA_Init(&hdma_adc);                                     // 写入 DMA 寄存器，生效配置

    /* 优先级 6 低于 FreeRTOS FromISR 禁止线，可安全发送任务通知。 */
    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 6, 0); // 设置 DMA 中断优先级为 6 (抢占优先级)
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);         // 使能 DMA 流中断 (HAL 会在 TC/HT 时回调)
    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc);   // 把 DMA 句柄挂到 ADC 句柄上，HAL_ADC_Start_DMA 需用

    /* ---- 4. 配置 ADC ---- */
    hadc1.Instance = ADCx;                                    // 绑定 ADC 外设 (如 ADC1)
    hadc1.Init.ClockPrescaler = ADC_CLOCKPRESCALER_PCLK_DIV4; // ADC 时钟 = PCLK2/4 (保证 <= 36MHz 最大采样时钟)
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;               // 12 位分辨率，输出 0~4095
    hadc1.Init.ScanConvMode = ENABLE;                         // 扫描模式: 按 Rank 依次转换多个通道
    /* 每个 TIM3 更新事件触发一次双通道扫描，避免 DMA 以满速循环覆盖半区。 */
    hadc1.Init.ContinuousConvMode = DISABLE;                           // 关闭连续转换: 一次扫描完成后停止，等下次触发，如果设为enable，ADC 会完成一轮扫描，然后自动开始下一轮
    hadc1.Init.DiscontinuousConvMode = DISABLE;                        // 关闭不连续模式 ，指的是不关闭，即使定时器3触发了adc 转换，它一次也只会根据NbrOfDiscConversion填的值进行规则组的转换，而非把AI1和AI2都转换完才停止
    hadc1.Init.NbrOfDiscConversion = 0;                                // 不连续转换的组数，此处不用，因为不连续转换关闭了。
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING; // 外部触发边沿: 上升沿 (TRGO 脉冲)
    hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T3_TRGO;        // TIM3 输出一次 TRGO 上升沿触发 → ADC 开始一轮 AI1、AI2 扫描
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;                        // 数据右对齐: 12 位值存在 bit[11:0] ，bit15 ... bit12 为 0（DMA 使用半字搬运所以是16bit）
    hadc1.Init.NbrOfConversion = ADC_CH_COUNT;                         // 本次扫描的通道数 = 2，表示规则组包含两个转换 Rank：因此每次 TIM3 触发执行：AI1 → AI2，因此DMA 缓冲区中的数据顺序就是：AI1, AI2, AI1, AI2, AI1, AI2
    /*TIM3 TRGO → ADC AI1 ──发 DMA 请求──> DMA 搬到 adc_buf → ADC AI2 ──发 DMA 请求──> DMA 搬到 adc_buf → 等待 1 ms → 下一次 TIM3 TRGO */
    hadc1.Init.DMAContinuousRequests = ENABLE; // ADC 转换完成持续向 DMA 发请求 (不被清零)
    hadc1.Init.EOCSelection = DISABLE;         // 不产生 EOC 中断，只用 DMA 中断，如果开启这个中断则是：ADC 每次转换完成 → ADC EOC 中断 → CPU 手动读取 ADC 数据 ，项目用的DMA中断则不开这个了
    HAL_ADC_Init(&hadc1);                      // 初始化 ADC 并写入寄存器

    /* ---- 5. 配置扫描序列中的通道 ---- */
    channel.SamplingTime = ADC_SAMPLETIME_56CYCLES;               // 采样时间 56 个 ADC 时钟周期，（APB2 = 90 MHz，PCLK2 = 90 MHz，→ ÷4 ADC clock = 22.5 MHz ），56/22.5MHz = 2.49 us，采样时间越长，采样精度越高
    channel.Offset = 0;                                           // 这是 ADC 通道配置结构体中的偏移量字段。当前使用的是规则组普通通道，不需要额外偏移，所以设置为 0
    channel.Channel = ADC_CH1_CHANNEL;                            // 通道1 当前是PC13，#define ADC_CH1_CHANNEL ADC_CHANNEL_13
    channel.Rank = 1;                                             // Rank = 1 表示它是一次扫描中的第一个转换（ADC1 通道 13 → PC3 → AI1 电位器），配置后，一次触发会先转换：Rank1：AI1，也就是PC13
    HAL_ADC_ConfigChannel(&hadc1, &channel);                      // 写入 ADC 配置
    channel.Channel = ADC_CH2_CHANNEL;                            // 通道2 当前是PA1，#define ADC_CH2_CHANNEL ADC_CHANNEL_1
    channel.Rank = 2;                                             // Rank = 2 表示它是一次扫描中的第二个转换（ADC1 通道 1 → PA1 → AI2 电位器），配置后，一次触发会接着转换：Rank2：AI2，也就是PA1
    HAL_ADC_ConfigChannel(&hadc1, &channel);                      // 写入 ADC 配置
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE); // 强转(uint32_t *)是因为这个函数定义形参就是(uint32_t *)，不过我们定义的半字uint16_t adc_buf[ADC_BUF_SIZE]，所以强转一下就行了,数组大小为128个uint16_t元素

    /* ---- 6. 配置触发定时器 TIM3: 1kHz 更新事件 ----TIM3 本身不负责读取 ADC 数据，它只周期性产生一个内部 TRGO 信号，ADC 收到这个信号后开始一次 AI1 → AI2 扫描。 */

    ADC_TRIGGER_TIM_CLK();                                                    // 使能 TIM3 时钟，宏实际展开为：__HAL_RCC_TIM3_CLK_ENABLE();
    htim_adc_trigger.Instance = ADC_TRIGGER_TIM;                              // #define ADC_TRIGGER_TIM TIM3
    htim_adc_trigger.Init.Prescaler = ADC_TRIGGER_TIM_PSC;                    // 系统时钟：180 MHz，APB1：45 MHz，APB1 定时器时钟：90 MHz，ADC_TRIGGER_TIM_PSC=8999，计数频率 =90 MHz /（8999+1）=10 kHz
    htim_adc_trigger.Init.CounterMode = TIM_COUNTERMODE_UP;                   // 向上计数
    htim_adc_trigger.Init.Period = ADC_TRIGGER_TIM_ARR;                       // 自动重装载值 ，当前ADC_TRIGGER_TIM_ARR=9，由于计数从 0 数到 9，一共是 计数10 个，更新频率=10khz/10=1kHz，触发 ADC 扫描的周期就是 1ms
    htim_adc_trigger.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;             // 表示定时器输入时钟不额外分频，这个字段主要影响定时器内部数字滤波等相关时钟，不是主要计数频率分频。真正决定计数频率的是：Prescaler而不是 ClockDivision
    htim_adc_trigger.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE; // 关闭 ARR 预装载，改值立即生效，当前定时器频率固定，运行期间没有动态修改 ARR，所以这个选项对当前功能影响不大
    HAL_TIM_Base_Init(&htim_adc_trigger);                                     // 初始化定时器时基，它只是配置定时器，还没有开始计数。

    /*TIM3 计数到 ARR → 产生 UEV → UEV 被送到 TRGO → ADC 收到 TIM3_TRGO → 开始 AI1 → AI2 扫描*/
    trigger_config.MasterOutputTrigger = TIM_TRGO_UPDATE;                      // 作用只有一句话：决定这个定时器拿"哪个内部事件"去当 TRGO 输出信号，送给其他外设当触发源。
    trigger_config.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;              // 表示 TIM3 不参与定时器主从同步链路，不等待其他定时器控制，也不把自己配置成复杂的同步控制器。
    HAL_TIMEx_MasterConfigSynchronization(&htim_adc_trigger, &trigger_config); // 这句把：MasterOutputTrigger = TIM_TRGO_UPDATE和MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE，写入 TIM3 的相关寄存器配置。

    HAL_TIM_Base_Start(&htim_adc_trigger); // 启动定时器计数 -> 开始周期性触发 ADC（这句才真正让 TIM3 开始计数：）TIM3 不进入 CPU 中断；
}

/**
 * @brief 读取指定 DMA 半区的两路 ADC 平均值。
 * @param half [输入] 0 表示前半区，1 表示后半区。
 * @param ai1 [输出] PC3 电位器平均值。
 * @param ai2 [输出] PA4 温度传感器平均值。
 * @return 1 表示读取成功，0 表示参数无效。
 */
uint8_t ADC_Multi_ReadAverage(uint8_t half, uint16_t *ai1, uint16_t *ai2)
{
    uint32_t sum1 = 0u;
    uint32_t sum2 = 0u;
    uint32_t offset;
    uint32_t i;

    if (half > 1u || ai1 == NULL || ai2 == NULL)
        return 0u;

    offset = (uint32_t)half * ADC_HALF_BUF_SIZE;
    for (i = 0u; i < ADC_SAMPLES_PER_BATCH; ++i)
    {
        sum1 += adc_buf[offset + 2u * i];
        sum2 += adc_buf[offset + 2u * i + 1u];
    }

    *ai1 = (uint16_t)(sum1 / ADC_SAMPLES_PER_BATCH);
    *ai2 = (uint16_t)(sum2 / ADC_SAMPLES_PER_BATCH);
    return 1u;
}

/**
 * @brief ADC DMA 前半区完成回调。
 * @param hadc [输入] 触发回调的 ADC 句柄。
 * @return 无。
 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc != NULL && hadc->Instance == ADC1)
        adc_dma_notify_from_isr(ADC_READY_FIRST_HALF);
}

/**
 * @brief ADC DMA 后半区完成回调。
 * @param hadc [输入] 触发回调的 ADC 句柄。
 * @return 无。
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc != NULL && hadc->Instance == ADC1)
        adc_dma_notify_from_isr(ADC_READY_SECOND_HALF);
}
