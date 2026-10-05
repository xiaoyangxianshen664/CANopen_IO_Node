# 1：介绍一下先

```c
在我们改成 TIM3 每 1 ms 触发之前，ADC 是启动后一直连续采样的。
当时配置是：
hadc1.Init.ContinuousConvMode = ENABLE;							//ADC 完成一次转换后不停止，立即启动下一次。
hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;  //	不使用外部硬件触发（如定时器 TRGO）
hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;				//由软件 HAL_ADC_Start() 启动，之后靠连续模式自己跑
hadc1.Init.DMAContinuousRequests = ENABLE;					   //ADC 每转换完一个数据就向 DMA 发请求，不会被阻塞，如ADC转换1 → DMA搬数据1
hdma_adc.Init.Mode = DMA_CIRCULAR;							  //普通模式，如果需要DMA搬运8个数据，那么8 个数据搬完以后，DMA 就不继续工作了。
启动一次：												    //选用循环模式的话，不停止，而是重新从 buffer[0] 开始，覆盖旧的数据
HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE);
之后硬件会自动循环执行：
ADC1 扫描 AI1
    ↓
ADC1 扫描 AI2
    ↓
DMA 搬到 adc_buf
    ↓
继续扫描 AI1、AI2
    ↓
DMA 继续搬运
    ↓
缓冲区半满触发中断
    ↓
缓冲区全满触发中断
    ↓
DMA 回到数组开头继续循环
所以当时是：
ADC：一直采样
DMA：一直搬运
IOTask：收到通知后读取一批数据
CANopenTask：再把采样值更新到对象字典
要注意，IOTask 的通知并没有控制 ADC 开始或停止。它只是告诉 IOTask：
DMA 某个阶段的数据已经准备好了，可以来处理。

当时的问题正是：ADC 和 DMA 一直高速运行，但 ADC_Multi_ReadAverage() 没有区分前半区和后半区，而是直接读取整个缓冲区。因此在半传输时，数组的一部分可能是新数据，另一部分可能还是旧数据，甚至可能正被 DMA 改写。
现在改成 TIM3 触发后，逻辑变成：
TIM3 每 1 ms 产生一次触发
    ↓
ADC 扫描一次 AI1、AI2
    ↓
DMA 保存这两个结果
这样 ADC 不再自由运行，而是由定时器控制采样节奏。由于每个 DMA 半区保存 32 组双通道样本：
前半区：约 32 ms 填满
后半区：再约 32 ms 填满
也就是说：
以前是 ADC 自己不停采，DMA 尽快填缓冲区；现在是 TIM3 定时触发 ADC，采样速率稳定可控，IOTask 只读取已经完成的半区。
```



### 采样一半触发中断进入IOTask，此时新的TIM3中断来了怎么办

```c
这里先纠正一个关键点：当前 TIM3 没有开启中断，它每 1 ms 产生的是硬件触发信号 TRGO，直接触发 ADC，不需要 CPU 进入 TIM3 中断函数。
因为我们调用的是：
HAL_TIM_Base_Start(&htim_adc_trigger);
而不是：
HAL_TIM_Base_Start_IT(&htim_adc_trigger);
所以，IOTask 正在处理数据时，下一次 TIM3 触发来了，ADC 和 DMA 会在硬件中继续工作，IOTask 同时继续执行。
用前半区刚完成的时刻举例：
前半区填满
    ↓
DMA 半传输中断通知 IOTask
    ↓
IOTask 读取前半区、计算平均值

与此同时：

TIM3 每 1 ms 继续触发 ADC
    ↓
ADC 扫描 AI1、AI2
    ↓
DMA 把新数据写进后半区
此时两边操作的是不同区域：
执行者	正在操作
IOTask	读取前半区
DMA	写入后半区


这就是把缓冲区分成两个半区的目的：一半供任务处理，另一半供 DMA 继续采集。
后半区大约需要 32 ms 填满。因此正常情况下，IOTask 有约 32 ms 的时间处理前半区；后半区完成后，双方交换：
IOTask 读取后半区
DMA 回到前半区写入
但这个时间窗口不是无限的。**如果 IOTask 被高优先级任务长时间占用 CPU，或者处理太慢，超过半区重用时间，DMA 就可能绕回来覆盖它正在读的数据。**DMA 不会等待 IOTask。
这里也要补充当前代码的边界：我们已经限制了采样速度、按半区读取，但还没有加入读取前后的半区有效性检查；通知位同时为 11 时，也不能仅凭位图确定哪个事件最新。因此当前方案适合及时处理的正常运行，任务严重延迟时的过期数据判断还需要补齐。
你先记住这个运行关系：
TIM3 硬件触发控制采样节奏，ADC 和 DMA 自主工作；DMA 半区完成中断通知 IOTask，IOTask 在该半区被重新使用前处理它。
```



### 当前采用的处理方法：

