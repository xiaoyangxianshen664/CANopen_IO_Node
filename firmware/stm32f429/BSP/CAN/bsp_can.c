#include "./CAN/bsp_can.h"

/* CAN1 HAL 句柄，供初始化、发送和中断处理共同使用。 */
CAN_HandleTypeDef Can_Handle;

/* HAL 接收暂存区；回调读取后会复制到协议层环形队列。 */
static CAN_RxHeaderTypeDef rx_header;
static uint8_t rx_data[8]; // 经典 CAN 一帧最多有 8 个数据字节，收到一帧后，HAL 把数据放进这里：

typedef struct
{
    can_frame_t frames[16]; /* 保存待处理的协议层帧。 */
    volatile uint8_t head;  /* ISR 写入位置。 */
    volatile uint8_t tail;  /* 任务读取位置。 */
} can_rx_queue_t;

static can_rx_queue_t rx_queue;          /* CAN 接收环形队列。 */
volatile uint32_t can_rx_overflow_count; /* 队列满导致丢帧的次数。 */
volatile uint32_t can_tx_busy_count;     /* 三个发送邮箱全满的次数。 */
volatile uint32_t can_error_count;       /* HAL CAN 错误回调次数。 */
volatile uint32_t can_bus_off_count;     /* Bus-off 错误次数。 */
volatile uint32_t can_rx_frame_count; /* Total CAN frames received. */

/**
 * @brief 配置 CAN1 使用的 PB8/PB9 复用引脚。
 * @param 无。
 * @return 无。
 * @note PB8 为 CAN1_RX，PB9 为 CAN1_TX，二者均使用 AF9。
 */
static void CAN_GPIO_Config(void)
{
    GPIO_InitTypeDef init = {0}; /* 先清零，避免未初始化字段影响 HAL。 */

    __HAL_RCC_GPIOB_CLK_ENABLE();           /* 开启 GPIOB 时钟。 */
    init.Pin = CAN_RX_PIN | CAN_TX_PIN;     /* 同时配置 RX 和 TX。 */
    init.Mode = GPIO_MODE_AF_PP;            /* 复用推挽输出/输入模式。 */
    init.Pull = GPIO_NOPULL;                /* 收发器提供总线电气偏置。 */
    init.Speed = GPIO_SPEED_FREQ_VERY_HIGH; /* CAN 边沿使用高速 GPIO。 */
    init.Alternate = CAN_AF_PORT;           /* 选择 AF9_CAN1。 */
    HAL_GPIO_Init(CAN_GPIO_PORT, &init);    /* 写入 GPIOB 配置。 */
}

/**
 * @brief 配置 CAN1 的标准帧过滤器。
 * @param 无。
 * @return 无。
 * @note 当前接收全部标准数据帧，便于阶段7调试；协议层再按 CAN-ID 分类。
 */
static void CAN_Filter_Config(void)
{
    CAN_FilterTypeDef filter = {0}; /* 过滤器结构体清零。 */

    filter.FilterBank = 0;                      /* 使用过滤器组0。 */
    filter.FilterMode = CAN_FILTERMODE_IDMASK;  /* 使用 ID 掩码模式。 */
    filter.FilterScale = CAN_FILTERSCALE_32BIT; /* 使用 32 位过滤器。 */
                                                /*当前采用 32 位模式并全部填 0，是因为阶段7首先要验证 CAN 物理收发、FIFO、中断和协议层接收链路，暂时不在硬件过滤器里限制 CAN-ID。*/
    filter.FilterIdHigh = 0;
    filter.FilterIdLow = 0;
    filter.FilterMaskIdHigh = 0;
    filter.FilterMaskIdLow = 0;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0; /* 统一送入 FIFO0。 */
    filter.FilterActivation = ENABLE;               /* 使能本过滤器。 */
    filter.SlaveStartFilterBank = 14;               /* CAN1 独立使用时保留默认分界。 */
    HAL_CAN_ConfigFilter(&Can_Handle, &filter);     /* 写入 bxCAN 过滤器。 */
}

/**
 * @brief 初始化 CAN1 并开启 FIFO0、错误和 Bus-off 通知。
 * @param 无。
 * @return 无。
 * @note CAN1 使用 500 kbit/s、标准数据帧和 Normal 模式。
 * @example CAN_Config();
 */
