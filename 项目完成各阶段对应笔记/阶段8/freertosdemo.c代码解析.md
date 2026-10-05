# 1：任务优先级

```c
#define CANOPEN_TASK_PRIORITY       3U
#define IO_TASK_PRIORITY            2U
#define MONITOR_TASK_PRIORITY       1U
#define DIAGNOSTICS_TASK_PRIORITY   1U
```

FreeRTOS 任务优先级是**数字越大，优先级越高**。所以这里的顺序是：

```
CanopenTask (3) > IOTask (2) > MonitorTask 和 DiagnosticsTask (1)
```

工程配置中 `configMAX_PRIORITIES` 是 32，因此合法优先级范围是 0～31。空闲任务通常使用优先级 0。处于就绪状态的高优先级任务会优先获得 CPU；两个同为优先级 1 的任务则共享这个优先级。





# 2：任务的栈深度

```c
#define CANOPEN_TASK_STACK_SIZE     512U
#define IO_TASK_STACK_SIZE          384U
#define MONITOR_TASK_STACK_SIZE     256U
#define DIAGNOSTICS_TASK_STACK_SIZE 256U
```

这几行设置各任务的栈深度。**这里的数字不是字节数，而是栈的单位数量**。本工程是 32 位 STM32，FreeRTOS 的一个栈单位是 4 字节，因此对应为：

| 任务            | 栈深度 | 约占内存  |
| --------------- | ------ | --------- |
| CanopenTask     | 512    | 2048 字节 |
| IOTask          | 384    | 1536 字节 |
| MonitorTask     | 256    | 1024 字节 |
| DiagnosticsTask | 256    | 1024 字节 |



任务调用函数、保存局部变量和中断上下文时都会用到任务栈。栈太小可能造成内存破坏或异常；留得太大则会占用更多 RAM。后续可以用 FreeRTOS 的栈高水位接口检查实际使用量，再判断是否需要调整。





# 3：任务句柄

```c
static TaskHandle_t CanopenTaskHandle;
static TaskHandle_t IoTaskHandle;
static TaskHandle_t MonitorTaskHandle;
static TaskHandle_t DiagnosticsTaskHandle;

//这里的TaskHandle_t 是一个重定义
typedef void * TaskHandle_t; 为一个指向void型的指针变量
    
    
```

```c
句柄的语义就是"我能用它找东西，但我不该知道东西长什么样" 

"找到结构体"是内核的内部行为，"不暴露结构体"是对外的契约。
//死傲娇了这一块
```

具体本质就是：

```c
可以直接用结构体指针，但 FreeRTOS 希望应用层只通过 API 操作任务，隐藏任务控制块的内部细节。
假设把完整结构体公开：
typedef struct
{
    uint32_t priority;
    // 栈地址、任务状态等……
} TaskControlBlock;

typedef TaskControlBlock *TaskHandle_t;
应用代码就能直接修改成员：
CanopenTaskHandle->priority = 5;
但任务优先级还关联调度器里的就绪链表等数据。**只改这个成员，没有同步调整其他内部状态，就可能破坏调度。**所以调整优先级应该调用：
vTaskPrioritySet(CanopenTaskHandle, 5);
用 void * 后，应用层能保存和传递地址，但无法直接访问成员：
CanopenTaskHandle->priority;  // 编译不通过
FreeRTOS 内部知道真实结构体类型，会把句柄转换成内部指针再操作。整个关系是：
应用层：保存句柄 → 交给 API
                         ↓
FreeRTOS 内部：找到任务控制块 → 操作任务
```





# 4：这段是在准备任务之间传递的数据，以及运行时观察用的变量

```c
typedef struct
{
    uint8_t di;
    uint16_t ai1;
    uint16_t ai2;
} io_sample_t;

static QueueHandle_t io_sample_queue;
static QueueHandle_t io_output_queue;

volatile uint32_t freertos_io_sample_count;
volatile uint32_t freertos_monitor_tick_count;
volatile co_status_t freertos_diagnostics_last_error = CO_OK;
volatile uint32_t freertos_diagnostics_rx_overflow;
volatile uint32_t freertos_diagnostics_tx_busy;
volatile uint32_t freertos_diagnostics_can_error;
volatile uint32_t freertos_diagnostics_bus_off;
static volatile uint32_t freertos_io_queue_error_count;

static void remember_status(co_status_t status);
static void dispatch_frame(const can_frame_t *frame);
```



### 1. `io_sample_t`：一次 I/O 采样的数据包

```c
typedef struct
{
    uint8_t di;
    uint16_t ai1;
    uint16_t ai2;
} io_sample_t;
```

这里定义了一个结构体类型，并起名为 `io_sample_t`：

| 成员  | 含义                  | 当前项目对应          |
| ----- | --------------------- | --------------------- |
| `di`  | 打包后的数字输入      | bit0：PA0；bit1：PC13 |
| `ai1` | 第一路 ADC 原始平均值 | PC3 电位器            |
| `ai2` | 第二路 ADC 原始平均值 | PA4 温度传感器        |

以后可以这样创建一个采样变量：

```c
io_sample_t sample;

sample.di  = 0x01;  // Key1 按下
sample.ai1 = 2336;
sample.ai2 = 1951;
```

**把三个输入放进一个结构体，方便作为一份采样快照整体传递。**

注意，这里的 AI 还是 ADC 原始值，范围为 `0～4095`；交给对象字典层后，才转换成我们约定的“原始值 × 8”。

另外，成员的数据大小相加是 5 字节，但由于内存对齐，当前平台上结构体通常占 6 字节。所以创建队列时使用：

```
sizeof(io_sample_t)
```

让编译器计算大小，不手写 `5`。





### 2. 两个队列句柄：输入和输出的传递通道

```c
//同样，这玩意还是隐藏结构体类型 
typedef void * QueueHandle_t;
static QueueHandle_t io_sample_queue;
static QueueHandle_t io_output_queue;
```

`QueueHandle_t` 与刚才的 `TaskHandle_t` 类似，是 FreeRTOS 提供的队列句柄类型。

这两行只是准备保存句柄，**还没有创建队列**。后面通过 `xQueueCreate()` 创建：

```c
io_sample_queue = xQueueCreate(1u, sizeof(io_sample_t));
io_output_queue = xQueueCreate(1u, sizeof(uint8_t));
```

它们传递的数据和方向不同：

```c
IOTask
  │
  │ io_sample_t：DI、AI1、AI2
  ↓
io_sample_queue
  ↓
CanopenTask → 更新对象字典
CanopenTask
  │
  │ uint8_t：DO 输出值
  ↓
io_output_queue
  ↓
IOTask → 控制 RGB 灯
```

两个队列长度都是 1，配合 `xQueueOverwrite()`，相当于**只保存最新状态的邮箱**。

FreeRTOS 队列会复制数据内容。因此 `IOTask` 发送的是结构体内容，接收任务拿到自己的副本，双方不需要共享同一个 `sample` 变量。

这里的 `static` 表示两个句柄只在当前 `.c` 文件内可见。





### 3. `volatile` 变量：运行计数和诊断快照

```c
volatile uint32_t freertos_io_sample_count;
volatile uint32_t freertos_monitor_tick_count;
```

这两个是运行计数：

- `freertos_io_sample_count`：IOTask 成功把采样发布到输入队列的次数。
- `freertos_monitor_tick_count`：MonitorTask 执行循环的次数，当前大约每秒增加一次。

例如 MonitorTask 观察采样计数是否增长，就能初步判断采样链路有没有继续推进。

接着是诊断快照：

```c
volatile co_status_t freertos_diagnostics_last_error = CO_OK;//最近一次记录的协议错误状态
volatile uint32_t freertos_diagnostics_rx_overflow;			//CAN 软件接收队列溢出次数
volatile uint32_t freertos_diagnostics_tx_busy;				//CAN 发送邮箱繁忙次数
volatile uint32_t freertos_diagnostics_can_error;			//CAN 错误累计次数
volatile uint32_t freertos_diagnostics_bus_off;				//Bus-off 累计次数
```

| 变量后缀      | 保存的内容                 |
| ------------- | -------------------------- |
| `last_error`  | 最近一次记录的协议错误状态 |
| `rx_overflow` | CAN 软件接收队列溢出次数   |
| `tx_busy`     | CAN 发送邮箱繁忙次数       |
| `can_error`   | CAN 错误累计次数           |
| `bus_off`     | Bus-off 累计次数           |

这些变量由 `DiagnosticsTask` 周期复制更新，方便调试器观察，**目前不会因此自动发送 EMCY**。