```c
当前代码里已经有一个很简单的处理逻辑：
ready_bits = 0u;

notified = xTaskNotifyWait(
    0u,
    0xFFFFFFFFUL,
    &ready_bits,
    pdMS_TO_TICKS(10u)
);
xTaskNotifyWait() 返回时，会把当前积累的通知位交给 ready_bits，并清除这些通知位。
正常情况下是：
ready_bits = ADC_READY_FIRST_HALF
表示前半区完成：
DMA：写后半区
IOTask：读前半区
或者：
ready_bits = ADC_READY_SECOND_HALF
表示后半区完成：
DMA：重新写前半区
IOTask：读后半区
这两种情况下，DMA 和 IOTask 操作的是不同区域，互不干扰。
问题在于，如果 IOTask 处理不够及时，两个通知在它下一次调用 xTaskNotifyWait() 前都已经到达，那么：
ready_bits = ADC_READY_FIRST_HALF |
             ADC_READY_SECOND_HALF;
也就是：
ready_bits = 0b11
当前代码的判断是：
half = (ready_bits & ADC_READY_SECOND_HALF) ? 1u : 0u;
只要后半区标志存在，就选择后半区。因此：
ready_bits = 0b11
    ↓
只读取后半区
    ↓
前半区这次被跳过
所以当前不是完全没有逻辑，而是采用了一个很简单的策略：
如果两个半区都完成，只处理较新的后半区，丢弃较旧的前半区。

这其实符合当前 I/O 节点“只关心最新输入状态”的取舍。电位器、温度、按键都不是必须逐个保存的高速波形，跳过一批旧采样通常不会影响最终状态。
但当前确实没有进一步做这些事情：
- 没有逐个处理两个待处理半区；
- 没有统计半区通知是否丢失；
- 没有记录 IOTask 是否处理超时；
- 没有检测 DMA 是否已经重写了尚未处理的半区；
- 没有把两半区合并后再发布一个 64 ms 平均值。
因此数据流可以准确画成：
半传输完成
    ↓
通知 bit0
    ↓
IOTask 及时醒来
    ↓
读取前半区
    ↓
DMA 写后半区
如果 IOTask 没及时醒来：
半传输完成 → bit0
全传输完成 → bit1
    ↓
ready_bits = 0b11
    ↓
当前代码只处理后半区
    ↓
前半区被跳过
所以你说的结论是对的：
当前方案已经解决了“正常情况下 DMA 与 IOTask 读写不同半区”的问题，但在两个标志位同时出现时，目前只做了“优先处理后半区”的基础判断，没有实现完整的积压、丢样检测或双半区合并逻辑。

对于我们这个第一版 CANopen I/O 节点，这个取舍可以接受；如果以后要做精确数据采集，就应该改成明确处理两个半区，或者使用真正的 DMA 双缓冲，并增加溢出和处理超时统计。
```







# 2：ADC及DMA及定时器初始化

```c
void ADC_Multi_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};             			     // GPIO 配置结构体，={0} 表示全部字段清零
    ADC_ChannelConfTypeDef channel = {0};         				 // ADC 通道配置结构体
    TIM_MasterConfigTypeDef trigger_config = {0};                 // 定时器主模式(TRGO)配置结构体

    /* ---- 1. 使能相关外设时钟 ---- */
    ADC_CH1_GPIO_CLK();  									   // 本质是__HAL_RCC_GPIOC_CLK_ENABLE()
    ADC_CH2_GPIO_CLK();  								        // 本质是__HAL_RCC_GPIOA_CLK_ENABLE()
    ADCx_CLK_ENABLE();                                             // 本质是使能 ADC1 外设时钟 (__HAL_RCC_ADC1_CLK_ENABLE)
    ADC_DMA_CLK_ENABLE();                                         // 本质是使能 DMA2 外设时钟 (__HAL_RCC_DMA2_CLK_ENABLE)

    /* ---- 2. 配置 GPIO 为模拟输入 ---- */
    gpio_init.Mode = GPIO_MODE_ANALOG;       					 // ADC专有模式
    gpio_init.Pull = GPIO_NOPULL;                                  // 浮空: 模拟引脚不允许上/下拉，否则会分压
    gpio_init.Pin = ADC_CH1_PIN;                                   // 通道1 引脚号 (如 GPIO_PIN_0)
    HAL_GPIO_Init(ADC_CH1_PORT, &gpio_init);                        // 应用到端口 (如 GPIOC)
    gpio_init.Pin = ADC_CH2_PIN;                                   // 复用同一结构体配置通道2 引脚
    HAL_GPIO_Init(ADC_CH2_PORT, &gpio_init);                       // 应用到端口 (如 GPIOA)

    /* ---- 3. 配置 DMA: 外设(ADC) -> 内存(adc_buf)，环形模式 ---- */
    hdma_adc.Instance = ADC_DMA_STREAM;                            // DMA 流 (如 DMA2_Stream0)
    hdma_adc.Init.Channel = ADC_DMA_CHANNEL;                       // DMA 通道 (与 ADC1 请求源绑定，如 DMA_CHANNEL_0)
    hdma_adc.Init.Direction = DMA_PERIPH_TO_MEMORY;                // 传输方向: 外设到内存
    hdma_adc.Init.PeriphInc = DMA_PINC_DISABLE;                    // 外设地址不递增 (始终读 ADC_DR 固定地址)
    hdma_adc.Init.MemInc = DMA_MINC_ENABLE;                        // 内存地址递增 (依次写入 adc_buf[0], [1], ...)
    hdma_adc.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;   // 外设数据宽度 16bit (ADC_DR 低 16 位有效)
    hdma_adc.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;      // 内存数据宽度 16bit (匹配 uint16_t)
    hdma_adc.Init.Mode = DMA_CIRCULAR;                             // 环形模式: 到缓冲区末尾后自动回卷到开头
    hdma_adc.Init.Priority = DMA_PRIORITY_HIGH;                    // 流优先级: 高 (采样不能丢)
    hdma_adc.Init.FIFOMode = DMA_FIFOMODE_DISABLE;                 // 关闭 FIFO，直接传输 (降低延迟，12bit 数据无需打包)
    HAL_DMA_Init(&hdma_adc);                                       // 写入 DMA 寄存器，生效配置
    /* 优先级 6 低于 FreeRTOS FromISR 禁止线，可安全发送任务通知。 */
    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 6, 0); 				  // 设置 DMA 中断优先级为 6 (抢占优先级)
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);         				   // 使能 DMA 流中断 ,就是NVIC太监和cpu汇报，不然即使触发中断cpu是不知道的
    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc);   				   // 把 DMA 句柄挂到 ADC 句柄上，HAL_ADC_Start_DMA 需用
```





