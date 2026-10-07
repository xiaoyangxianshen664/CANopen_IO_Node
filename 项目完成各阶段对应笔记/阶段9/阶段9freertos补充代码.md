# 1：process_reliability（）函数

它实际上只干 4 件事：

```c
process_reliability()
│
├── ① 看有没有新的故障
│      ├─ 看门狗复位
│      ├─ Bus-off
│      ├─ RX 溢出
│      ├─ CAN Error
│      └─ ADC 停滞
│
├── ② 如果有故障 → 上报 EMCY
│
├── ③ 已经处于故障状态
│      ├─ 安全输出
│      ├─ EMCY 重试
│      └─ 判断是否恢复
│
└── ④ 连续健康足够久
       └─ 清除 EMCY → 恢复正常
```

记住这个**状态机**

```c
			发现故障
                ↓
        ┌───────────────┐
        │  故障活动状态  │
        └───────┬───────┘
                ↓
          安全输出 = 1
                ↓
         ProcessEmcy()
                ↓
       持续检查通信/I/O
                ↓
       ┌───────────────┐
       │ 是否持续健康？ │
       └───────┬───────┘
          否 ↓       ↓ 是
       计时归零     累加恢复时间
                        ↓
              达到稳定时间？
                  ↓ 是
              ClearEmcy()
                  ↓
          故障活动 = 0
          安全输出 = 0
```

这个函数本质上就是：

> 发现故障 → 上报或重试 EMCY → 输出进入安全状态 → 等待系统连续稳定 → 发送 EMCY 清除帧 → 解除安全输出。





### 1：变量

```c
/*1. 运行与监视计数*/
volatile uint32_t freertos_io_sample_count;        // 成功写入I/O采样队列的次数
volatile uint32_t freertos_monitor_tick_count;     // MonitorTask运行的循环次数
static volatile uint32_t canopen_task_cycle_count; // CANopenTask循环运行次数，用于任务健康监测
static volatile uint32_t io_task_cycle_count;      // IOTask循环运行次数，用于任务健康监测

/*2. 诊断信息*/
volatile co_status_t freertos_diagnostics_last_error = CO_OK; // 最近记录的诊断错误状态
volatile uint32_t freertos_diagnostics_rx_overflow;           // CAN 接收队列溢出（丢帧）累计次数
volatile uint32_t freertos_diagnostics_tx_busy;               // CAN 发送邮箱全忙（报文未发出）累计次数
volatile uint32_t freertos_diagnostics_can_error;             // 进了错误中断后，还进入错误回调的次数
volatile uint32_t freertos_diagnostics_bus_off;               // CAN Bus-off 发生累计次数
static volatile uint32_t freertos_io_queue_error_count;       // I/O队列操作失败或采样停滞的累计次数
static volatile uint8_t io_sampling_stalled;                  // 当前是否检测到 I/O 采样停滞。

/*3. 故障处理与恢复状态*/
static uint8_t reliability_fault_active; // 是否正在处理活动故障
static uint8_t reliability_wait_for_rx;  // Bus-off 后是否还在等待新 CAN 帧，以确认通信恢复
static uint32_t reliability_rx_baseline; // Bus-off 发生时的接收帧计数基准。
static uint32_t reliability_recovery_ms; // 连续满足恢复条件的计时值。

/*4：上次检查到的 CAN 错误计数，用于发现计数是否增加。*/
static uint32_t observed_rx_overflow_count; // 上次处理时观察到的CAN接收溢出计数
static uint32_t observed_can_error_count;   // 上次处理时观察到的CAN错误计数
static uint32_t observed_bus_off_count;     // 上次处理时观察到的Bus-off计数

/*5. 看门狗复位记录*/
static uint8_t watchdog_reset_pending; // 看门狗复位待上报标志，非0表示需报告看门狗复位故障
```

```c
process_reliability()
│
├── ① 系统跑得怎么样？
│      freertos_io_sample_count
│      freertos_monitor_tick_count
│      canopen_task_cycle_count
│      io_task_cycle_count
│
├── ② 最近出过什么问题？
│      freertos_diagnostics_last_error
│      freertos_diagnostics_rx_overflow
│      freertos_diagnostics_tx_busy
│      freertos_diagnostics_can_error
│      freertos_diagnostics_bus_off
│      freertos_io_queue_error_count
│      io_sampling_stalled
│
├── ③ 现在是不是正在处理故障？
│      reliability_fault_active
│      reliability_wait_for_rx
│      reliability_rx_baseline
│      reliability_recovery_ms
│
├── ④ 这一轮有没有“新错误”？
│      observed_rx_overflow_count
│      observed_can_error_count
│      observed_bus_off_count
│
└── ⑤ 开机是不是因为看门狗复位？
       watchdog_reset_pending
```