`co_status_t` 是项目定义的状态类型；初始化为 `CO_OK`，表示启动时还没有记录到错误。

```c
static volatile uint32_t freertos_io_queue_error_count;
```

这个是文件内部使用的异常计数。目前既统计队列操作失败，也统计 MonitorTask 发现采样计数未增长的情况，因此不能单凭它判断一定是队列故障。

**`volatile` 的含义要特别记准：**

它告诉编译器，这个变量可能被当前代码之外的执行流程改变，读取和写入时不能随意省略实际的内存访问。

但它**不等于互斥锁，也不保证并发操作安全**：

```
count++;
```

实际上包含“读取 → 加一 → 写回”。两个任务同时执行仍可能丢失一次更新。当前这个内部异常计数有多个任务修改，`volatile` 本身不能解决这种竞争。

没有显式初值的这些文件作用域变量，也会在启动时自动初始化为 0。





### 4. 最后两行：提前声明内部函数

```c
static void remember_status(co_status_t status);
static void dispatch_frame(const can_frame_t *frame);
```

这是**函数声明**：先告诉编译器函数的名字、参数和返回类型，具体实现放在后面。

```c
static void remember_status(co_status_t status);
```

意思是：

- `static`：函数只供当前 `.c` 文件使用；
- `void`：没有返回值；
- `status`：传入一个协议处理结果。

它后面会过滤 `CO_OK` 和 `CO_IGNORED`，只把错误保存到 `canopen_last_error`。

```c
static void dispatch_frame(const can_frame_t *frame);
```

表示传入一个 CAN 帧的地址，由它把帧交给 NMT、SDO、PDO 接收函数处理。

这里的：

```
const can_frame_t *frame
```

意思是：**不能通过这个指针修改它所指向的 CAN 帧内容**，适合只读取、解析报文的函数。

这一段可以记成：

> `io_sample_t` 定义输入数据包；两个队列负责输入和输出的跨任务传递；计数和快照方便观察运行情况；最后两行提前声明内部辅助函数。





### 5.队列可以传输的数据类型

FreeRTOS 队列可以存整个结构体。**它按创建时指定的字节数复制数据，不关心数据是整数还是结构体。

用我们项目的代码来看：



**① 创建队列，指定每个元素的大小：**

```c
io_sample_queue = xQueueCreate(1u, sizeof(io_sample_t));
```

表示：

```c
队列容量：1 个元素
每个元素：一份完整的 io_sample_t 结构体
```



**② IOTask 把结构体放进去：**

```c
io_sample_t sample;

sample.di  = 0x01;
sample.ai1 = 2336;
sample.ai2 = 1951;

xQueueOverwrite(io_sample_queue, &sample);
```

传入 `&sample`，是为了告诉 FreeRTOS：“从这个地址开始复制数据。”

**队列里保存的是结构体内容的副本，而不是 `sample` 的地址。**

```
IOTask 的 sample          队列内的副本
┌──────────────┐         ┌──────────────┐
│ di  = 1      │  复制   │ di  = 1      │
│ ai1 = 2336   │ ──────> │ ai1 = 2336   │
│ ai2 = 1951   │         │ ai2 = 1951   │
└──────────────┘         └──────────────┘
```

所以发送完成后，即使 IOTask 修改了自己的 `sample`，队列里那份数据也不会跟着改变。



**③ CanopenTask 取出结构体：**

```c
io_sample_t sample;

if (xQueueReceive(io_sample_queue, &sample, 0u) == pdPASS)
{
    Canopen_App_UpdateInputs(sample.di, sample.ai1, sample.ai2);
}
```

FreeRTOS 把队列里的数据复制到 **CanopenTask 自己的 `sample`** 中。`0u` 表示不等待：队列有数据就取，没有就立即返回。

因此整条链就是：

```
IOTask 的结构体
    ↓ 复制进去
队列中的结构体副本
    ↓ 复制出来
CanopenTask 的结构体
```

这正是结构体适合用在队列里的原因：**把有关联的 DI、AI1、AI2 打包，作为一份完整数据一起传递。**

补充一个边界：如果结构体里有指针成员，队列只会复制那个指针值，不会复制指针指向的数据。我们这个结构体全是数值成员，所以可以直接这样使用。





# 5：这个函数的作用是：**ADC DMA 完成一批采样后，在中断里通知 IOTask，让它起来处理数据**

```c
//我们第一版没有区分前后半区所采集的adc 值用的通知函数是vTaskNotifyGiveFromISR（），只有两个参数
void adc_dma_notify_from_isr(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (IoTaskHandle != NULL)
    {
        vTaskNotifyGiveFromISR(IoTaskHandle, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

//改用区分前后半区的时候，改用函数xTaskNotifyFromISR（），这个有四个参数
```

它只发通知，ADC 数据仍在 DMA 缓冲区里。

（它可以翻译成一句人话：

> **ADC DMA 中断发生后，通知 IoTask：“数据准备好了，你可以起来干活了。”**）

```c
//运行流程
ADC DMA 半传输 / 全传输中断
    ↓
adc_dma_notify_from_isr()
    ↓
通知 IOTask
    ↓
IOTask 读取并处理 ADC 数据
```



### 1. 准备一个“是否需要切换任务”的标志

```c
//typedef long BaseType_t;
BaseType_t higher_priority_task_woken = pdFALSE;
```

`BaseType_t` 是 FreeRTOS 定义的基础整数类型。这里用它保存一个标志：

- `pdFALSE`：目前没有唤醒需要立即运行的更高优先级任务。
- `pdTRUE`：通知操作唤醒了比**被中断打断的任务**优先级更高的任务。

注意：这里比较的是**两个任务的优先级**，不是任务与中断的优先级。



### 2. 确认 IOTask 已创建

```
if (IoTaskHandle != NULL)
```

ADC DMA 在任务创建前就可能已经启动。如果这时发生中断，`IoTaskHandle` 还是 `NULL`，就跳过通知。

任务创建成功后，这个句柄才能用来找到 IOTask。



### 3. 从中断中发送任务通知

```c
(void)xTaskNotifyFromISR(IoTaskHandle, ready_bits, eSetBits,
                                 &higher_priority_task_woken);
```

它有四个参数，因为它允许指定：

- 要通知哪个任务；
- 传什么通知值；
- 按什么规则更新通知值，例如 `eSetBits`；
- 是否唤醒了更高优先级任务，并把pdtrue 或者pdfalse 更新到变量higher_priority_task_woken中



其中实参可以理解为：“把这次传来的标志位，**并入 IOTask 当前的通知值**”，规则就是按位 OR（`|=`）。

假设定义是：

```
#define ADC_READY_FIRST_HALF  (1UL << 0)  // 二进制 01
#define ADC_READY_SECOND_HALF (1UL << 1)  // 二进制 10
```

一开始 IOTask 的通知值是 `00`。

前半区完成时，传入 `01`：

```
当前通知值 00
ready_bits 01
按位 OR    01
```

通知值变成 `01`，表示前半区完成。

如果 IOTask 还没来得及处理，后半区也完成了，传入 `10`：

```
当前通知值 01
ready_bits 10
按位 OR    11
```

通知值变成 `11`，两个标志都还在，表示前、后半区都完成了。IOTask 收到后就能检查这两个位，并按代码逻辑选择处理半区。

这里的重点是：`eSetBits` **不会用新来的值覆盖旧值**，而是把新标志合并进去。通知值可以同时记录多个状态。

对应到函数：

```
xTaskNotifyFromISR(IoTaskHandle, ready_bits, eSetBits,
                   &higher_priority_task_woken);
```

就是：

> 把 `ready_bits` 中为 1 的位，设置到 `IoTaskHandle` 对应任务的通知值中。

IOTask 取走通知并清除相关位后，下一次半区完成又可以重新设置这些标志。





### 4. 必要时请求任务切换

```c
portYIELD_FROM_ISR(higher_priority_task_woken);
```

如果标志是 `pdTRUE`，这句就请求在中断退出时进行任务切换，让被唤醒的高优先级任务尽快运行；如果是 `pdFALSE`，就不因这次通知请求切换。

结合我们项目：

```
MonitorTask 优先级 1 正在运行
    ↓ DMA 中断
唤醒 IOTask，优先级 2
    ↓ 2 > 1
请求切换到 IOTask
```

如果中断打断的是优先级 3 的 `CanopenTask`，即使唤醒了优先级 2 的 IOTask，也不会因此抢占 CanopenTask。