```c
    /* ---- 4. 配置 ADC ---- */
    hadc1.Instance = ADCx;                                    		     // 绑定 ADC 外设 (如 ADC1)
    hadc1.Init.ClockPrescaler = ADC_CLOCKPRESCALER_PCLK_DIV4; 			// ADC 时钟 = PCLK2/4 (保证 <= 36MHz 最大采样时钟)
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;                          // 12 位分辨率，输出 0~4095
    hadc1.Init.ScanConvMode = ENABLE;                                    // 扫描模式: 按 Rank 依次转换多个通道

    /* 每个 TIM3 更新事件触发一次双通道扫描， */
    hadc1.Init.ContinuousConvMode = DISABLE;                           // 关闭连续转换: 一次扫描完成后停止，等下次触发，如果设为enable，ADC 会																									完成一轮扫描，然后自动开始下一轮
    hadc1.Init.DiscontinuousConvMode = DISABLE;                   // 关闭不连续模式 ，比如规则组有两个rank要转换，这个如果enable 的话，
															//NbrOfDiscConversion填的值=1时，那么定时器3触发一次转换，那么一次转一个rank
															//此处关闭这个模式，就是定时器3触发后，把AI1和AI2一同转化了。
    hadc1.Init.NbrOfDiscConversion = 0;                                // 不连续转换的组数，此处不用，因为不连续转换模式在上一句初始化关闭了。
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING; // 外部触发边沿: 上升沿 (TRGO 脉冲)
    hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T3_TRGO;        // TIM3 输出一次 TRGO 上升沿触发 → ADC 开始一轮 AI1、AI2 扫描
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;                        // 数据右对齐: 12 位值存在 bit[11:0] ，bit15 ... bit12 为 0（DMA 使用																											半字搬运所以是16bit）
    hadc1.Init.NbrOfConversion = ADC_CH_COUNT;                // 本次扫描的通道数 = 2，表示规则组包含两个转换 Rank：因此每次 TIM3 触															  发执行：AI1 → AI2，因此DMA 缓冲区中的数据顺序就是：AI1, AI2, AI1, AI2, AI1, AI2
    
/*TIM3 TRGO → ADC AI1 ──发 DMA 请求──> DMA 搬到 adc_buf → ADC AI2 ──发 DMA 请求──> DMA 搬到 adc_buf → 等待 1 ms → 下一次 TIM3 TRGO */
    hadc1.Init.DMAContinuousRequests = ENABLE; 						 // ADC 转换完成持续向 DMA 发请求 (不被清零)
    hadc1.Init.EOCSelection = DISABLE;         // 不产生 EOC 中断，只用 DMA 中断，如果开启这个中断则是：ADC 每次转换完成 → ADC EOC 中断 → CPU 																					手动读取 ADC 数据 ，项目用的DMA中断则不开这个了
    HAL_ADC_Init(&hadc1);                     							 // 初始化 ADC 并写入寄存器


```