------

# 总览

你可以把它压缩成下面这个版本：

```c
读取看门狗复位待上报标志
    ↓
比较 CAN 错误累计值与上次记录值，找出本轮新错误
    ↓
按优先级处理一种新故障：
Bus-off → CAN 接收溢出 → 一般 CAN 错误 → I/O 采样停滞
    ↓
若没有活动故障：本轮返回
若有活动故障：
    打开安全输出 → 处理或重试待发 EMCY
    ↓
检查本轮是否健康healthy
    ├─ I/O 采样正常
    ├─ 本轮没有新的 CAN 错误
    └─ 若发生过 Bus-off，还要收到新的 CAN 帧
    ↓
节点处于 Operational 且满足健康条件？
    ├─ 否：恢复计时清零
    └─ 是：恢复计时加 1（约 1 ms）
    ↓
计时达到 1000 ms
    ↓
尝试清除 EMCY
    ↓
清除流程认为完成后，清除本地故障状态并解除安全输出
```





### 实例：

我们用 **CAN 接收队列溢出** 举例，从节点正常运行开始。假设节点 ID 是 `1`，节点处于 Operational，EMCY 发送成功，且 `FAULT_RECOVERY_STABLE_MS = 1000`。具体阈值以工程里的宏定义为准。

### 1. 正常运行

此时没有可靠性故障：

```c
reliability_fault_active = 0
reliability_recovery_ms = 0
can_rx_overflow_count = 0
observed_rx_overflow_count = 0
```

`Canopen_Task` 周期运行，调用 `process_reliability()`。计数相同，所以：

```
new_rx_overflow = 0
```

没有新故障，`reliability_fault_active` 仍为 `0`，函数直接返回。不会进入安全输出和 EMCY 处理。



### 2. CAN 接收队列发生溢出

CAN 接收队列满，底层把累计计数加一：

```c
can_rx_overflow_count = 1
observed_rx_overflow_count = 0
```

下一轮 `process_reliability()` 比较这两个值，发现不同：

```
new_rx_overflow = 1
```

Bus-off 分支没触发，于是进入接收溢出的 `else if`，调用：

```c
Canopen_App_ReportEmcy(
    CO_EMCY_ERROR_CAN_RX_OVERFLOW,
    CO_EMCY_REGISTER_COMMUNICATION,
    NULL);
```

它请求 EMCY 模块上报故障。假设发送成功，节点 ID 为 `1`，那么 EMCY 的 CAN-ID 是 `0x081`，数据大致为：

```
01 FF 11 00 00 00 00 00
```

这里错误码 `0xFF01` 按低字节在前发送为 `01 FF`；Error Register 为 `0x11`，即通信错误位 `0x10` 加通用错误位 `0x01`。

随后代码把：

```
reliability_fault_active = 1
```

标记为正在处理故障。





### 3. 进入安全输出并开始检查恢复

前面的故障分支结束后，函数继续执行：

```c
Canopen_App_SetSafeOutput(1u);
remember_status(Canopen_App_ProcessEmcy());
```

`SetSafeOutput(1)` 打开安全输出标志。`ProcessEmcy()` 负责处理待重试 EMCY；刚才报告帧已经成功发送的话，就没有待重试帧，不会重复发送故障帧。

本轮 `new_rx_overflow` 是 `1`，所以本轮健康判断失败：

```c
healthy = 0
reliability_recovery_ms = 0
```

之后的周期里，只要接收溢出计数没有再增加，`new_rx_overflow` 就会变成 `0`。采样正常、没有新的 CAN 错误、节点仍处于 Operational 时，恢复计时开始累加：

```
reliability_recovery_ms = 1、2、3……
```

同时 `Canopen_Task` 通过 `Canopen_App_GetOutputs()` 取 DO 值时会应用安全输出，输出值被强制为 `0`，再经输出队列交给 `IO_Task` 控制硬件。





### 4. 稳定时间达到阈值，清除故障

计时达到 `FAULT_RECOVERY_STABLE_MS` 后，调用：

```
Canopen_App_ClearEmcy();
```

假设清除帧发送成功，`co_emcy_clear()` 会发送错误码和 Error Register 都为零的 EMCY 清除帧。发送成功后，EMCY 模块清掉活动故障状态；可靠性处理代码随后执行：

```c
reliability_fault_active = 0
reliability_wait_for_rx = 0
reliability_recovery_ms = 0
安全输出标志 = 0
```

故障处理结束，后续 DO 可以恢复为对象字典中的输出值。

整条过程可以记成：