```c
这次过程是：
CanopenTask（优先级 3）正在运行
    ↓
DMA 中断打断它
    ↓
通知 IOTask（优先级 2）
IOTask 从阻塞状态变成就绪状态
    ↓
中断结束
    ↓
CanopenTask 仍然就绪，优先级更高
所以继续运行 CanopenTask
唤醒任务，只代表它可以参与调度，不代表它马上获得 CPU。
随后 CanopenTask 执行到：
vTaskDelayUntil(&previous_wake, period);
如果需要等待下一周期，它就进入阻塞状态。此时，调度器才会选择就绪任务中优先级最高的任务，例如 IOTask：
CanopenTask 阻塞等待
    ↓
IOTask 获得 CPU
    ↓
处理 ADC、按键和输出
所以这里并不是“有立即切换的需求，但被拒绝了”，而是：
IOTask 已被唤醒，但当前 CanopenTask 优先级更高，因此没有因这次通知立即切换任务的必要。
```

这里也不是在 ISR 里直接调用 `IO_Task()`，任务运行仍然由调度器安排。

整段代码可以记成：

> **先确认 IOTask 存在，再从 DMA 中断给它发通知；如果唤醒了更高优先级任务，就请求中断退出时切换任务。**



### 5.整个流程

```c
完整调用链是：
ADC1 + DMA 开始工作
    ↓
DMA2_Stream0_IRQHandler()
    ↓
HAL_DMA_IRQHandler(&hdma_adc)
    ↓
HAL 库判断 DMA 是半传输完成还是全部传输完成
    ↓
HAL_ADC_ConvHalfCpltCallback()
或
HAL_ADC_ConvCpltCallback()
    ↓
adc_dma_notify_from_isr()
    ↓
vTaskNotifyGiveFromISR(IoTaskHandle, ...)
    ↓
IOTask 被通知
```





# 6： 返回两个按键引脚的电平

```c
static uint8_t read_board_di(void)
{
    uint8_t value = 0u;											//先把返回值uint8_t value清零

    if (HAL_GPIO_ReadPin(KEY1_GPIO_PORT, KEY1_PIN) == GPIO_PIN_SET)//读取 KEY1：
        value |= 0x01u;										   //若电平为高，也就是 GPIO_PIN_SET，就把 value 的最低位设为 1：
    if (HAL_GPIO_ReadPin(KEY2_GPIO_PORT, KEY2_PIN) == GPIO_PIN_SET)//读取 KEY2：
        value |= 0x02u;										   //若 KEY2 引脚为高，就设置 bit1：0x02 = 二进制 0000 0010
    return value;											  //这里用 |= 是按位或并赋值：设置指定的位，同时保留已经设置好的其他位。
}

```

这个函数把两个按键引脚的电平，打包成一个 `uint8_t` 返回，供后续作为 DI 输入状态使用。

| KEY1 电平 | KEY2 电平 | 返回值 | 二进制      |
| --------- | --------- | ------ | ----------- |
| 低        | 低        | `0x00` | `0000 0000` |
| 高        | 低        | `0x01` | `0000 0001` |
| 低        | 高        | `0x02` | `0000 0010` |
| 高        | 高        | `0x03` | `0000 0011` |

要留意：代码判断的是**引脚高电平**，不能仅凭这段代码断定“高电平就是按下”。按下时引脚是高还是低，要看按键电路和 GPIO 上拉/下拉配置。





# 7：这个函数根据 `outputs` 的三个低位，分别控制红、绿、蓝 LED

```c
static void apply_board_do(uint8_t outputs)
{
    LED_R((outputs & 0x01u) ? 0 : 1);
    LED_G((outputs & 0x02u) ? 0 : 1);
    LED_B((outputs & 0x04u) ? 0 : 1);
}
```

`outputs` 可以看作 3 路数字输出状态：

```c
bit0 → 红灯
bit1 → 绿灯
bit2 → 蓝灯
```

例如 `outputs = 0x05`，二进制是 `0000 0101`，表示 bit0 和 bit2 为 1，因此控制红灯和蓝灯。

每行都用掩码检查对应的位：

```
outputs & 0x01u   // 检查 bit0
outputs & 0x02u   // 检查 bit1
outputs & 0x04u   // 检查 bit2
```

以红灯这句为例：

```
LED_R((outputs & 0x01u) ? 0 : 1);
```

它的意思是：

1. 检查 `outputs` 的 bit0；
2. 如果 bit0 非零，三元表达式取 `0`；
3. 如果 bit0 为零，取 `1`；
4. 把结果交给 `LED_R()` 控制红灯。

三行对应关系是：

```
LED_R((outputs & 0x01u) ? 0 : 1);  // bit0 控红灯
LED_G((outputs & 0x02u) ? 0 : 1);  // bit1 控绿灯
LED_B((outputs & 0x04u) ? 0 : 1);  // bit2 控蓝灯
```

例如：

```
outputs = 0x00 → 三个位都为 0 → 三个宏都收到 1
outputs = 0x01 → 只有红灯位为 1 → 红灯宏收到 0
outputs = 0x05 → 红灯和蓝灯位为 1 → 红、蓝宏收到 0，绿灯宏收到 1
```

注意这里输出位为 `1` 时，传给 LED 宏的参数反而是 `0`。这通常是因为 LED 的 GPIO **低电平点亮**：输出位为 1 表示“打开 LED”，代码就给引脚低电平。要确认最终引脚电平和亮灭含义，还要看 `LED_R/G/B` 宏本身如何定义。

函数也声明了 `static`，所以只能在当前 `.c` 文件内部调用。





# 8：真实 I/O 的处理任务

```c
static void IO_Task(void *argument)
{
    /*1：局部变量和任务参数*/
    io_sample_t sample;		 //暂存一次采样结果，含 DI、AI1、AI2。
    uint8_t outputs;		//暂存一条 DO 输出命令。
    uint8_t half;			//选择 ADC DMA 的前半区 0 或后半区 1。
    uint32_t ready_bits;	//接收 ADC 回调传来的半区完成标志。
    BaseType_t notified;	//记录是否收到了任务通知。BaseType_t为long类型。uint32_t

    (void)argument;			//任务创建接口统一要求传一个参数。这里任务不使用它，所以用 (void) 表示有意忽略，避免编译器报未使用参数警告。
    for (;;)			    //循环运行，这是 FreeRTOS 任务的常见写法，表示任务不会执行完后返回，而是持续等待和处理 I/O。
    {
        
        ready_bits = 0u;											    //局部变量
        notified = xTaskNotifyWait(0u, 0xFFFFFFFFUL, &ready_bits,
                                   pdMS_TO_TICKS(10u));
        if (notified == pdPASS && ready_bits != 0u)
        {
            /* 两个位同时到达时优先处理最新完成的后半区。 */
            half = (ready_bits & ADC_READY_SECOND_HALF) ? 1u : 0u;
            if (ADC_Multi_ReadAverage(half, &sample.ai1, &sample.ai2) != 0u)
            {
                sample.di = read_board_di();
                if (xQueueOverwrite(io_sample_queue, &sample) == pdPASS)
                    freertos_io_sample_count++;
                else
                    freertos_io_queue_error_count++;
            }
        }

        while (xQueueReceive(io_output_queue, &outputs, 0u) == pdPASS)
            apply_board_do(outputs);
    }
}
```

它主要做两件事：

```c
收到 ADC DMA 半区完成通知
    ↓
计算 AI1/AI2 平均值，读取 DI
    ↓
把最新采样放进 io_sample_queue

检查 io_output_queue
    ↓
把 CANopen 写入的 DO 状态输出到 LED/GPIO
```





### 1：回顾一下rots的库函数

```c
任务 A
  │
  │ xTaskNotifyWait()
  ↓
“我先睡着，等别人通知我”
  │
  │
  │       任务B / ISR
  │           │
  │           │ 发送通知
  │           ↓
  │      通知任务A
  │           │
  └───────────┘
              ↓
       A 被唤醒继续执行
    
FreeRTOS 给每个任务都准备了一个通知状态。

可以粗略理解成：

TaskA
┌────────────────────┐
│ Notification Value │
│     uint32_t       │
└────────────────────┘

这个东西可以保存一个 uint32_t：

uint32_t notification_value;

其他任务或者中断可以修改/发送这个通知。

例如：

xTaskNotifyGive(TaskAHandle);

相当于：

“告诉TaskA，有事情了！”
```