```c
    /* ---- 5. 配置扫描序列中的通道 ---- */
    channel.SamplingTime = ADC_SAMPLETIME_56CYCLES;               // 采样时间 56 个 ADC 时钟周期，（APB2 = 90 MHz，PCLK2 = 90 MHz，→ ÷4 ADC 																 clock = 22.5 MHz ），56/22.5MHz = 2.49 us，采样时间越长，采样精度越高
    channel.Offset = 0;                                           // 这是 ADC 通道配置结构体中的偏移量字段。当前使用的是规则组普通通道，不需要额																											外偏移，所以设置为 0
    channel.Channel = ADC_CH1_CHANNEL;                            // 通道1 当前是PC13，#define ADC_CH1_CHANNEL ADC_CHANNEL_13
    channel.Rank = 1;                                             // Rank = 1 表示它是一次扫描中的第一个转换（ADC1 通道 13 → PC3 → AI1 电位																				器），配置后，一次触发会先转换：Rank1：AI1，也就是PC13
    HAL_ADC_ConfigChannel(&hadc1, &channel);                      // 写入 ADC 配置
    channel.Channel = ADC_CH2_CHANNEL;                            // 通道2 当前是PA1，#define ADC_CH2_CHANNEL ADC_CHANNEL_1
    channel.Rank = 2;                                             // Rank = 2 表示它是一次扫描中的第二个转换（ADC1 通道 1 → PA1 → AI2 电位																			  器），配置后，一次触发会接着转换：Rank2：AI2，也就是PA1
    HAL_ADC_ConfigChannel(&hadc1, &channel);                      // 写入 ADC 配置
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE); // 强转(uint32_t *)是因为这个函数定义形参就是(uint32_t *)，不过我们定义的半														字uint16_t adc_buf[ADC_BUF_SIZE]，所以强转一下就行了,数组大小为128个uint16_t元素
```





```c

    /* ---- 6. 配置触发定时器 TIM3: 1kHz 更新事件 ----TIM3 本身不负责读取 ADC 数据，它只周期性产生一个内部 TRGO 信号，ADC 收到这个信号后开始一次 AI1 → AI2 扫描。 */

    ADC_TRIGGER_TIM_CLK();                                                    // 使能 TIM3 时钟，宏实际展开为：__HAL_RCC_TIM3_CLK_ENABLE();
    htim_adc_trigger.Instance = ADC_TRIGGER_TIM;                              // #define ADC_TRIGGER_TIM TIM3
    htim_adc_trigger.Init.Prescaler = ADC_TRIGGER_TIM_PSC;                    // 系统时钟：180 MHz，APB1：45 MHz，APB1 定时器时钟：90 MHz，																	  ADC_TRIGGER_TIM_PSC=8999，计数频率 =90 MHz /（8999+1）=10 kHz
    htim_adc_trigger.Init.CounterMode = TIM_COUNTERMODE_UP;                   // 向上计数
    htim_adc_trigger.Init.Period = ADC_TRIGGER_TIM_ARR;                       // 自动重装载值 ，当前ADC_TRIGGER_TIM_ARR=9，由于计数从 0 数到 															   9，一共是 计数10 个，更新频率=10khz/10=1kHz，触发 ADC 扫描的周期就是 1ms
    htim_adc_trigger.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;             // 表示定时器输入时钟不额外分频，这个字段主要影响定时器内部数字滤                                                         波等相关时钟，不是主要计数频率分频。真正决定计数频率的是：Prescaler而不是 ClockDivision
    htim_adc_trigger.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE; // 关闭 ARR 预装载，改值立即生效，当前定时器频率固定，运行期间没																			 		有动态修改 ARR，所以这个选项对当前功能影响不大
    HAL_TIM_Base_Init(&htim_adc_trigger);                                     // 初始化定时器时基，它只是配置定时器，还没有开始计数。

    /*TIM3 计数到 ARR → 产生 UEV → UEV 被送到 TRGO → ADC 收到 TIM3_TRGO → 开始 AI1 → AI2 扫描*/
    trigger_config.MasterOutputTrigger = TIM_TRGO_UPDATE;                      // 作用只有一句话：决定这个定时器拿"哪个内部事件"去当 TRGO 输出																					信号，送给其他外设当触发源。


	/*有四种触发源*/
	#define TIM_TRGO_RESET  //软件强制更新，一般不用这个模式，属于"手动发一次脉冲"的场景。
    #define TIM_TRGO_ENABLE //计数器一使能就触发一次，那一瞬间产生一个脉冲。适合"启动时同步一波外设"，但之后不再触发
    #define TIM_TRGO_UPDATE //每次溢出/更新都触发 ⭐最常用
    #define TIM_TRGO_OC1    // 比较匹配事件


    trigger_config.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;              // 表示 TIM3 不参与定时器主从同步链路，不等待其他定时器控制，也																			不把自己配置成复杂的同步控制器。
    HAL_TIMEx_MasterConfigSynchronization(&htim_adc_trigger, &trigger_config); // 这句把：MasterOutputTrigger = TIM_TRGO_UPDATE和															MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE，写入 TIM3 的相关寄存器配置。

    HAL_TIM_Base_Start(&htim_adc_trigger); // 启动定时器计数 -> 开始周期性触发 ADC（这句才真正让 TIM3 开始计数：）TIM3 不进入 CPU 中断；
}
```

