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

#define CANOPEN_TASK_PRIORITY 3U
#define IO_TASK_PRIORITY 2U
#define MONITOR_TASK_PRIORITY 1U
#define DIAGNOSTICS_TASK_PRIORITY 1U
#define CANOPEN_TASK_STACK_SIZE 512U
#define IO_TASK_STACK_SIZE 384U
#define MONITOR_TASK_STACK_SIZE 256U
#define DIAGNOSTICS_TASK_STACK_SIZE 256U

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