```c
//xTaskNotifyWait() 作用： 当前任务“等通知”，等到了以后把通知值取出来。
BaseType_t xTaskNotifyWait( uint32_t ulBitsToClearOnEntry,
                             uint32_t ulBitsToClearOnExit,
                             uint32_t *pulNotificationValue,
                             TickType_t xTicksToWait );
返回值BaseType_t ：
返回值		          含义
pdPASS (1)	         在超时前被 xTaskNotify / xTaskNotifyFromISR 唤醒，*pulNotificationValue 已被写入通知值
pdFAIL (0)	         等待超时；或  把ulBitsToClearOnEntry通知值清 0 后本就无事可做。*pulNotificationValue 不会被写入
      
    
四个形参：
① ulBitsToClearOnEntry —— 入口清除掩码 //函数刚被调用时立即生效：把任务通知值中与掩码为 1 的位清零。

假设通知值现在是：1011 0101 ，调用时填的参数是xTaskNotifyWait（0x01, ...）；
原来的通知值1011 0101，清除 bit0，使得任务通知值进入函数时为1011 0100
    
② ulBitsToClearOnExit —— 出口清除掩码//函数返回前生效：把通知值中与掩码为 1 的位清零。这是"处理完即消费"的关键。
xTaskNotifyWait(0x00,0xFF,...);     //进入函数：不清任务通知值任何 bit ，收到通知后，把低 8 位全部清掉
    
    
③ pulNotificationValue —— 出参，拿到通知值    
非 NULL：返回前把"出口清零之后的通知值"写入 *pulNotificationValue。这就是你上一条里那个 ready_bits 位图。
NULL：不关心值，只把它当二值信号量用
    
④ xTicksToWait —— 阻塞时长
    
取值					含义
portMAX_DELAY	     死等，直到被 notify（需 INCLUDE_vTaskSuspend == 1）
0					不阻塞，立即返回
pdMS_TO_TICKS(100)	  最多等 100ms
```





### 2：半传输完成示例

我们按**前半区刚刚传输完成**走一遍。假设：

```
ADC_READY_FIRST_HALF = 0x01
```



##### 1. DMA 半传输完成，中断回调发出标志

HAL 调用：

```
HAL_ADC_ConvHalfCpltCallback(hadc);
```

确认这是 ADC1 后，执行：

```
adc_dma_notify_from_isr(ADC_READY_FIRST_HALF);
```

此时，`adc_dma_notify_from_isr()` 里的参数 `area_flag` 是：

```
area_flag = 0x01
```

接着它调用：

```
xTaskNotifyFromISR(IoTaskHandle, area_flag, eSetBits, ...);
```

FreeRTOS 把 `0x01` 存进 **IOTask 的任务通知值**，并让正在等待通知的 IOTask 准备运行。





##### 2. IOTask 取通知

IOTask 运行到这轮循环开头：

```c
received_flags = 0u;		//局部变量
```

此处清零的是 **IO_Task 自己的局部变量**：

```
IO_Task 的 received_flags = 0
```

之前中断传来的 `0x01` 已经存放在 FreeRTOS 的任务通知值里，因此不会被这句清零。

接着执行：

```
notified = xTaskNotifyWait(0u, 0xFFFFFFFFUL, &received_flags,
                           pdMS_TO_TICKS(10u));
```

FreeRTOS 把它存着的 `0x01` 取出来，写进 `IO_Task` 的局部变量：

```
notified = pdPASS
IO_Task 的 area_flag = 0x01
```

所以才会通过这个判断：

```
if (notified == pdPASS && area_flag != 0u)
```





##### 3. 根据标志选择前半区

接下来：

```c
half = (area_flag & ADC_READY_SECOND_HALF) ? 1u : 0u;
```

当前 `area_flag` 此时是 `0x01`，表示前半区完成；其中没有后半区标志，因此：

```
half = 0
```

之后调用：

```c
ADC_Multi_ReadAverage(0u, &sample.ai1, &sample.ai2);
```

读取 DMA 缓冲区的前半区并计算 AI1、AI2 平均值。

整个过程可以压缩成：

```c
中断回调传入 0x01
    ↓
FreeRTOS 保存任务通知值 0x01
    ↓
IO_Task 把自己的局部 ready_bits 清为 0
    ↓
xTaskNotifyWait 从 FreeRTOS 取出 0x01
    ↓
局部 ready_bits 变成 0x01
    ↓
判断为前半区完成，half = 0
```

所以，**中断侧的 `ready_bits` 是发送内容；任务侧的 `ready_bits` 是接收位置。**它们是不同的局部变量，FreeRTOS 的任务通知值负责把标志从中断侧传到任务侧。





### 3：回顾一下队列库函数

```c
xQueueOverwrite(queue, &data);
拆开：

xQueueOverwrite(
    ① 往哪个队列写
    ② 写入什么数据
);

例如：

uint16_t adc_value = 1234;

xQueueOverwrite(AdcQueue, &adc_value);

意思就是：

把 1234 放进 AdcQueue。
```



```c
你的场景非常适合 xQueueOverwrite()。关键是：队列长度是 1，但队列每个元素可以是一个完整的结构体。

比如你这个结构体可以理解成：

typedef struct
{
    uint8_t  di;      // 1个DI状态
    uint16_t ai1;     // AI通道1
    uint16_t ai2;     // AI通道2
} AcquireData_t;

然后创建队列：

QueueHandle_t AcquireQueue;

AcquireQueue = xQueueCreate(1, sizeof(AcquireData_t));

这里一定要区分两个 1：

xQueueCreate(1, sizeof(AcquireData_t));
             ↑
             队列里最多存 1 个“元素”

                          ↑
                          每个元素的大小是整个结构体

所以内存逻辑是：

AcquireQueue

┌───────────────────────────────┐
│       AcquireData_t           │
│                               │
│  DI    │  AI1    │  AI2      │
│────────┼─────────┼───────────│
│  1个字节 │ 2个字节 │ 2个字节   │
└───────────────────────────────┘

//生产者

比如你的 AcquireTask 采集完一次：

AcquireData_t data;

data.di  = DI_Value;
data.ai1 = AI1_Value;
data.ai2 = AI2_Value;

xQueueOverwrite(AcquireQueue, &data);

第一次：

采集结果：
DI  = 0
AI1 = 1234
AI2 = 2345

        ↓

Queue
┌─────────────────────┐
│ 0 │ 1234 │ 2345     │
└─────────────────────┘

下一次采集：

DI  = 1
AI1 = 1250
AI2 = 2360

        ↓

xQueueOverwrite()

变成：

Queue
┌─────────────────────┐
│ 1 │ 1250 │ 2360     │
└─────────────────────┘

上一组：

0 / 1234 / 2345

就被整个结构体一起覆盖掉了。


 //消费者

比如你的 MonitorTask：

AcquireData_t data;

if (xQueueReceive(AcquireQueue, &data, portMAX_DELAY) == pdPASS)
{
    printf("DI  = %d\n", data.di);
    printf("AI1 = %d\n", data.ai1);
    printf("AI2 = %d\n", data.ai2);
}

它拿到的不是一个单独的 DI 或 AI，而是一次完整采集结果：

             一次采集
                ↓
        ┌─────────────────┐
        │ DI              │
        │ AI1             │
        │ AI2             │
        └─────────────────┘
                ↓
          xQueueOverwrite
                ↓
             Queue
                ↓
          xQueueReceive
                ↓
        MonitorTask

这其实就是一个很典型的 “最新状态快照（snapshot）” 模式。

采集任务不断更新最新的 DI + AI1 + AI2，其他任务永远只关心最近一次完整采集结果，而不需要排队处理历史采样。

所以你之前代码里如果看到：

xQueueOverwrite(AcquireQueue, &data);

现在就可以直接理解成：

“把这一次完整的 DI + 两路 AI 采集结果，作为最新快照放进队列；如果队列里还有上一份，就整份替换掉。”

而不是“只能存一个变量”。队列长度为 1 ≠ 只能存 1 个字节/变量；它是只能存 1 个“结构体元素”。
```



### 4：队列取出元素函数

`xQueueReceive()` = 从队列里面取出一个元素，并把这个元素复制到你指定的变量/结构体里。

```c
xQueueReceive(
    QueueHandle_t xQueue,
    void *pvBuffer,
    TickType_t xTicksToWait
);
还是拆成三个东西：

xQueueReceive(
    ① 从哪个队列取
    ② 取出来放到哪里
    ③ 队列为空时最多等多久
);

```



##### 1. 先看函数

```c
xQueueReceive(
    QueueHandle_t xQueue,
    void *pvBuffer,
    TickType_t xTicksToWait
);
```

还是拆成三个东西：

```c
xQueueReceive(
    ① 从哪个队列取
    ② 取出来放到哪里
    ③ 队列为空时最多等多久
);
```

------



##### 2. 放到你现在这个结构体场景里

你的数据：

```c
typedef struct
{
    uint8_t  di;
    uint16_t ai1;
    uint16_t ai2;
} AcquireData_t;
```

