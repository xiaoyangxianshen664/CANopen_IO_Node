/**
 * @file    freertos_demo.c
 * @brief   当前 CANopen 工程的 FreeRTOS 任务组织层。
 *
 * CANopenTask 独占协议栈和对象字典；IOTask 独占按键、ADC 和 LED；
 * MonitorTask 与 DiagnosticsTask 只观察运行状态，为后续可靠性阶段留出入口。
 */

#include "freertos_demo.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "bsp_can.h"
#include "./ADC/ADC_Multi.h"
#include "./Key/Key.h"
#include "./LED/LED.h"
#include "./canopen_app.h"
#include "./IWDG/bsp_iwdg.h"

#include <stddef.h>

#define CANOPEN_TASK_PRIORITY 3U
#define IO_TASK_PRIORITY 2U
#define MONITOR_TASK_PRIORITY 1U
#define DIAGNOSTICS_TASK_PRIORITY 1U
#define CANOPEN_TASK_STACK_SIZE 512U
#define IO_TASK_STACK_SIZE 384U
#define MONITOR_TASK_STACK_SIZE 256U
#define DIAGNOSTICS_TASK_STACK_SIZE 256U
#define FAULT_RECOVERY_STABLE_MS 1000U

/*
 * EMCY 实机测试开关：置 1 时，启动后人为注入一次“看门狗复位”故障，
 * 用于验证 process_reliability()、EMCY 上报、安全输出和自动恢复链路。
 * 测试完成后必须改回 0，正式固件不能保留该注入。
 */
#define FREERTOS_EMCY_TEST_INJECT_WATCHDOG 0U

static TaskHandle_t CanopenTaskHandle;
static TaskHandle_t IoTaskHandle;
static TaskHandle_t MonitorTaskHandle;
static TaskHandle_t DiagnosticsTaskHandle;

typedef struct
{
    uint8_t di;
    uint16_t ai1;
    uint16_t ai2;
} io_sample_t;

/*队列句柄*/
static QueueHandle_t io_sample_queue; // I/O采样队列，传递DI、AI1、AI2结构体
static QueueHandle_t io_output_queue; // DO输出队列，把待输出的DO值传给IOTask

/*1. 运行与监视计数*/
volatile uint32_t freertos_io_sample_count;        // 成功写入I/O采样队列的次数
volatile uint32_t freertos_monitor_tick_count;     // MonitorTask运行的循环次数
static volatile uint32_t canopen_task_cycle_count; // CANopenTask循环运行次数，用于任务健康监测
static volatile uint32_t io_task_cycle_count;      // IOTask循环运行次数，用于任务健康监测

/*2. 诊断信息*/
volatile co_status_t freertos_diagnostics_last_error = CO_OK; // 最近记录的诊断错误状态
volatile uint32_t freertos_diagnostics_rx_overflow;           // CAN 接收队列溢出（丢帧）累计次数
volatile uint32_t freertos_diagnostics_tx_busy;               // CAN 发送邮箱全忙（报文未发出）累计次数
volatile uint32_t freertos_diagnostics_can_error;             // 进了can错误中断后，还进入错误回调的次数
volatile uint32_t freertos_diagnostics_bus_off;               // CAN Bus-off 发生累计次数
static volatile uint32_t freertos_io_queue_error_count;       // I/O队列操作失败或采样停滞的累计次数
static volatile uint8_t io_sampling_stalled;                  // 当前是否检测到 I/O 采样停滞。

/*3. 故障处理与恢复状态*/
static uint8_t reliability_fault_active; // 是否正在处理活动故障，只有两种值，0和1
static uint8_t reliability_wait_for_rx;  // Bus-off 后是否还在等待新 CAN 帧，以确认通信恢复
static uint32_t reliability_rx_baseline; // Bus-off 发生时的接收帧计数基准。
static uint32_t reliability_recovery_ms; // 连续满足恢复条件的计时值。

/*4：上次检查到的 CAN 错误计数，用于发现计数是否增加。*/
static uint32_t observed_rx_overflow_count; // 上次处理时观察到的CAN接收溢出计数
static uint32_t observed_can_error_count;   // 上次处理时观察到的CAN错误计数
static uint32_t observed_bus_off_count;     // 上次处理时观察到的Bus-off计数

/*5. 看门狗复位记录*/
static uint8_t watchdog_reset_pending; // 看门狗复位待上报标志，非0表示需报告看门狗复位故障

static void remember_status(co_status_t status);
static void dispatch_frame(const can_frame_t *frame);
static void process_reliability(void);