void CAN_Config(void)
{
    CAN_GPIO_Config();           /* 先配置 CAN1 复用引脚。 */
    __HAL_RCC_CAN1_CLK_ENABLE(); /* 开启 CAN1 外设时钟。 */

    Can_Handle.Instance = CANx;                     /* 选择 CAN1 实例。 */
    Can_Handle.Init.TimeTriggeredMode = DISABLE;    /* 不使用时间触发通信。 */
    Can_Handle.Init.AutoBusOff = ENABLE;            /* Bus-off 后允许硬件自动恢复。 */
    Can_Handle.Init.AutoWakeUp = ENABLE;            /* 允许总线活动唤醒 CAN。 */
    Can_Handle.Init.AutoRetransmission = ENABLE;    /* 发送失败自动重发。 */
    Can_Handle.Init.ReceiveFifoLocked = DISABLE;    /* FIFO 满时允许覆盖旧消息策略。 */
    Can_Handle.Init.TransmitFifoPriority = DISABLE; /* 使用 CAN-ID 仲裁发送优先级。 */
    Can_Handle.Init.Mode = CAN_MODE_NORMAL;         /* 使用真实总线 Normal 模式。 */
    Can_Handle.Init.SyncJumpWidth = CAN_SJW_1TQ;    /* 同步跳转宽度 1TQ。 */
    Can_Handle.Init.TimeSeg1 = CAN_BS1_11TQ;        /* 时间段1为 11TQ。 */
    Can_Handle.Init.TimeSeg2 = CAN_BS2_3TQ;         /* 时间段2为 3TQ。 */
    Can_Handle.Init.Prescaler = 6;                  /* APB1=45MHz 时得到 500kbit/s。 */
    HAL_CAN_Init(&Can_Handle);                      /* 初始化 bxCAN 寄存器。 */

    CAN_Filter_Config();                      /* 配置标准帧进入 FIFO0。 */
    HAL_CAN_Start(&Can_Handle);               /* 启动 CAN 外设。 */
    HAL_CAN_ActivateNotification(&Can_Handle, /* 开启 HAL 通知源。 */
                                 CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_ERROR | CAN_IT_BUSOFF);

    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 6, 0); /* 可安全调用 FreeRTOS API 的中断优先级。 */
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);         /* 开启 FIFO0 中断。 */
    HAL_NVIC_SetPriority(CAN1_SCE_IRQn, 6, 0); /* 错误中断使用相同优先级。 */
    HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);         /* 开启状态变化错误中断。 */
}

/**
 * @brief 把协议层帧转换为 HAL 标准帧并提交发送邮箱。
 * @param user 传输层私有参数，当前不使用。
 * @param frame 待发送的协议层帧。
 * @return CO_OK 表示提交成功；邮箱满返回 CO_ERR_TX_BUSY；其他失败返回 CO_ERR_TX_FAILED。
 */
co_status_t CAN_SendFrame(void *user, const can_frame_t *frame)
{
    CAN_TxHeaderTypeDef header = {0}; /* HAL 发送帧头清零。 */
    uint32_t mailbox;                 /* HAL 返回实际使用的邮箱编号。 */
    (void)user;                       /* 当前适配层没有额外用户参数。 */

    if (frame == NULL) /* 拒绝空帧指针。 */
        return CO_ERR_ARGUMENT;
    if (frame->is_extended || frame->is_remote || frame->is_fd ||
        frame->id > CO_CAN_ID_MAX || frame->dlc > 8u) /* 协议只允许标准经典数据帧。 */
        return CO_ERR_FRAME_TYPE;
    if (HAL_CAN_GetTxMailboxesFreeLevel(&Can_Handle) == 0u) /* 检查三个邮箱是否全满。 */
    {
        can_tx_busy_count++; /* 记录暂忙，调用者负责后续重试。 */
        return CO_ERR_TX_BUSY;
    }

    header.StdId = frame->id;                      /* 填入 11 位标准 CAN-ID。 */
    header.IDE = CAN_ID_STD;                       /* 选择标准帧。 */
    header.RTR = CAN_RTR_DATA;                     /* 选择数据帧。 */
    header.DLC = frame->dlc;                       /* 填入有效数据长度。 */
    header.TransmitGlobalTime = DISABLE;           /* 不使用时间戳功能。 */
    if (HAL_CAN_AddTxMessage(&Can_Handle, &header, /* 提交帧头和数据到邮箱。 */
                             (uint8_t *)frame->data, &mailbox) != HAL_OK)
        return CO_ERR_TX_FAILED; /* HAL 提交失败。 */
    return CO_OK;                /* 已成功进入发送邮箱。 */
}