队列：

```c
AcquireQueue = xQueueCreate(1, sizeof(AcquireData_t));
```

生产者：

```c
AcquireData_t data;

data.di  = DI_Value;
data.ai1 = AI1_Value;
data.ai2 = AI2_Value;

xQueueOverwrite(AcquireQueue, &data);
```

这时候队列里面相当于：

```c
AcquireQueue

┌──────────────────────┐
│ DI │ AI1 │ AI2       │
│  1 │ 1250│ 2360      │
└──────────────────────┘
```

------



##### 3. `xQueueReceive()` 就是把它取出来

消费者：

```
AcquireData_t data;

xQueueReceive(
    AcquireQueue,
    &data,
    portMAX_DELAY
);
```

意思：

> 从 `AcquireQueue` 里面取出一个 `AcquireData_t`，复制到 `data`。

于是：

```c
队列                           data变量

┌─────────────────┐            ┌─────────────────┐
│ DI  AI1  AI2    │  ───────→  │ DI  AI1  AI2    │
│ 1   1250 2360   │            │ 1   1250 2360   │
└─────────────────┘            └─────────────────┘
```

所以之后：

```c
printf("%d\n", data.di);
printf("%d\n", data.ai1);
printf("%d\n", data.ai2);
```

就可以访问这一次取出来的数据。

------

##### 4. 注意：`xQueueReceive()` 默认会“取走”数据

这是非常重要的一点。

假设：

```c
Queue：

┌─────────────────┐
│ 1 │ 1250 │ 2360 │
└─────────────────┘
```

执行：

```
xQueueReceive(AcquireQueue, &data, 0);
```

之后：

```c
Queue：

┌─────────────────┐
│      空         │
└─────────────────┘
```

数据已经被取走了。

而：

```
data：

┌─────────────────┐
│ 1 │ 1250 │ 2360 │
└─────────────────┘
```

------

##### 5. 如果我只想“看看”，不想取走呢？

FreeRTOS 有：

```c
xQueuePeek()
```

例如：

```c
xQueuePeek(
    AcquireQueue,
    &data,
    portMAX_DELAY
);
```

它是：

> **看看队列里面的数据，但不把数据拿走。**

所以：

```c
xQueueReceive()
    ↓
取出来 + 从队列删除

xQueuePeek()
    ↓
看一眼 + 数据仍然留在队列
```

这个区别很重要。

------



##### 6. 第三个参数：`portMAX_DELAY`

你经常会看到：

```c
xQueueReceive(
    AcquireQueue,
    &data,
    portMAX_DELAY
);
```

意思是：

> **如果队列现在没有数据，我这个任务就阻塞等待，直到有数据。**

比如：

```c
MonitorTask
     │
     ↓
xQueueReceive()
     │
     │ 队列为空
     ↓
   阻塞
     │
     │
     │       AcquireTask
     │            │
     │            │ xQueueOverwrite()
     │            ↓
     └──────→ 队列有数据
                  │
                  ↓
             MonitorTask
               被唤醒
                  │
                  ↓
             继续执行
```

这就是 FreeRTOS 很典型的：

> **生产者 → 队列 → 消费者**

------

##### 7. 如果不想一直等

可以：

```c
xQueueReceive(
    AcquireQueue,
    &data,
    0
);
```

意思：

> **现在有数据就拿，没有就马上返回。**

或者：

```c
xQueueReceive(
    AcquireQueue,
    &data,
    pdMS_TO_TICKS(100)
);
```

意思：

> **最多等 100 ms。**

------



##### 8. 最后看返回值

这个也别忘了。

通常：

```c
if (xQueueReceive(AcquireQueue, &data, portMAX_DELAY) == pdPASS)
{
    // 成功取到数据
}
```

`pdPASS`：

> 成功取到数据。

如果使用有限等待时间：

```c
if (xQueueReceive(AcquireQueue, &data, pdMS_TO_TICKS(100)) == pdPASS)
{
    // 收到了
}
else
{
    // 100ms内没有收到
}
```

------



##### 你现在这几个 API 可以串起来了

你的项目场景可以直接记成：

```c
AcquireTask
     │
     │ 采集
     ↓
┌─────────────────────┐
│ DI + AI1 + AI2      │
└─────────────────────┘
     │
     │ xQueueOverwrite()
     ↓
┌─────────────────────┐
│     Queue           │
│  最新的一组数据      │
└─────────────────────┘
     │
     │ xQueueReceive()
     ↓
MonitorTask
```

所以你可以用一句话把两个函数记住：

> **`xQueueOverwrite()`：把最新的一组数据塞进去，旧的可以覆盖。**
>
> **`xQueueReceive()`：把队列里的这一组数据取出来，复制到我的结构体变量里。**

而你这个 **DI + 两个 AI 的结构体**，就是一个完整的“数据包/数据快照”，`xQueueReceive()` 一次把整个结构体取出来。





### 5：完整for循环处理链路

```c
    for (;;)
    {
        received_flags = 0u; // 局部判断变量，先清0
        notified = xTaskNotifyWait(0u, 0xFFFFFFFFUL, &received_flags,
                                   pdMS_TO_TICKS(10u));
        if (notified == pdPASS && received_flags != 0u)
        {
            /* 两个位同时到达时优先处理最新完成的后半区。 */
            half = (received_flags & ADC_READY_SECOND_HALF) ? 1u : 0u;
            if (ADC_Multi_ReadAverage(half, &sample.ai1, &sample.ai2) != 0u)
            {
                sample.di = read_board_di();
                if (xQueueOverwrite(io_sample_queue, &sample) == pdPASS)
                    freertos_io_sample_count++;
                else
                    freertos_io_queue_error_count++;
            }
        }

        while (xQueueReceive(io_output_queue, &outputs, 0u) == pdPASS)
            apply_board_do(outputs);
    }
```



```c
按运行顺序梳理：
1. 每轮循环开始，先执行：
   received_flags = 0u;
   这只是把 IO_Task 的本地接收变量清零，防止超时时误用上一次的值。
2. xTaskNotifyWait() 最多等 10 ms。如果收到中断发来的通知，FreeRTOS 会把通知值写入 received_flags，并返回 pdPASS。这个值是通过任务通知传过来的，不是直接给变量赋值。若等到超时，则不会处理 ADC 数据，但循环还会继续检查 DO 输出队列。
3. 前半区完成时，收到的是 ADC_READY_FIRST_HALF，值为 0x01；后半区标志 ADC_READY_SECOND_HALF 的值是 0x02，不是 0。所以前半区通知下：
   received_flags & ADC_READY_SECOND_HALF
   结果是 0，三元表达式选择 half = 0。若后半区完成，结果非零，就选择 half = 1。若两个标志同时到达，则代码优先选后半区。
4. half = 0 时，ADC_Multi_ReadAverage() 读取 adc_buf[0] 到 adc_buf[63]，把其中 32 个 AI1 值和 32 个 AI2 值分别求平均，结果写入 sample.ai1、sample.ai2。这里保存的是平均值，不是 32 个原始数据。
5. 计算成功后，read_board_di() 读取当前 DI，写入 sample.di。此时 sample 结构体包含：
   DI、AI1 平均值、AI2 平均值
   xQueueOverwrite(io_sample_queue, &sample) 把整个结构体复制进 io_sample_queue。成功就增加采样计数，失败就增加队列错误计数。之后由 Canopen_Task 从这条队列取出 sample。
6. 最后的 while 检查的是另一条队列 io_output_queue。它从中取出一个 DO 输出字节，放进 uint8_t outputs，再交给 apply_board_do() 控制灯。outputs 不是结构体，因为这条队列传的是 DO 位掩码；采样结构体 sample 则走 io_sample_queue。
可以记成两个方向：
IO_Task → io_sample_queue：{DI, AI1, AI2} → Canopen_Task

Canopen_Task → io_output_queue：DO 字节 → IO_Task → 控灯
因此，你原先理解的采样部分基本正确；需要改的是：ADC_READY_SECOND_HALF 的值是 0x02，以及 while 取出的 outputs 来自 DO 输出队列，不是刚才的 sample 结构体。
```







# 9：Canopen_Task（）任务