| MSM                           | 当前含义                         |
| ----------------------------- | -------------------------------- |
| `TIM_MASTERSLAVEMODE_DISABLE` | TIM3 不参与定时器主从同步        |
| `TIM_MASTERSLAVEMODE_ENABLE`  | 开启 TIM3 内部的主从同步控制功能 |





### 回顾一下定时器的从模式：

这里容易把两个概念混在一起：

- **定时器从模式**：复位、门控、触发、外部时钟等；
- **输出比较模式**：其中有一个叫 **Frozen（冻结）模式**。

所以“冻结模式”通常不是定时器主从模式，而是输出比较通道的工作模式。



TRGO = Trigger Output：触发输出

ITR = Internal Trigger  ：内部触发





TRGI = Trigger Input：触发输入



示例：

```c
TIM1_TRGO
    ↓
TIM3 ITR0
    ↓
TIM3 TRGI
```

可以翻译成：

```c
TIM1 的触发输出
       ↓
TIM3 的内部触发输入0
       ↓
TIM3 的触发输入
```

更形象一点：

```c
             芯片内部
┌─────────┐              ┌─────────┐
│  TIM1   │              │  TIM3   │
│         │              │         │
│   TRGO ─┼─────────────→│ ITR0    │
│         │              │   ↓     │
└─────────┘              │  TRGI   │
                         └─────────┘
```

这里有一个很重要的概念：

TRGO 是“我发出去”；TRGI 是“我收进来”；ITR 是定时器之间内部连接触发信号的通道。

可以直接脑补成：

> **一个定时器发触发信号 → 通过芯片内部连接 → 另一个定时器收到触发信号。**

这就是 STM32 定时器**级联/同步**的基础。

代码里通常体现为：

```c
// 主定时器选择用什么事件产生 TRGO
master_config.MasterOutputTrigger = TIM_TRGO_UPDATE;

// 从定时器选择哪一路内部触发输入
slave_config.InputTrigger = TIM_TS_ITR0;

// 从定时器收到触发后采取什么动作
slave_config.SlaveMode = TIM_SLAVEMODE_RESET;
```

因此你最后那句总结得很准确：

> 一个定时器产生触发输出，通过芯片内部映射连接到另一个定时器的 ITR，再进入它的 TRGI，最后由从模式决定如何响应。

这就是 STM32 定时器级联和同步的基本结构。



### 1、复位模式 Reset Mode

```
TIM_SLAVEMODE_RESET
```

行为是：

> 每收到一次有效触发，计数器就重新从 0 开始计数。

例如：

```
TIM1_TRGO ──脉冲────脉冲────脉冲──→ TIM3_TRGI
                                     ↓
TIM3 CNT：0 1 2 3 ... → 触发 → 0 1 2 3 ...
```

它的作用是让 TIM3 的计数周期与 TIM1 的触发保持同步。

常见用途：

- 同步两个定时器；
- 让一个定时器周期重新对齐；
- 让 PWM 或输出波形在外部事件到来时重新开始；
- 消除定时器之间长期运行产生的相位偏差。

注意，复位模式不是“停止定时器”。触发到来时只是把计数器清零，之后它继续按照自己的计数时钟运行。



### 2、门控模式 Gated Mode

```
TIM_SLAVEMODE_GATED
```

行为是：

> 只有当触发输入 TRGI 有效时，定时器才计数；触发无效时，定时器暂停计数。

例如把主定时器的 TRGO 当作门控信号：

```
TRGI： 低低低 | 高高高高 | 低低 | 高高高 | 低
TIM3： 停止   | 允许计数   | 停止 | 允许计数 | 停止
```

它相当于一个“允许运行窗口”：

```
TRGI 有效   → TIM3 运行
TRGI 无效   → TIM3 暂停
```

常见用途：

- 只在某个时间窗口内计时；
- 用外部脉冲控制定时器是否工作；
- 对输入信号的高电平持续时间进行测量；
- 对脉冲门控后的时长进行统计。

它和复位模式的区别是：

| 模式     | 触发到来时的行为               |
| -------- | ------------------------------ |
| 复位模式 | 清零计数器，然后继续计数       |
| 门控模式 | 触发有效时允许计数，失效时暂停 |



### 3、触发模式 Trigger Mode

```
TIM_SLAVEMODE_TRIGGER
```

行为是：

> 定时器平时不启动或保持等待，收到一次有效触发后开始计数。

可以理解成“外部启动”：

```
初始化完成
    ↓
TIM3 等待
    ↓ 收到 TIM1_TRGO
TIM3 开始计数
```

它适合：

- 等待外部同步信号后启动；
- 多个定时器同时开始工作；
- 等待某个事件到来后生成延时或 PWM；
- 让定时器由另一个定时器统一启动。

但是要注意，触发模式通常是“启动一次”。触发之后 TIM3 是否持续运行、是否再次响应触发，还要结合具体定时器状态和配置判断。



### 4、外部时钟模式 1

```
TIM_SLAVEMODE_EXTERNAL1
```

行为是：

> 把 TRGI 输入的触发脉冲当作定时器的计数脉冲。