```c
CAN接收溢出计数增加
    ↓
Canopen_Task发现 new_rx_overflow = 1
    ↓
上报 EMCY
    ↓
启动故障处理并进入安全输出
    ↓
连续健康且节点处于 Operational
    ↓
稳定计时达到阈值
    ↓
发送 EMCY 清除帧
    ↓
清除故障标志并解除安全输出
```

这个例子没有 Bus-off，所以不会等待新的 CAN 接收帧来确认通信恢复；如果故障是 Bus-off，还要满足前面学过的接收帧计数增加条件。







# 2：Monitor_Task（）任务

```c
static void Monitor_Task(void *argument)
{
    uint32_t previous_sample_count = 0u;
    uint32_t previous_canopen_cycles = 0u;
    uint32_t previous_io_cycles = 0u;
    uint8_t tasks_healthy;

    (void)argument;
    for (;;)
    {
        freertos_monitor_tick_count++;
        io_sampling_stalled = (freertos_io_sample_count == previous_sample_count) ? 1u : 0u;
        if (io_sampling_stalled != 0u)
            freertos_io_queue_error_count++;

        tasks_healthy = (uint8_t)(canopen_task_cycle_count != previous_canopen_cycles &&
                                  io_task_cycle_count != previous_io_cycles);
        previous_sample_count = freertos_io_sample_count;
        previous_canopen_cycles = canopen_task_cycle_count;
        previous_io_cycles = io_task_cycle_count;

        if (tasks_healthy != 0u)
            (void)BSP_IWDG_Refresh();

        vTaskDelay(pdMS_TO_TICKS(1000u));
    }
}
```

这个任务每约 1 秒做两项检查：**I/O 采样有没有推进、CANopen 和 I/O 两个任务有没有继续运行**。如果两个任务都在运行，就刷新独立看门狗。



### 先看三个局部变量

```
uint32_t previous_sample_count = 0u;
uint32_t previous_canopen_cycles = 0u;
uint32_t previous_io_cycles = 0u;
```

它们分别记住上一次检查时的采样次数、CANopen 任务循环次数、I/O 任务循环次数，供下一轮比较。

```
uint8_t tasks_healthy;
```

保存本轮的任务健康检查结果：两个任务的循环计数都变化，才为非零。



### 每轮具体怎么跑

```
freertos_monitor_tick_count++;
```

记录 Monitor_Task 又运行了一轮。

```
io_sampling_stalled =
    (freertos_io_sample_count == previous_sample_count) ? 1u : 0u;
```

比较本次和上次检查的 I/O 采样计数：

- 相同：这段检查间隔内没有成功写入新采样，标记 `io_sampling_stalled = 1`。
- 不同：采样有推进，标记为 `0`。

采样停滞时，下面这句增加错误计数：

```
if (io_sampling_stalled != 0u)
    freertos_io_queue_error_count++;
```

接下来检查两个任务是否都运行过：

```
tasks_healthy = (uint8_t)(
    canopen_task_cycle_count != previous_canopen_cycles &&
    io_task_cycle_count != previous_io_cycles);
```

`&&` 表示两个条件必须都成立：CANopenTask 的循环计数变了，**并且** IOTask 的循环计数也变了，才认为任务健康。

然后把当前计数保存成下一轮的比较基准：

```
previous_sample_count = freertos_io_sample_count;
previous_canopen_cycles = canopen_task_cycle_count;
previous_io_cycles = io_task_cycle_count;
```

如果两个任务都正常运行，就刷新看门狗：

```
if (tasks_healthy != 0u)
    (void)BSP_IWDG_Refresh();
```

`(void)` 表示忽略刷新函数的返回值。若有一个任务没推进，就不刷新；如果这种情况持续到看门狗超时，硬件会触发复位。

最后：

```
vTaskDelay(pdMS_TO_TICKS(1000u));
```

Monitor_Task 阻塞约 1 秒，让其他任务运行，再开始下一轮检查。任务第一次启动时会先执行一轮检查，然后才进入这段 1 秒延时。

举个计数例子：上次检查时采样数是 `30`，本次是 `61`，说明采样有推进，`io_sampling_stalled = 0`。如果 CANopen 和 I/O 的循环计数也都增加，`tasks_healthy` 为 `1`，Monitor_Task 就刷新看门狗。

有个启动时序值得留意：三个 `previous_*` 初值都是 `0`。如果 Monitor_Task 第一次运行时采样数还是 `0`，它会立刻把 `io_sampling_stalled` 设为 `1`；而 ADC 半区采样大约 32 ms 才完成一次，所以启动初期可能短暂误判采样停滞。我们之后可以结合初始化和任务启动顺序，判断是否需要启动宽限时间。