```c
static void Canopen_Task(void *argument)
{
    can_frame_t frame;                           // 用于暂存从 CAN 接收队列取出的报文
    io_sample_t sample;                          // 暂存 io_sample_queue 里的结构体，包含 DI、AI1、AI2。
    uint8_t outputs;                             // 从对象字典读出的 DO 位掩码，是一个 uint8_t型变量
    uint8_t last_outputs = 0xFFu;                // 记住上次送给硬件的 DO 值，用来判断是否变化。
    TickType_t previous_wake;                    // 上次唤醒时间戳，配合 vTaskDelayUntil()，让任务按 1 ms 周期运行。
    const TickType_t period = pdMS_TO_TICKS(1u); // previous_wake 和 period：配合 vTaskDelayUntil()，让任务按 1 ms 周期运行。

    (void)argument;                      // argument 是 FreeRTOS 任务函数统一要求的参数，但本任务不用它，所以明确忽略。
    previous_wake = xTaskGetTickCount(); // 读取当前系统 tick，作为周期延时的起点。
    for (;;)
    {
        /*1. 把接收队列里的 CAN 帧处理完*/
        while (CAN_ReceiveFrame(&frame)) // 只要 CAN 接收队列里还有帧，CAN_ReceiveFrame() 就取出一帧放进 frame
            dispatch_frame(&frame);      // 然后交给 dispatch_frame() 分发，这里用 while，所以一轮任务会把当前积压的报文都处理掉；队列空了，循环才结束。

        /*2. 取出最新 I/O 采样并更新 CANopen 输入*/
        if (xQueueReceive(io_sample_queue, &sample, 0u) == pdPASS)                        // ADC及DMA采样结束唤醒了IO_Task，将最新的DI，AI1，AI2放进了 io_sample_queue 队列里，这里取出。
            remember_status(Canopen_App_UpdateInputs(sample.di, sample.ai1, sample.ai2)); // 调用Canopen_App_UpdateInputs更新到我们的字典对象条目里

        /*3. 推进 Heartbeat 和 PDO   ：每轮循环都调用这两个处理函数，并传入经过的时间 1u，表示按 1 ms 推进它们内部的定时逻辑。*/
        remember_status(Canopen_App_ProcessHeartbeat(1u)); // Heartbeat 到期时，处理发送节点状态报文；
        remember_status(Canopen_App_ProcessPdo(1u));       // PDO 条件满足时，处理 TPDO 上报或相关 PDO 状态。

        /*4. DO 状态变化时，通知 I/O 任务更新硬件*/
        if (Canopen_App_GetOutputs(&outputs) == CO_OK && outputs != last_outputs)
        {
            last_outputs = outputs;                                   // 记下新值，避免后续每个 1 ms 周期都重复发送相同的 DO 状态
            if (xQueueOverwrite(io_output_queue, &outputs) != pdPASS) // 把这个 uint8_t DO 值写入 io_output_queue。之后 IO_Task 从这条队列取出它，并调用apply_board_do(outputs) 控制灯或 GPIO。
                freertos_io_queue_error_count++;                      // 如果队列写入失败，计数器加 1
        }
        /*5. 等到下一个 1 ms 周期*/
        vTaskDelayUntil(&previous_wake, period);
    }
}
```

这个 `Canopen_Task()` 是 CANopen 主任务：每轮先处理收到的 CAN 帧，再取最新的 I/O 采样更新协议对象，然后推进 Heartbeat 和 PDO，最后把 DO 状态变化交给 `IO_Task` 输出。

```c
正好是采样方向的反向链路：
IO_Task ──{DI, AI1, AI2}──→ io_sample_queue ──→ Canopen_Task

Canopen_Task ──DO 字节──→ io_output_queue ──→ IO_Task 控制硬件
```

```c#
整体可以记成：
每约 1 ms：
处理所有待收 CAN 帧
    ↓
取一份 I/O 采样并更新对象字典
    ↓
推进 Heartbeat、PDO 定时逻辑
    ↓
DO 值变化时通知 IO_Task
    ↓
等待下一个周期
```

其中两个队列最关键：`io_sample_queue` 把真实输入送给 CANopen，`io_output_queue` 把 CANopen 的 DO 命令送给硬件





### 1：更新链路及上报逻辑

```c
if (xQueueReceive(io_sample_queue, &sample, 0u) == pdPASS)
 remember_status(Canopen_App_UpdateInputs(sample.di, sample.ai1, sample.ai2));
//关键点是：DI 或 AI 的值变化本身不会直接唤醒 IO_Task。
```

当前链路是按 ADC DMA 半区完成来采样：

```c
DMA 半区完成
    ↓
通知 IO_Task
    ↓
计算 AI1/AI2 平均值，并读取当前 DI
    ↓
把 {DI, AI1, AI2} 覆盖写入 io_sample_queue
    ↓
Canopen_Task 取出这一组采样
    ↓
Canopen_App_UpdateInputs() 更新对象字典
```

所以，即使输入值没有变化，`IO_Task` 收到 DMA 通知后仍会采样并写队列；如果按键或电位器在两次采样之间变化，下一次采样会读到新值。

队列长度是 1，`xQueueOverwrite()` 会保留最新的一组采样。`Canopen_Task` 每轮以非阻塞方式取它：队列里有数据就更新对象字典，没有数据就跳过这次更新。

对象字典更新后，后续 PDO 处理会判断输入是否变化，并按 PDO 状态和配置决定是否上报。也就是说，**采样和写对象字典是周期驱动的，TPDO 才会根据变化及配置决定发送**。





# 10：Monitor_Task（）任务

```c
static void Monitor_Task(void *argument)
{
    uint32_t previous_sample_count = 0u; // 保存上一次采样计数

    (void)argument; // argument 是 FreeRTOS 任务函数统一要求的参数，但本任务不用它，所以明确忽略。
    for (;;)        // 任务函数不会结束。每轮检查一次，然后延时约 1 秒，再开始下一轮。
    {
        freertos_monitor_tick_count++; // 这个计数器每次进入监视任务循环就加 1，表示 Monitor 运行过多少轮。它主要用于观察任务是否还活着
        /*判断 I/O 采样是否停滞*/
        if (freertos_io_sample_count == previous_sample_count) // 如果本次检查时采样计数和上次一样，说明从上一次检查到现在，没有新的采样成功写入队列，于是增加错误计数。
        {
            freertos_io_queue_error_count++;
        }
        /*更新比较基准*/
        previous_sample_count = freertos_io_sample_count; // 把本次计数保存下来，作为下一轮比较基准。
        /*延时 1 秒 */
        vTaskDelay(pdMS_TO_TICKS(1000u)); // 把 1000 ms 转换为 FreeRTOS tick，然后任务进入阻塞态约 1 秒。它不是一直占 CPU，延时期间会让其他就绪任务运行。
    }
}
```

这个 `Monitor_Task()` 是一个**低频运行的 I/O 活跃度监视任务**，它不采集、不控制灯，只检查 `IOTask` 是否持续产生新的采样。

整体流程：

```c
Monitor_Task 运行
    ↓
monitor_tick_count++
    ↓
比较本次和上次 I/O 采样计数
    ↓
没有新采样 → 错误计数加一
    ↓
保存本次采样计数
    ↓
阻塞约 1 秒
    ↓
回到 for 循环重新检查
```

它监视的是：**IOTask 有没有持续产生采样**，不是监视按键或 ADC 的具体数值是否变化。即使 DI、AI 数值完全不变，只要 IOTask 持续采样并成功写队列，`freertos_io_sample_count` 仍会递增，Monitor 就不会报这个异常。



### 示例：

可以把它看成“每隔 1 秒拍一张采样计数的照片，然后和上一张比较”。

假设任务启动时：

```c
freertos_monitor_tick_count = 0;
freertos_io_sample_count = 0;
previous_sample_count = 0;
freertos_io_queue_error_count = 0;

//第一次进入 Monitor_Task
freertos_monitor_tick_count++; // freertos_monitor_tick_count 由0→1
然后比较：
freertos_io_sample_count == previous_sample_count
					 0 == 0
条件成立，说明这一秒内还没有成功采样，于是：freertos_io_queue_error_count = 1 //可以表示采样出错，也可以表示为还未采样
保存本次采样计数：previous_sample_count = freertos_io_sample_count; //    previous_sample_count依旧为0
然后任务阻塞约 1 秒，假设这一秒中 IOTask 成功写入了 30 次采样：freertos_io_sample_count = 30
    
//第二次进入监视任务
freertos_monitor_tick_count++;// freertos_monitor_tick_count 由1→2
比较：freertos_io_sample_count == previous_sample_count //30≠ 0
条件不成立，说明采样在正常增长，不增加错误计数，freertos_io_queue_error_count依旧为1
随后：previous_sample_count = freertos_io_sample_count;//    previous_sample_count由0→30
然后任务阻塞约 1 秒
假设下一秒 IOTask 因为 ADC 或队列故障没有成功写入新采样：
freertos_io_sample_count = 30
previous_sample_count = 30
    
    
//第三次进入监视任务
freertos_monitor_tick_count++;
结果：
freertos_monitor_tick_count = 3
比较：
30 == 30
条件成立，于是：
freertos_io_queue_error_count = 2
这里第二次错误表示：
最近这 1 秒没有新的 I/O 采样成功写入队列。
    
//总结
所以这几个变量的含义是：
freertos_monitor_tick_count		//Monitor_Task 执行了多少轮
    	

freertos_io_sample_count		//IOTask 成功写入采样队列多少次
    

previous_sample_count		//上一次 Monitor_Task 检查时记录的采样次数
    		

freertos_io_queue_error_count // 采样队列失败或采样停滞的累计次数
   
需要注意，第一次检查时如果系统刚启动、ADC 还没产生第一批数据，也可能被记录一次错误。这更像是“启动阶段尚未采样”，不一定是真故障。
```