也就是说，TIM3 不再主要依赖自己的内部时钟计数，而是每收到一个外部触发，就相当于计数一次：

```
TIM1_TRGO：   ↑    ↑    ↑    ↑    ↑
TIM3 CNT：    1    2    3    4    5
```

如果 TIM1 的 TRGO 是 1 kHz：

```
TIM1 每 1 ms 发送一次 TRGO
TIM3 每 1 ms 计数一次
```

此时 TIM3 的计数频率就是 1 kHz。

如果 TIM3 的自动重装载值是：

```
ARR = 9;
```

则 TIM3 收到 10 个触发后产生一次更新事件：

```
1 kHz / 10 = 100 Hz
```

所以这里要特别区分：

```
TIM1 TRGO 频率 = 1 kHz
TIM3 计数频率 = 1 kHz
TIM3 更新事件频率 = 100 Hz
```

如果 TIM3 再把自己的更新事件通过 TRGO 输出给 ADC，那么 ADC 得到的触发频率就是 100 Hz，而不是 1 kHz。



### 5、组合模式

部分 STM32 定时器还支持组合从模式，例如：

```c
TIM_SLAVEMODE_COMBINED_RESETTRIGGER
TIM_SLAVEMODE_COMBINED_GATED
```

它们把多个行为组合到一起。



##### 组合复位加触发

可以理解为：

```
触发到来：
1. 启动或响应定时器；
2. 同时把计数器重新同步到 0。
```

适合要求“每次外部事件都重新对齐”的场景。



##### 组合门控

可以理解为：

```
触发有效时运行；
触发无效时暂停；
并根据触发边沿进行同步。
```

具体可用模式取决于芯片型号和 HAL 库版本，不同 STM32 系列支持的组合模式不完全相同。



### 6、你说的“冻结模式”是什么

冻结模式一般是输出比较模式：

```
TIM_OCMODE_FROZEN
```

它不是“从定时器收到触发后怎么运行”，而是：

> 当计数器与比较寄存器 `CCR` 匹配时，输出通道保持原来的状态，不主动翻转、置位或清零。

例如：

```
CNT == CCR1
    ↓
比较事件发生
    ↓
输出保持当前电平
```

所以它和下面这些模式属于不同层级：

```c
TIM_SLAVEMODE_RESET      // 从模式：收到触发后清零
TIM_SLAVEMODE_GATED      // 从模式：触发有效时计数
TIM_SLAVEMODE_TRIGGER    // 从模式：触发后启动
TIM_SLAVEMODE_EXTERNAL1  // 从模式：把触发当计数脉冲

TIM_OCMODE_FROZEN        // 输出比较模式：匹配时输出保持不变
```





### 7、和主模式 `MSM` 的区别

还要再区分：

```c
TIM_MASTERSLAVEMODE_ENABLE
TIM_MASTERSLAVEMODE_DISABLE
```

这个配置叫 **Master/Slave Mode（主从模式控制位）**，它和上面的：

```c
TIM_SLAVEMODE_RESET
TIM_SLAVEMODE_GATED
TIM_SLAVEMODE_TRIGGER
```

不是一回事。

可以这样分层理解：

```
第一层：主定时器输出什么？
TIM_TRGO_UPDATE
TIM_TRGO_OC1REF
TIM_TRGO_OC2REF
...

第二层：从定时器接收哪个触发？
TIM_TS_ITR0
TIM_TS_ITR1
TIM_TS_TI1FP1
...

第三层：从定时器收到触发后怎么做？
TIM_SLAVEMODE_RESET
TIM_SLAVEMODE_GATED
TIM_SLAVEMODE_TRIGGER
TIM_SLAVEMODE_EXTERNAL1

第四层：是否启用定时器间的主从同步协调？
TIM_MASTERSLAVEMODE_ENABLE/DISABLE
```



### 8、放回我们当前的项目

我们现在没有使用：

```
TIM1 → TIM3
```

也没有配置 TIM3 的从模式：

```
TIM_SLAVEMODE_RESET
TIM_SLAVEMODE_GATED
TIM_SLAVEMODE_TRIGGER
```

当前实际是：

```
TIM3 内部时钟
    ↓
TIM3 自己计数
    ↓ 每 1 ms 产生一次 UEV
TIM3_TRGO_UPDATE
    ↓
ADC1 外部触发
    ↓
AI1 → AI2
    ↓
DMA
```

因此当前工程中：

- TIM3 是独立运行的触发定时器；
- `TIM_TRGO_UPDATE` 选择 TIM3 的更新事件作为 TRGO；
- `TIM_MASTERSLAVEMODE_DISABLE` 表示不建立定时器到定时器的主从同步；
- ADC1 接收 TIM3 的 TRGO，但 ADC1 不是通过 `TIM_SLAVEMODE_*` 配置的定时器从机。

你可以先记住一句话：

> **主定时器负责产生节拍，从定时器负责按照主定时器的触发进行复位、门控、启动或计数；而冻结模式属于输出比较，不属于定时器从模式。**