/**
 * @brief 从 ADC DMA 中断把已完成半区通知给 IOTask。
 * @param area_flag [输入] 本次完成的 DMA 半区标志：ADC_READY_FIRST_HALF 表示前半区，
 *                       ADC_READY_SECOND_HALF 表示后半区。
 * @return 无。
 * @note 运行在 ISR 中，只调用 xTaskNotifyFromISR 和 portYIELD_FROM_ISR。
 */
void adc_dma_notify_from_isr(uint32_t area_flag)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (IoTaskHandle != NULL)
    {
        (void)xTaskNotifyFromISR(IoTaskHandle, area_flag, eSetBits,
                                 &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

static uint8_t read_board_di(void)
{
    uint8_t value = 0u;

    if (HAL_GPIO_ReadPin(KEY1_GPIO_PORT, KEY1_PIN) == GPIO_PIN_SET)
        value |= 0x01u;
    if (HAL_GPIO_ReadPin(KEY2_GPIO_PORT, KEY2_PIN) == GPIO_PIN_SET)
        value |= 0x02u;
    return value;
}

static void apply_board_do(uint8_t outputs)
{
    LED_R((outputs & 0x01u) ? 0 : 1);
    LED_G((outputs & 0x02u) ? 0 : 1);
    LED_B((outputs & 0x04u) ? 0 : 1);
}

/**
 * @brief 读取实体 DI/AI，并把最新采样交给 CanopenTask。
 * @param argument [输入] FreeRTOS 任务参数，当前未使用。
 * @return 无。
 */
static void IO_Task(void *argument)
{
    io_sample_t sample;      // 采样结构体存DI，AI1，AI2
    uint8_t outputs;         // 用来局部存储取出的DO而已
    uint8_t half;            // ad采样值计算平均值的半区标志，为0则计算0~63，为1则计算64~127
    uint32_t received_flags; // 局部变量用于下面的if判断而已
    BaseType_t notified;     // 接受返回值，存pdfalse或者pdtrue，用于if 判断的局部变了而已

    (void)argument;
    for (;;)
    {
        io_task_cycle_count++;
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
}

/**
 * @brief 处理 CAN 帧、协议定时器、对象字典和 DO 输出队列。
 * @param argument [输入] FreeRTOS 任务参数，当前未使用。
 * @return 无。
 */
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
        canopen_task_cycle_count++;
        /*1. 把接收队列里的 CAN 帧处理完*/
        while (CAN_ReceiveFrame(&frame)) // 只要 CAN 接收队列里还有帧，CAN_ReceiveFrame() 就取出一帧放进 frame
            dispatch_frame(&frame);      // 然后交给 dispatch_frame() 分发，这里用 while，所以一轮任务会把当前积压的报文都处理掉；队列空了，循环才结束。

        /*2. 取出最新 I/O 采样并更新 CANopen 输入*/
        if (xQueueReceive(io_sample_queue, &sample, 0u) == pdPASS)                        // ADC及DMA采样结束唤醒了IO_Task，将最新的DI，AI1，AI2放进了 io_sample_queue 队列里，这里取出。
            remember_status(Canopen_App_UpdateInputs(sample.di, sample.ai1, sample.ai2)); // 调用Canopen_App_UpdateInputs更新到我们的字典对象条目里

        /*3. 推进 Heartbeat 和 PDO   ：每轮循环都调用这两个处理函数，并传入经过的时间 1u，表示按 1 ms 推进它们内部的定时逻辑。*/
        process_reliability();

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

static void Monitor_Task(void *argument)
{
    uint32_t previous_sample_count = 0u;   // 记录上一次检查时的采样次数
    uint32_t previous_canopen_cycles = 0u; // CANopen 任务循环次数
    uint32_t previous_io_cycles = 0u;      // I/O 任务循环次数
    uint8_t tasks_healthy;                 // 保存本轮的任务健康检查结果：两个任务的循环计数都变化，才为非零。
    uint8_t sample_monitor_initialized = 0u;

    (void)argument;
    for (;;)
    {
        freertos_monitor_tick_count++; // 记录 Monitor_Task 又运行了一轮。
        /*1：比较本次和上次检查的 I/O 采样计数：*/
        if (sample_monitor_initialized == 0u)
        {
            io_sampling_stalled = 0u;
            sample_monitor_initialized = 1u;
        }
        else
        {
            io_sampling_stalled = (freertos_io_sample_count == previous_sample_count) ? 1u : 0u; // 相同：这段检查间隔内没有成功写入新采样，标记 io_sampling_stalled = 1。不同：采样有推进，标记为 0。
            if (io_sampling_stalled != 0u)
                freertos_io_queue_error_count++; // 采样停滞时，下面这句增加错误计数：
        }
        /*2接下来检查两个任务是否都运行过：CANopenTask 的循环计数变了，并且 IOTask 的循环计数也变了，才认为任务健康。*/
        tasks_healthy = (uint8_t)(canopen_task_cycle_count != previous_canopen_cycles &&
                                  io_task_cycle_count != previous_io_cycles);
        /*3：把当前计数保存成下一轮的比较基准：*/
        previous_sample_count = freertos_io_sample_count;
        previous_canopen_cycles = canopen_task_cycle_count;
        previous_io_cycles = io_task_cycle_count;

        /*4：如果两个任务都正常运行，就刷新看门狗*/
        if (tasks_healthy != 0u)
            (void)BSP_IWDG_Refresh();

        vTaskDelay(pdMS_TO_TICKS(1000u)); // Monitor_Task 阻塞约 1 秒，让其他任务运行，再开始下一轮检查。
    }
}

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

static void process_reliability(void)
{
    /*它们是 process_reliability() 每次运行时临时使用的，不会像文件顶部的 static 变量那样保留到下一次调用。*/
    uint8_t new_bus_off;     // 是通过比较 can_bus_off_count 和 observed_bus_off_count 来赋值的，如果两者不同，比较结果为真，new_bus_off 得到 1，表示本轮发现 Bus-off 计数增加；如果相同，得到 0，表示没有发现新的 Bus-off 事件。
    uint8_t new_rx_overflow; // 接收溢出比较 can_rx_overflow_count 和 observed_rx_overflow_count
    uint8_t new_can_error;   // CAN 错误比较 can_error_count 和 observed_can_error_count。不同就赋值 1，相同就赋值 0。

    /*这两个变量在声明时还没有值，会在代码执行到相应赋值语句时才得到值。*/
    /*healthy 后面会根据多个条件合在一起判断：这些条件全都成立，healthy 才是 1；只要有一个不成立，就是 0
    healthy = (uint8_t)(io_sampling_stalled == 0u &&
                    new_bus_off == 0u &&
                    new_rx_overflow == 0u &&
                    new_can_error == 0u);*/
    uint8_t healthy;    // 本轮是否满足故障恢复条件
    co_status_t status; // status 用来接收 EMCY 函数的返回值，如：status = Canopen_App_ReportEmcy(...);这里 status 保存故障上报的结果；后面调用 EMCY 重试或清除时，也会用它保存对应函数的返回结果。

    /* 1:处理看门狗复位的故障
    这段只处理一种情况：系统检测到之前发生过看门狗复位，现在把这条信息作为 EMCY 故障报告出去。 */
    if (watchdog_reset_pending != 0u && reliability_fault_active == 0u) // reliability_fault_active非0表示已有故障待处理，正在执行安全输出、EMCY处理和恢复检查；
    {
        status = Canopen_App_ReportEmcy(CO_EMCY_ERROR_WATCHDOG_RESET,    // 上报“看门狗复位”故障码
                                        CO_EMCY_REGISTER_GENERIC, NULL); // 设置通用错误位,不附带厂商自定义数据
        if (status != CO_ERR_ARGUMENT)                                   // 只要不是参数错误，就认为故障已交给 EMCY 流程处理
        {
            reliability_fault_active = 1u; // reliability_fault_active = 1u 不表示 EMCY 已经成功上报。它只表示可靠性故障处理流程开始了；EMCY 帧可能已发送，也可能还在等待重试。
            watchdog_reset_pending = 0u;   // 清除等待上报的看门狗复位标记
        }
    }
    /*如果第一次检查前没有发生过 Bus-off：他们的值都是0，第一次比较：相同 → new_bus_off = 0
     如果第一次检查前已经发生过 Bus-off：can_bus_off_count = 1，observed_bus_off_count = 0 第一次比较：不同 → new_bus_off = 1
    */
    new_bus_off = (uint8_t)(can_bus_off_count != observed_bus_off_count);             // Bus-off累计次数与上次不同，表示本轮出现了新事件
    new_rx_overflow = (uint8_t)(can_rx_overflow_count != observed_rx_overflow_count); // 接收溢出累计次数与上次不同，表示本轮出现了新事件
    new_can_error = (uint8_t)(can_error_count != observed_can_error_count);           // CAN错误累计次数与上次不同，表示本轮出现了新事件
    observed_bus_off_count = can_bus_off_count;                                       // 更新Bus-off比较基准，避免下轮重复识别同一次事件
    observed_rx_overflow_count = can_rx_overflow_count;                               // 更新接收溢出的比较基准
    observed_can_error_count = can_error_count;                                       // 更新CAN错误的比较基准

    /*2：处理 Bus-off：只有本轮检测到新的 Bus-off，才进入这个分支。*/
    if (new_bus_off != 0u) // 本轮检测到新的 Bus-off 事件
    {
        reliability_wait_for_rx = 1u;                 // 记录一项 Bus-off 专属的恢复要求：后续恢复检查时，要等到收到新的 CAN 帧。
        reliability_rx_baseline = can_rx_frame_count; // 保存此刻的 CAN 接收帧计数，作为基准。后续只有 can_rx_frame_count 大于这个基准，才能满足“Bus-off 后收到了新帧”这个条件。
        /*再判断当前是否已经有可靠性故障正在处理。没有才尝试报告这次 Bus-off；如果已有故障，
        就不再叠加报告另一种故障，但前面设置的接收恢复标记和基准仍然生效。*/
        if (reliability_fault_active == 0u)
        {
            status = Canopen_App_ReportEmcy(CO_EMCY_ERROR_BUS_OFF,                 // 请求 EMCY 模块上报 Bus-off 故障，错误寄存器标为通信类。
                                            CO_EMCY_REGISTER_COMMUNICATION, NULL); // 标记为通信类错误,  不附带厂商自定义数据
            if (status != CO_ERR_ARGUMENT)                                         // 若返回值不是参数错误
                reliability_fault_active = 1u;                                     // 启动可靠性故障处理；不代表EMCY帧已经成功发送
        }
    }
    /*3： Bus-off 分支没进，那么没发生新的 Bus-off，这里另外一种故障，can接收帧溢出*/
    else if (new_rx_overflow != 0u && reliability_fault_active == 0u)
    {
        status = Canopen_App_ReportEmcy(CO_EMCY_ERROR_CAN_RX_OVERFLOW,         // 上报CAN接收溢出故障码
                                        CO_EMCY_REGISTER_COMMUNICATION, NULL); // 标记为通信类错误,  不附带厂商自定义数据
        if (status != CO_ERR_ARGUMENT)                                         // 若返回值不是参数错误
            reliability_fault_active = 1u;                                     // 启动可靠性故障处理；不代表EMCY帧已经成功发送
    }
    /*4： 前面没有更新Bus-off或，can接收帧溢出，且当前无活动故障时，检查CAN错误*/
    else if (new_can_error != 0u && reliability_fault_active == 0u)
    {
        status = Canopen_App_ReportEmcy(CO_EMCY_ERROR_CAN,                     // 上报通用CAN错误码
                                        CO_EMCY_REGISTER_COMMUNICATION, NULL); // 标记为通信类错误,不附带厂商自定义数据
        if (status != CO_ERR_ARGUMENT)                                         // 不是参数错误，表示故障已交给EMCY流程处理
            reliability_fault_active = 1u;                                     // 启动可靠性故障处理；不代表EMCY帧已经成功发送
    }
    /*5：这段是故障优先链的最后一项：前面的 Bus-off、接收溢出、一般 CAN 错误都没有被选中，而且当前没有活动故障时，才检查 I/O 采样停滞。*/
    else if (io_sampling_stalled != 0u && reliability_fault_active == 0u)
    {
        status = Canopen_App_ReportEmcy(CO_EMCY_ERROR_ADC,               // 上报ADC采样故障码
                                        CO_EMCY_REGISTER_VOLTAGE, NULL); // 将错误归入电压类, 不附带厂商自定义数据
        if (status != CO_ERR_ARGUMENT)                                   // 返回值不是参数错误
            reliability_fault_active = 1u;                               // 启动可靠性故障处理；不代表EMCY帧已经成功发送
    }
    /*6：如果前面的分支没有启动故障处理，标志仍为 0，这里就 return，后两句不执行。
    检查 reliability_fault_active
   ├─ 0：return，结束本轮
   └─ 1：打开安全输出 → 处理/重试EMCY
    */
    if (reliability_fault_active == 0u) // 当前没有活动故障
        return;                         // 不需要安全输出、EMCY重试或恢复检查，直接结束本轮

    Canopen_App_SetSafeOutput(1u);              // 有故障则上面的if不成立：打开安全输出开关，后续DO将被强制为0
    remember_status(Canopen_App_ProcessEmcy()); // 处理EMCY中已保存的待重试帧。如果当前没有待重试帧，通常返回 CO_IGNORED；如果有，就尝试发送。

    /*7：这段在回答一个问题：故障发生后，系统是否已经连续健康到可以尝试清除 EMCY？*/

    /*先计算本轮的基本健康状态：*/
    healthy = (uint8_t)(io_sampling_stalled == 0u &&                  // 查 ADC/I/O采样是否正常，io_sampling_stalled在 Monitor_Task() 里被赋值
                        new_bus_off == 0u && new_rx_overflow == 0u && // 本轮没有新的Bus-off
                        new_can_error == 0u);                         // 本轮没有新的接收溢出,本轮没有新的CAN错误
    /*然后是 Bus-off 专属检查： Bus-off恢复的额外条件：必须在Bus-off后收到新的CAN帧。  */
    if (reliability_wait_for_rx != 0u &&               // 之前发生过 Bus-off故障，Bus-off分支将把这个标志设为1
        can_rx_frame_count == reliability_rx_baseline) // // 接收帧数仍等于Bus-off时记录的基准
        healthy = 0u;                                  // // 尚未收到新帧，本轮不能算系统恢复健康

    /* 8：上面的healthy置1且到了if判断发现Bus-off 后已收到新帧，此时healthy 保持 1，可以累计恢复时间 */
    if (healthy != 0u && Canopen_App_IsOperational() != 0u) // 本轮系统健康，并且 CANopen 节点处于 Operational，才累计故障恢复稳定时间。
        reliability_recovery_ms++;                          // 稳定时间增加；通常每次函数调用代表约1ms
    else
        reliability_recovery_ms = 0u; // 中途再次异常或离开Operational，连续计时清零

    /*9：这段是在恢复稳定时间达到阈值后，尝试清除 EMCY，并在认为清除完成后解除安全输出。。*/
    if (reliability_recovery_ms >= FAULT_RECOVERY_STABLE_MS) // #define FAULT_RECOVERY_STABLE_MS 1000U，也就是1s
    {
        status = Canopen_App_ClearEmcy(); // 组装并发送EMCY清除帧，或处理清除帧重试
        /*CO_OK ：清除帧发送成功或CO_IGNORED：当前没有需要清除的活动故障，或者清除操作已无需重复执行。*/
        if (status == CO_OK || status == CO_IGNORED)
        {
            reliability_fault_active = 0u; // 清除本地“活动故障”标志
            reliability_wait_for_rx = 0u;  // 不再等待Bus-off后的CAN报文
            reliability_recovery_ms = 0u;  // 清零恢复计时，准备下一次故障
            Canopen_App_SetSafeOutput(0u); // 解除安全输出，允许恢复正常DO输出
        }
        else
        {
            /*清除失败时保留故障状态和安全输出，记录错误，后续再次进入本函数时继续尝试*/
            remember_status(status);
        }
    }
}

static void remember_status(co_status_t status)
{
    if (status != CO_OK && status != CO_IGNORED)
        canopen_last_error = status;
}

static void dispatch_frame(const can_frame_t *frame)
{
    remember_status(Canopen_App_NmtReceive(frame));
    remember_status(Canopen_App_SdoReceive(frame));
    remember_status(Canopen_App_PdoReceive(frame));
}

/**
 * @brief 停在当前状态，表示 FreeRTOS 应用初始化失败。
 * @note 这些错误发生在调度器启动前，或表示调度器意外返回，当前没有安全的
 *       任务上下文可以继续运行，因此保留在此处等待调试器定位原因。
 */
static void freertos_demo_halt(void)
{
    for (;;)
        ;
}

/**
 * @brief 创建应用任务、队列并启动 FreeRTOS 调度器。
 * @param 无。
 * @return 无；正常情况下不会返回。
 */
void freertos_demo(void)
{
    watchdog_reset_pending = (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) ? 1u : 0u; // 看看“这次开机是不是 IWDG 看门狗把 MCU 重启的，上一次复位由独立看门狗 IWDG 触发，就把 watchdog_reset_pending 设为 1
    __HAL_RCC_CLEAR_RESET_FLAGS();                                                      // ：看完以后，把这个复位原因标志清掉。
#if FREERTOS_EMCY_TEST_INJECT_WATCHDOG != 0U
    /* 测试版强制注入一次看门狗复位记录，后续仍由 process_reliability() 正常处理。 */
    watchdog_reset_pending = 1u;
#endif
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