# 11：Diagnostics_Task（）任务

```c
static void Diagnostics_Task(void *argument)
{
    (void)argument; // FreeRTOS 任务统一形参，本任务未使用，显式忽略以消除告警
    for (;;)        // 任务永不结束：每秒做一次诊断快照，然后阻塞约 1 秒
    {
        freertos_diagnostics_last_error = canopen_last_error;     // CANopen 最近一次错误（CO_OK 表示无错误）
        freertos_diagnostics_rx_overflow = can_rx_overflow_count; // CAN 接收队列溢出（丢帧）累计次数
        freertos_diagnostics_tx_busy = can_tx_busy_count;         // CAN 发送邮箱全忙（报文未发出）累计次数
        freertos_diagnostics_can_error = can_error_count;         // HAL CAN 错误回调触发累计次数
        freertos_diagnostics_bus_off = can_bus_off_count;         // CAN Bus-off 发生累计次数（<= can_error）

        vTaskDelay(pdMS_TO_TICKS(1000u)); // 阻塞约 1 秒，让出 CPU 给其他就绪任务
    }
}
//这里要特别注意：这些计数器是累计值，诊断任务不会清零它们。
```

这个任务的作用是：**每隔约 1 秒，把 CANopen 和 CAN 驱动层的运行状态复制成一份诊断快照**，方便调试器、监控代码或后续对象字典读取。





# 12：freertos_demo（）函数

```c
void freertos_demo(void)
{
    /* 1. 初始化 CANopen 应用层和对象字典。 */
    if (Canopen_App_Init() != CO_OK)
        freertos_demo_halt(); // 仅为了任务上下文可以继续运行，因此保留在此处等待调试器定位原因

    /*
     * 2. 创建两条长度为 1 的队列：
     *    - io_sample_queue：保存最新的一份 DI/AI 采样结构体；
     *    - io_output_queue：保存最新的一份 DO 输出字节。
     *    队列长度为 1 是因为应用只关心最新 I/O 状态。
     */
    io_sample_queue = xQueueCreate(1u, sizeof(io_sample_t));
    io_output_queue = xQueueCreate(1u, sizeof(uint8_t));
    if (io_sample_queue == NULL || io_output_queue == NULL)
        freertos_demo_halt(); // 仅为了任务上下文可以继续运行，因此保留在此处等待调试器定位原因

    /*
     * 3. 创建 CANopen 主任务。
     *    优先级最高，负责接收 CAN 帧、推进协议定时器、处理 PDO/Heartbeat，
     *    并把新的 DO 输出值交给 IOTask。
     */
    if (xTaskCreate(Canopen_Task, "CanopenTask", CANOPEN_TASK_STACK_SIZE,
                    NULL, CANOPEN_TASK_PRIORITY, &CanopenTaskHandle) != pdPASS)
        freertos_demo_halt(); // 仅为了任务上下文可以继续运行，因此保留在此处等待调试器定位原因

    /*
     * 4. 创建 I/O 任务。
     *    由 ADC DMA 通知唤醒，读取 DI/AI，并执行 CANopen 下发的 DO 输出。
     */
    if (xTaskCreate(IO_Task, "IOTask", IO_TASK_STACK_SIZE, NULL,
                    IO_TASK_PRIORITY, &IoTaskHandle) != pdPASS)
        freertos_demo_halt(); // 仅为了任务上下文可以继续运行，因此保留在此处等待调试器定位原因

    /* 5. 创建 I/O 采样监视任务，每约 1 秒检查采样计数是否仍在增长。 */
    if (xTaskCreate(Monitor_Task, "MonitorTask", MONITOR_TASK_STACK_SIZE,
                    NULL, MONITOR_TASK_PRIORITY, &MonitorTaskHandle) != pdPASS)
        freertos_demo_halt(); // 仅为了任务上下文可以继续运行，因此保留在此处等待调试器定位原因

    /* 6. 创建 CAN/CANopen 诊断任务，每约 1 秒刷新一次诊断快照。 */
    if (xTaskCreate(Diagnostics_Task, "DiagnosticsTask",
                    DIAGNOSTICS_TASK_STACK_SIZE, NULL,
                    DIAGNOSTICS_TASK_PRIORITY, &DiagnosticsTaskHandle) != pdPASS)
        freertos_demo_halt(); // 仅为了任务上下文可以继续运行，因此保留在此处等待调试器定位原因

    /*
     * 7. 启动调度器。
     *    从这里开始由 FreeRTOS 决定任务运行顺序；正常情况下该函数不会返回。
     */
    vTaskStartScheduler();

    /* 如果执行到这里，说明调度器启动失败或意外返回。 */
    freertos_demo_halt(); // 仅为了任务上下文可以继续运行，因此保留在此处等待调试器定位原因
}
```



```c
freertos_demo()
    │
    ├─ 1. Canopen_App_Init()
    │      初始化 CANopen 应用层和对象字典
    │
    ├─ 2. 创建 io_sample_queue
    │      长度为 1
    │      存放 io_sample_t：DI、AI1、AI2
    │
    ├─ 3. 创建 io_output_queue
    │      长度为 1
    │      存放 uint8_t：DO 输出值
    │
    ├─ 4. 创建 CanopenTask
    │      优先级 3
    │
    ├─ 5. 创建 IOTask
    │      优先级 2
    │
    ├─ 6. 创建 MonitorTask
    │      优先级 1
    │
    ├─ 7. 创建 DiagnosticsTask
    │      优先级 1
    │
    └─ 8. vTaskStartScheduler()
           启动 FreeRTOS 调度器
```

要注意：调用 `xTaskCreate()` 时，任务只是被创建并进入就绪列表，**还没有马上运行**。因为此时调度器还没有启动。

等执行到：

```c
vTaskStartScheduler();
```

FreeRTOS 才正式接管 CPU，并选择最高优先级的 `CanopenTask` 先运行。

如果某一步失败，就进入：

```c
freertos_demo_halt();
```

也就是无限循环停在那里，方便使用调试器查看是哪一步初始化失败。



# 13：项目代码运行逻辑

当然，我们再把**进入稳定运行后，代码实际上怎样循环**完整串一次。最重要的是：`CanopenTask` 和 `IOTask` 都不会一轮执行完就退出；它们都在 `for (;;)` 中循环，只是每轮会在延时或等待函数里让出 CPU。





### 1：`CanopenTask`每约 1 ms 循环一轮

优先级是 3，最高。每轮依次做四件事：

```c
① 处理 CAN 接收环形队列中的报文
② 有新采样时，取出 {DI, AI1, AI2} 并更新对象字典
③ 推进 Heartbeat 和 PDO 的定时处理
④ DO 值变化时，把 DO 字节放进 io_output_queue
```

随后调用 `vTaskDelayUntil()`，进入阻塞状态，等到下一个约 1 ms 的周期。时间到了，任务变为就绪，调度器安排它继续执行；从延时函数返回后，它回到 `for` 循环开头，开始下一轮。

```
四件事做完 → 延时到下一周期 → 恢复运行 → 回到第一件事
```

因此它是**每约 1 ms 开始新一轮**，不是每 1 ms 重新创建任务。每轮实际执行了多久目前没有测量。





### 2：`IOTask`等待通知或超时，然后继续循环

优先级是 2。每轮开头先清本地变量，再等待：

```
received_flags = 0u;
notified = xTaskNotifyWait(..., pdMS_TO_TICKS(10u));
```

执行到 `xTaskNotifyWait()` 时，若没有 ADC 通知，IOTask 会在这个函数里阻塞，最长 10 ms。之后有两种情况：