# 3：读取指定 DMA 半区的两路 ADC 平均值。

这个函数的作用是：

> 从 ADC DMA 缓冲区中选择前半区或后半区，分别计算 AI1、AI2 的 32 次采样平均值，并通过指针返回结果。

```c
当前缓冲区参数是：
ADC_SAMPLES_PER_BATCH = 32
ADC_CH_COUNT          = 2
ADC_HALF_BUF_SIZE     = 64
ADC_BUF_SIZE          = 128
因此缓冲区布局是：
前半区 adc_buf[0] ~ adc_buf[63]
后半区 adc_buf[64] ~ adc_buf[127]
每两个数组元素是一组 AI1、AI2 数据：
[AI1_0][AI2_0]
[AI1_1][AI2_1]
[AI1_2][AI2_2]
...
[AI1_31][AI2_31]
函数逐段看：
uint8_t ADC_Multi_ReadAverage(
    uint8_t half,
    uint16_t *ai1,
    uint16_t *ai2
)
三个参数分别是：
- half：选择读取哪一半缓冲区；
  - 0：前半区；
  - 1：后半区。
- ai1：用于输出 AI1 平均值的指针；
- ai2：用于输出 AI2 平均值的指针。
例如：
uint16_t ai1_value;
uint16_t ai2_value;

ADC_Multi_ReadAverage(0u, &ai1_value, &ai2_value);
函数执行结束后，平均值会写入：
ai1_value
ai2_value
```



### 1. 定义累加变量

```c
uint32_t sum1 = 0u;
uint32_t sum2 = 0u;
uint32_t offset;
uint32_t i;
```

`sum1` 用来累加 AI1 的 32 个采样值，`sum2` 用来累加 AI2 的 32 个采样值。

这里使用 `uint32_t`，而不是 `uint16_t`，是为了防止累加过程溢出。

即使 ADC 是 12 位，单次最大值约为：

```
4095
```

32 次累加最大约为：

```
4095 × 32 = 131040
```

这个数已经超过 `uint16_t` 的最大值：

```
65535
```

所以必须使用至少 32 位的累加变量。





### 2. 检查参数是否合法

```c
if (half > 1u || ai1 == NULL || ai2 == NULL)
    return 0u;
```

这里检查三件事：

```c
half 是否只能是 0 或 1
ai1 是否是空指针
ai2 是否是空指针
```

如果这样调用：

```
ADC_Multi_ReadAverage(2u, &ai1, &ai2);
```

由于不存在第 2 个半区，函数直接失败。

如果这样调用：

```
ADC_Multi_ReadAverage(0u, NULL, &ai2);
```

函数后面无法通过 `ai1` 写入结果，也会直接失败。

返回值约定是：

```
0：失败
1：成功
```





### 3. 计算所选半区的起始位置

```c
offset = (uint32_t)half * ADC_HALF_BUF_SIZE;
```

由于：

```c
ADC_HALF_BUF_SIZE = 64
```

所以：

当 `half = 0`：

```
offset = 0 × 64 = 0
```

读取范围是：

```c
adc_buf[0] ~ adc_buf[63]
```

当 `half = 1`：

```
offset = 1 × 64 = 64
```

读取范围是：

```
adc_buf[64] ~ adc_buf[127]
```

这个 `offset` 就是当前半区的起始下标。





### 4. 循环读取 32 组数据

```c
for (i = 0u; i < ADC_SAMPLES_PER_BATCH; ++i)
{
    sum1 += adc_buf[offset + 2u * i];
    sum2 += adc_buf[offset + 2u * i + 1u];
}
```

因为 DMA 中的数据是交错排列的：

```
AI1_0、AI2_0、AI1_1、AI2_1、AI1_2、AI2_2...
```

所以：

```
offset + 2u * i
```

取到的是 AI1。

而：

```
offset + 2u * i + 1u
```

取到的是 AI2。

以 `half = 0` 为例：

| `i`  | AI1 下标 | AI2 下标 |
| ---- | -------- | -------- |
| 0    | 0        | 1        |
| 1    | 2        | 3        |
| 2    | 4        | 5        |
| 3    | 6        | 7        |
| ...  | ...      | ...      |
| 31   | 62       | 63       |

以 `half = 1` 为例：

| `i`  | AI1 下标 | AI2 下标 |
| ---- | -------- | -------- |
| 0    | 64       | 65       |
| 1    | 66       | 67       |
| 2    | 68       | 69       |
| ...  | ...      | ...      |
| 31   | 126      | 127      |

因此这个循环不会把 AI1 和 AI2 混在一起。





### 5. 计算平均值并写回调用者

```c
*ai1 = (uint16_t)(sum1 / ADC_SAMPLES_PER_BATCH);
*ai2 = (uint16_t)(sum2 / ADC_SAMPLES_PER_BATCH);
```

因为：

```
ADC_SAMPLES_PER_BATCH = 32
```

所以实际计算是：