/**
 * @brief HAL FIFO0 有待处理报文时，把硬件帧复制到协议层环形队列。
 * @param hcan 触发本次回调的 CAN 句柄。
 * @return 无。
 * @note 回调只取帧和入队，不在中断中执行 NMT、SDO 或 PDO 业务。
 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    uint8_t next; /* 这是本次中断处理中使用的临时变量，用来保存：当前写入位置的下一个索引它是局部变量，因为只在本次回调中计算和使用。 */

    /*1：从FIFO读取报文帧*/
    if (hcan != &Can_Handle) /* 如果以后还有 CAN2，HAL 也可能调用同一个回调函数，但传入的是 CAN2 的句柄。此时直接返回，避免误处理其他 CAN 实例的报文 */
        return;
    /*hcan→ 哪个 CAN 外设，CAN_RX_FIFO0 → 从 FIFO0 读取，&rx_header  → 把帧头写入 rx_header，rx_data     → 把数据写入 rx_data[8]*/
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK) /* 这一步从硬件 FIFO0 读取一帧报文。 */
        return;                                                                  // 如果读取失败：直接退出中断回调，不继续处理无效数据。
    can_rx_frame_count++; /* RX activity confirms controller recovery. */

    /*2：计算软件环形队列的下一个写位置：*/
    next = (uint8_t)((rx_queue.head + 1u) % 16u); /* 环形队列下一个位置。 */

    /*3：判断队列是否已满：*/
    if (next == rx_queue.tail) /* head 追上 tail 表示队列已满。 */
    {
        can_rx_overflow_count++; /* 记录丢帧，不覆盖未处理帧。 */
        return;
    }

    rx_queue.frames[rx_queue.head].id = rx_header.StdId;                          /* 转换标准 CAN-ID。 */
    rx_queue.frames[rx_queue.head].dlc = rx_header.DLC;                           /* 转换 DLC。 */
    rx_queue.frames[rx_queue.head].is_extended = (rx_header.IDE == CAN_ID_EXT);   /* 标记扩展帧。 */
    rx_queue.frames[rx_queue.head].is_remote = (rx_header.RTR == CAN_RTR_REMOTE); /* 标记远程帧。 */
    rx_queue.frames[rx_queue.head].is_fd = 0;                                     /* bxCAN 只支持经典 CAN。 */
    for (uint8_t i = 0; i < rx_header.DLC; i++)                                   /* 复制有效数据字节。 */
        rx_queue.frames[rx_queue.head].data[i] = rx_data[i];
    rx_queue.head = next; /* 最后提交新的写位置。 */
}

/**
 * @brief 从接收环形队列取出一帧。
 * @param frame 输出帧地址。
 * @return 1 表示成功取帧，0 表示参数无效或队列为空。
 */
uint8_t CAN_ReceiveFrame(can_frame_t *frame)
{
    uint8_t tail; /* 保存本次读取位置。 */

    if (frame == NULL || rx_queue.tail == rx_queue.head) /* 检查参数和空队列。 */
        return 0;
    tail = rx_queue.tail;                         /* 读取当前队列尾位置。 */
    *frame = rx_queue.frames[tail];               /* 整帧复制给调用者。 */
    rx_queue.tail = (uint8_t)((tail + 1u) % 16u); /* 提交新的读取位置。 */
    return 1;
}

/**
 * @brief 记录 HAL CAN 错误和 Bus-off 状态。
 * @param hcan 发生错误的 CAN 句柄。
 * @return 无。
 */
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
    if (hcan != &Can_Handle) /* 只统计当前 CAN1。 */
        return;
    can_error_count++;                                      /* 记录一次 HAL 错误回调。 */
    if ((HAL_CAN_GetError(hcan) & HAL_CAN_ERROR_BOF) != 0u) /* 判断 Bus-off 位。 */
        can_bus_off_count++;                                /* 记录 Bus-off 次数。 */
}