```c
//情况1
IOTask 正在运行
    ↓ 调用 xTaskNotifyWait，没有通知
进入阻塞态，等待通知或超时
    ↓ 通知到达 / 10 ms 超时
变为就绪态
    ↓ 调度器分配 CPU
回到运行态，xTaskNotifyWait 返回
    ↓ 
    → 计算对应半区的 AI1/AI2 平均值
    → 读取 DI
    → 把 {DI, AI1, AI2} 覆盖写入 io_sample_queue
	→ 处理DO
	
//情况2
10 ms 超时
    → 等待结束，但没有 ADC 新采样
    → 跳过 AI/DI 采样处理
    → 处理DO
```

两种情况之后都会继续检查 `io_output_queue`，取出 DO 值并调用 `apply_board_do()` 控制 LED。处理完到达 `for` 循环末尾后，**回到循环开头**，再次清零 `received_flags`，再次等待。

所以：

- DMA 半区完成约每 32 ms 通知一次，这是 ADC 数据处理的节奏；
- 10 ms 是一次通知等待的最长时间，超时后也会继续检查 DO 队列；
- `IOTask` 没有固定的 1 ms 或 32 ms 任务周期。





### 3：ADC、DMA 与中断在后台做什么

TIM3 每 1 ms 触发 ADC 扫描 AI1、AI2。CPU 正在执行任务时，ADC 和 DMA 仍由硬件持续工作。

DMA 写满一个半区后产生中断。中断回调通知 IOTask 哪个半区已经完成。**通知会让 IOTask 变为就绪，但不一定马上运行**：如果此时优先级更高的 `CanopenTask` 正在运行，IOTask 要等 CanopenTask 延时或阻塞后再获得 CPU。通知会保留给 IOTask，没能立即运行不等于通知丢失。







### 4：两个低优先级任务

`MonitorTask` 和 `DiagnosticsTask` 优先级都是 1，每约 1 秒执行一次短暂检查，然后各自延时 1 秒。它们没有固定的 CPU 时段；只有优先级 3、2 的任务都没有就绪运行时，才轮到它们。没有就绪任务时，FreeRTOS 运行空闲任务。

Heartbeat 也不是一个独立任务：`CanopenTask` 每轮推进 Heartbeat 计时约 1 ms，默认 `0x1017:00=1000 ms`，计时到期才发送一帧。Node-ID 1 在 Operational 状态时，典型帧是 `0x701`，数据字节 `0x05`。

整体运行关系可以这样记：

```c
CanopenTask：四项协议/I-O协调工作 → 延时 → 回到循环开头
IOTask：等待通知或超时 → 处理 ADC/DI、检查 DO → 回到循环开头
DMA 中断：半区完成时通知 IOTask
Monitor/Diagnostics：低优先级周期检查
```

“通知到达”表示 IOTask 已经就绪；“获得 CPU”还要由调度器根据优先级和当前任务状态决定。







### 5：上位机操作时，代码是怎么执行的



##### 1：上位机发送 `01 01`，节点进入 Operational

这帧应是 **CAN-ID `0x000`，DLC=2，数据 `01 01`**：

```
data[0] = 0x01：NMT Start 命令
data[1] = 0x01：目标节点 Node-ID 1
```

运行路径是：

```c
上位机发送 NMT 帧
    ↓
CAN 硬件收到帧，触发 CAN RX 中断
    ↓
bsp_can.c 的 HAL_CAN_RxFifo0MsgPendingCallback()
    ↓
中断把帧放入软件环形队列，尽快退出
    ↓
CanopenTask 下一轮取出帧
    ↓
dispatch_frame()
    ↓
Canopen_App_NmtReceive()
    ↓
co_nmt_receive()
    ↓
节点状态改为 Operational
```

CAN 接收中断只把帧放进队列，**不在中断里执行 NMT 协议处理**。`CanopenTask` 每约 1 ms 检查接收队列，因此收到帧后会在它下一次取队列时处理。

进入 Operational 后，这轮任务接着调用 `Canopen_App_ProcessPdo(1u)`。PDO 服务发现节点刚进入 Operational，会先发送一次 TPDO1 和 TPDO2，公布当前输入状态。之后才按 PDO 规则处理输入变化和事件周期。



##### 2：按下 KEY1 或 KEY2，DI 如何上报

当前按键映射是：

```c
KEY1 = PA0 → DI bit0
KEY2 = PC13 → DI bit1
```

`Key.h` 标明按键按下为高电平。`read_board_di()` 读到引脚高电平时设置对应位：

```c
KEY1 按下 → DI = 0x01
KEY2 按下 → DI = 0x02
两键都按下 → DI = 0x03
```

不过按键本身**不会直接让 `IO_Task` 运行**。在当前这条代码路径里，`IO_Task` 是由 ADC DMA 半区完成通知唤醒的：

```c
TIM3 每 1 ms 触发 ADC 扫描
    ↓
DMA 写满一个半区，约 32 ms
    ↓
ADC DMA 回调通知 IOTask
    ↓
IOTask 计算 AI1/AI2 平均值，并读取此刻的按键电平
    ↓
把 {DI, AI1, AI2} 写入 io_sample_queue
```

比如你在两次采样之间按下 KEY1，下一次 DMA 半区完成后，`IO_Task` 读到 `DI=0x01`，再把这组 DI/AI 数据交给队列。

之后的路径是：

```c
CanopenTask 取出 io_sample_queue 中的 sample
    ↓
Canopen_App_UpdateInputs(sample.di, sample.ai1, sample.ai2)
    ↓
更新对象字典中的 DI、AI1、AI2
    ↓
Canopen_App_ProcessPdo(1u) 检查输入变化
    ↓
若处于 Operational 且 DI 变化，发送 TPDO1（节点1的 CAN-ID 0x181）
```

所以按键变化是**采样时读到的**，随后被写入对象字典；在 Operational 状态下，PDO 服务再据此决定是否发送 TPDO1。



完整链路：

```c
ADC DMA 回调通知 IOTask
    ↓
IOTask 采到新的 DI
    ↓
CanopenTask 从队列取出采样
    ↓
Canopen_App_UpdateInputs() 更新对象字典
    ↓
CanopenTask 调用 Canopen_App_ProcessPdo(1u)
    ↓
co_pdo_process() 将当前 DI 和上次记录的 last_di 比较
    ↓
DI 有变化且节点处于 Operational → 发送 TPDO1
    
//真正判断 DI 变化并发送 TPDO1 的是Canopen_App_ProcessPdo函数里面调用的 co_pdo_process()
remember_status() 只负责记录错误状态。按下和松开如果都被采样到，通常都会各产生一次 DI 状态变化上报。
```





##### 3：上位机发 DO 数据，让 LED 改变颜色

比如上位机发：

```c
CAN-ID = 0x201
DLC    = 1
DATA   = 0x01
```

`0x201` 是节点 1 的 RPDO1。数据 `0x01` 表示 DO 的 bit0 为 1。接收和执行路径是：

```c
CAN RX 中断将帧放入接收环形队列
    ↓
CanopenTask 取出帧并调用 dispatch_frame()
    ↓
Canopen_App_PdoReceive()
    ↓
co_pdo_receive() 检查节点处于 Operational、DLC 和数据是否合法
    ↓
把 DO 值写入对象字典 0x6200:01
    ↓
CanopenTask 调用 Canopen_App_GetOutputs() 读出 DO
    ↓
DO 值变化，写入 io_output_queue
    ↓
IO_Task 取出 outputs 并调用 apply_board_do()
    ↓
改变 LED GPIO
```

当前 LED 位映射是：

| DO 数据位 | LED  |
| --------- | ---- |
| bit0      | 红   |
| bit1      | 绿   |
| bit2      | 蓝   |

LED 是低电平点亮，所以输出位为 1 时，代码传给对应 LED 宏的参数是 0。例如：

```c
DO = 0x01 → 红灯亮
DO = 0x02 → 绿灯亮
DO = 0x04 → 蓝灯亮
DO = 0x05 → 红灯和蓝灯亮
```

另外，`IO_Task` 检查 DO 队列是在等待 ADC 通知之后。它最多等 10 ms 就会继续检查队列，因此 DO 命令到 LED 有一点任务调度延迟；这不是 CAN 中断直接控制灯，而是 `CanopenTask` 更新对象字典，再经队列交给 `IO_Task` 控灯。

这三个动作可以浓缩为：

```c
NMT：CAN 帧 → CAN RX 中断入队 → CanopenTask → NMT 状态变化

按键：ADC DMA 通知 → IOTask 读 DI/AI → 采样队列 → CanopenTask 更新对象字典 → TPDO 上报

DO：RPDO CAN 帧 → CAN RX 中断入队 → CanopenTask 写对象字典 → 输出队列 → IOTask 控制 LED
```