```
AI1 平均值 = AI1 的 32 个采样值之和 / 32
AI2 平均值 = AI2 的 32 个采样值之和 / 32
```

`*ai1` 和 `*ai2` 表示：

> 把计算结果写入调用者传进来的变量。

例如：

```
uint16_t ai1_value;
uint16_t ai2_value;

ADC_Multi_ReadAverage(1u, &ai1_value, &ai2_value);
```

函数内部的：

```
*ai1 = ...
```

等价于修改外部的：

```
ai1_value
```

这里使用的是整数除法，小数部分会被舍弃。例如：

```
1001 / 32 = 31
```

而不是 31.28125。

ADC 原始值本来就是整数，所以这种处理通常可以接受。如果以后需要四舍五入，可以改成：

```c
*ai1 = (uint16_t)((sum1 + ADC_SAMPLES_PER_BATCH / 2u)
                  / ADC_SAMPLES_PER_BATCH);
```

不过当前版本使用的是直接取整。



### 6. 成功返回

```
return 1u;
```

表示：

```c
参数合法；
指定半区已读取；
AI1 和 AI2 平均值已经写入输出变量。
```



### 整个函数的执行过程

以读取后半区为例：

```
ADC_Multi_ReadAverage(1u, &ai1, &ai2);
```

执行流程是：

```c
half = 1
  ↓
offset = 1 × 64 = 64
  ↓
读取 adc_buf[64] ~ adc_buf[127]
  ↓
偶数位置累加到 sum1
奇数位置累加到 sum2
  ↓
sum1 / 32 得到 AI1 平均值
sum2 / 32 得到 AI2 平均值
  ↓
写入 ai1、ai2
  ↓
返回 1
```

它和 DMA 半区通知的配合关系是：

```c
DMA 完成前半区
    ↓
设置 ADC_READY_FIRST_HALF
    ↓
IOTask 收到通知
    ↓
ADC_Multi_ReadAverage(0u, ...)
```

或者：

```c
DMA 完成后半区
    ↓
设置 ADC_READY_SECOND_HALF
    ↓
IOTask 收到通知
    ↓
ADC_Multi_ReadAverage(1u, ...)
```

所以这个函数的关键安全前提是：

> 只能读取 DMA 已经完成的那一半，不能在 DMA 还正在写入该半区时读取它。

最终，这个函数完成了三件事：

```
选择半区
    ↓
按 AI1/AI2 交错格式拆分数据
    ↓
分别计算 32 次采样平均值
```

也就是说，它是“DMA 原始缓冲区”与“IOTask 使用的 AI1、AI2 稳定值”之间的转换函数。





# 4：DMA半传输回调

```c
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc != NULL && hadc->Instance == ADC1)
        adc_dma_notify_from_isr(ADC_READY_FIRST_HALF);//ADC_READY_FIRST_HALF是我们定义的一个标志位  (1UL << 0)
}

```

它们本身不读取 ADC，也不计算平均值，只负责告诉 `IOTask`：

> DMA 缓冲区的哪一半已经写完，可以读取了。

完整调用链是：

```c
TIM3 TRGO
    ↓
ADC1 扫描 AI1、AI2
    ↓
DMA 写入 adc_buf
    ↓
DMA2_Stream0_IRQHandler()
    ↓
HAL_DMA_IRQHandler()
    ↓
HAL_ADC_ConvHalfCpltCallback()
    ↓
adc_dma_notify_from_isr()
    ↓
IOTask
```







# 5：DMA 全传输回调

```c
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc != NULL && hadc->Instance == ADC1)
        adc_dma_notify_from_isr(ADC_READY_SECOND_HALF);////ADC_READY_SECOND_HALF  (1UL << 1)
}

```



```c
TIM3 TRGO
    ↓
ADC1 扫描 AI1、AI2
    ↓
DMA 写入 adc_buf
    ↓
DMA2_Stream0_IRQHandler()
    ↓
HAL_DMA_IRQHandler()
    ↓
HAL_ADC_ConvCpltCallback()
    ↓
adc_dma_notify_from_isr()
    ↓
IOTask
```





这两个回调运行在中断上下文中，也就是 ISR 环境。

因此不适合在里面执行：

```c
ADC_Multi_ReadAverage()
```

尤其不适合做复杂处理，例如：

- 遍历 32 组采样；
- 计算平均值；
- 更新对象字典；
- 处理 PDO；
- 执行阻塞操作；
- 调用普通 FreeRTOS API。

当前回调只做一件事：

```
adc_dma_notify_from_isr(...);
```

然后由任务上下文中的 `IOTask` 做后续工作：

```c
DMA 回调：
快速设置通知位

IOTask：
读取已完成半区
计算平均值
更新 AI1、AI2
更新对象字典
执行后续输入处理
```

这就是典型的：

```
中断负责“发现事件”
任务负责“处理事件”
```



最核心的原则就是：

> 半传输回调只通知前半区完成，全传输回调只通知后半区完成；IOTask 根据通知位读取已经写完的那一半，避免读取 DMA 正在写入的数据。