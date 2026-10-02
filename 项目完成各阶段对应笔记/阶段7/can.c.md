

# 总览：

```c
.c 主线已经理清了：
1. 定义和初始化变量
   - CAN 全局句柄；
   - HAL 接收帧头和数据暂存区；
   - 软件环形队列；
   - head、tail 和错误统计变量。
2. CAN 初始化分三步					 //static void CAN_GPIO_Config(void) 和static void CAN_Filter_Config(void)和 void CAN_Config(void)
   - GPIO 引脚配置；
   - 过滤器配置；
   - CAN 通信参数和中断通知配置。
3. 发送方向								//函数co_status_t CAN_SendFrame(void *user, const can_frame_t *frame)
   协议层 can_frame_t
       ↓
   CAN_SendFrame()
       ↓
   HAL_CAN_AddTxMessage()
       ↓
   CAN 硬件发送邮箱
4. 接收中断方向						 //函数void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
   CAN 总线	
       ↓
   CAN 硬件 FIFO0
       ↓
   HAL_CAN_GetRxMessage()
       ↓
   rx_header + rx_data
       ↓
   软件环形队列 rx_queue
5. 任务读取方向						//函数uint8_t CAN_ReceiveFrame(can_frame_t *frame)
   rx_queue.frames[tail]
       ↓
   CAN_ReceiveFrame()
       ↓
   调用者定义的 can_frame_t 结构体和 data[8] 数组
其中：
head
    中断写入位置

tail
    任务读取位置
所以整个 CAN BSP 的职责就是完成：
协议层报文 → CAN 硬件发送邮箱
CAN 硬件接收 FIFO → 软件环形队列 → 协议层报文
    
       ↓
6.发生 CAN 错误 → 错误回调统计并判断 Bus-off	//函数void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
```



# 1：基本句柄和变量

```c
CAN_HandleTypeDef Can_Handle;//CAN1 HAL 句柄，
```

```c
static uint8_t rx_data[8];			//can报文帧数据内容存这里
static CAN_RxHeaderTypeDef rx_header;
//是 STM32 HAL CAN 库定义好的类型，通常就在：stm32f4xx_hal_can.h里面。它本质上是一个结构体类型，用来保存接收到的 CAN 帧的头部信息
```

```c
大致定义是：
typedef struct
{
    uint32_t StdId;              // 标准ID
    uint32_t ExtId;              // 扩展ID
    uint32_t IDE;                // ID类型：标准/扩展
    uint32_t RTR;                // 数据帧/远程帧
    uint32_t DLC;                // 数据长度
    uint32_t Timestamp;          // 时间戳
    uint32_t FilterMatchIndex;   // 匹配到的过滤器
} CAN_RxHeaderTypeDef;
rx_header 为我们创建的结构体变量
    
//什么时候真正被 HAL 调用？
在这个函数：
HAL_CAN_GetRxMessage(
    &hcan1,
    CAN_RX_FIFO0,
    &rx_header,
    rx_data
);
HAL 函数的第三个参数就是：CAN_RxHeaderTypeDef *pHeader
    
示例：
static CAN_RxHeaderTypeDef rx_header;
static uint8_t rx_data[8];
HAL_CAN_GetRxMessage(
    &hcan1,
    CAN_RX_FIFO0,
    &rx_header,
    rx_data
);


 //总结
执行这个函数后：
HAL 从 CAN 接收 FIFO 中取出一帧已经收到的 CAN 报文，然后把这帧报文的信息分别存入我们事先定义好的 rx_header 和 rx_data。
可以把整个过程记成：
CAN总线
   ↓
STM32 CAN外设
   ↓
CAN接收FIFO
   ↓
HAL_CAN_GetRxMessage()
   ↓
   ├── rx_header ← ID、DLC、帧类型等
   │
   └── rx_data   ← 11 22 33 44 等实际数据
所以如果你现在是在学习 CANopen 接收代码，后面看到：

if (rx_header.StdId == 0x181)

你就应该立刻理解成：

拿刚刚 HAL 接收到的这帧 CAN 报文的 ID，判断它是不是 0x181。

而：

rx_data[0]
rx_data[1]
...

就是在取这帧报文的 Data 字段。
    
例如收到 SDO 请求：
CAN-ID = 0x601
DLC    = 8
Data   = 40 00 10 00 00 00 00 00
接收之后：
rx_header.StdId = 0x601;
rx_header.DLC   = 8;

rx_data[0] = 0x40;
rx_data[1] = 0x00;
rx_data[2] = 0x10;
rx_data[3] = 0x00;
rx_data[4] = 0x00;
rx_data[5] = 0x00;
rx_data[6] = 0x00;
rx_data[7] = 0x00;
这里的 rx_data 只是 HAL 层暂存区。它后面会被复制到协议层自己的帧结构中，所以不能让协议层长期直接使用它。
同样，static 表示它只在 bsp_can.c 内部使用。
```



```c
typedef struct
{
    can_frame_t frames[16];       /* 保存待处理的协议层帧。 */
    volatile uint8_t head;        /* ISR 写入位置。 */
    volatile uint8_t tail;        /* 任务读取位置。 */
} can_rx_queue_t;
//这一段没有立即创建变量，而是在定义一种新的结构体类型，可以把它理解为定义了一个“CAN 接收环形队列模板”。
这个队列由三部分组成：
frames[16]   保存 16 个协议层 CAN 帧的位置
head         中断中写入位置索引
tail         任务读取位置索引

子成员：can_frame_t frames[16] 是我们纯 C 协议层定义的帧类型：
typedef struct
{
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint8_t is_extended;
    uint8_t is_remote;
    uint8_t is_fd;
} can_frame_t;
frames[16] 表示队列最多预留 16 个帧槽位。
不过当前环形队列采用“空一个槽位区分满和空”的方法，所以实际最多同时保存 15 帧。因为判断队列满的条件是：
next == rx_queue.tail
如果真的使用到第 16 个位置，就无法区分：
队列为空
队列已满
因此会主动保留一个空槽。


子成员：volatile uint8_t head;
head 是队列的写入位置，CAN 接收回调运行在中断环境中，它每收到一帧，就把数据写入：
rx_queue.frames[rx_queue.head]
然后把 head 向后移动：rx_queue.head =（uint8_t)((rx_queue.head + 1u) % 16u);
如果当前：head = 3 表示下一帧放入： frames[3]
写完后变成：head = 4
到 frames[15] 后，再回到：frames[0] 这就是“环形”的来源。
    
    
    
子成员：volatile uint8_t tail;  //tail 是队列的读取位置索引
CANopen 任务在普通任务上下文中调用：CAN_ReceiveFrame(&frame);

函数内部会从：rx_queue.frames[rx_queue.tail]取出一帧，然后移动 tail：
rx_queue.tail =(uint8_t)((tail + 1u) % 16u);
因此： 
中断只负责 head：写入队列   
任务只负责 tail：读取队列
数据流可以画成：
CAN FIFO0
   ↓
中断回调
   ↓
frames[head] 写入
   ↓
head 前进

CANopenTask
   ↓
frames[tail] 读取
   ↓
tail 前进
head 和 tail 都声明为 volatile，是因为它们会被不同执行上下文访问：
    
head：
    中断回调修改
    任务读取

tail：
    任务修改
    中断读取
    
//总览
 head 和 tail 就是环形队列的两个数组索引；head 指向下一次写入位置，tail 指向下一次读取位置。
 在当前实现中，frames[16] 实际最多存放 15 帧，因为必须空出一个槽位区分：head == tail
 到底是：队列为空 还是队列已满
```



```c
static can_rx_queue_t rx_queue;   //创建了一个接收队列变量。
它包含：
rx_queue.frames[16]
rx_queue.head
rx_queue.tail
由于它是文件级静态变量，启动时会自动清零：
rx_queue.head = 0
rx_queue.tail = 0
rx_queue.frames[] 全部为 0 因此初始状态就是空队列
    
```



 CAN 统计变量

```c
//这些是传输层的运行统计

volatile uint32_t can_rx_overflow_count; //表示接收环形队列已满时，后续帧被丢弃的次数。4
它可以帮助判断：
- CANopen 任务处理是否太慢；
- 队列容量是否太小；
- 总线报文负载是否过高；
- 是否发生长时间任务阻塞。
    
volatile uint32_t can_tx_busy_count;     /* 表示调用发送函数时，三个 CAN 发送邮箱都被占用的次数。 */
这不一定代表 CAN 硬件故障，也可能只是：
- 总线暂时繁忙；
- 没有其他节点 ACK；
- 自动重发导致邮箱长时间占用；
- 上层短时间连续发送太多帧。
    
volatile uint32_t can_error_count;      // 表示 HAL CAN 错误回调被调用的次数。
它可以统计 CAN 外设报告的错误，例如：
ACK 错误
总线错误
主动错误
被动错误
FIFO 错误
Bus-off
具体错误原因还要通过：
HAL_CAN_GetError(&Can_Handle)读取。


volatile uint32_t can_bus_off_count;      /* Bus-off 错误次数。 */
Bus-off 通常说明发送错误计数累积到严重程度，常见原因包括：
CANH/CANL 接线错误
没有共地
波特率不一致
终端电阻问题
总线上没有其他节点 ACK
CAN 收发器或硬件异常
当前这几个统计量也声明为 volatile，因为它们可能在 CAN 中断回调中增加，而诊断任务可能在另一个上下文读取。
```

这部分代码可以分成三层：

```c
HAL 硬件层：
    Can_Handle
    rx_header
    rx_data

BSP 缓冲层：
    can_rx_queue_t
    rx_queue
    head
    tail

诊断统计层：
    can_rx_overflow_count
    can_tx_busy_count
    can_error_count
    can_bus_off_count
```







# 2：GPIO复用给can外设

```c
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
```





# 3：FIFO过滤器初始化

过滤器决定：

```c
哪些收到的 CAN 帧允许进入 FIFO0
哪些帧直接被硬件丢弃
//它不是“CAN 收发器的过滤器”，而是 STM32 CAN 外设内部的“接收过滤器”。
//它决定的是：CAN 总线上收到的帧，哪些允许进入 STM32 的接收 FIFO，哪些直接丢弃。
```

你可以把它理解成：

```c
CAN总线
   ↓
STM32 CAN外设收到一帧
   ↓
┌─────────────────┐
│   CAN过滤器      │
│                 │
│  这帧要不要？    │
└─────────────────┘
   ↓             ↓
  要             不要
   ↓             ↓
FIFO0           丢弃
   ↓
HAL_CAN_GetRxMessage()
   ↓
rx_header + rx_data
```

所以过滤器发生在：

```c
收到CAN帧
   ↓
过滤
   ↓
FIFO
   ↓
HAL读取
```

**不是等 `HAL_CAN_GetRxMessage()` 读取的时候才过滤。**



```c
static void CAN_Filter_Config(void)
{
    CAN_FilterTypeDef filter = {0}; //创建过滤器结构体 创建一个：CAN_FilterTypeDef 类型的变量：随后把结构体所有成员先清零。
    filter.FilterBank = 0;                      /*  Bank（过滤器组共	28 组，可选0~27），此处使用过滤器组0。 */
    filter.FilterMode = CAN_FILTERMODE_IDMASK;  /* 使用 ID 掩码模式。 */
    filter.FilterScale = CAN_FILTERSCALE_32BIT; /* 使用 32 位过滤器。 */
    //当前采用 32 位模式并全部填 0，是因为阶段7首先要验证 CAN 物理收发、FIFO、中断和协议层接收链路，暂时不在硬件过滤器里限制 CAN-ID。
    filter.FilterIdHigh = 0; 
    filter.FilterMaskIdHigh = 0; 
    filter.FilterIdLow = 0;
    filter.FilterMaskIdLow = 0;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0; /* 统一送入 FIFO0。 */
    filter.FilterActivation = ENABLE;               /* 使能本过滤器。 */
    filter.SlaveStartFilterBank = 14;               /* CAN1 独立使用时保留默认分界。 */
    HAL_CAN_ConfigFilter(&Can_Handle, &filter);     /* 写入 bxCAN 过滤器。 */
}
```





### 1：FilterBank过滤器组

我用哪一个过滤器？

STM32F4 的 CAN 过滤器是编号管理的，例如：

```c
Filter Bank
    ↓
┌──────┬──────┬──────┬──────┬──────┐
│  0   │  1   │  2   │ ...  │  27  │
└──────┴──────┴──────┴──────┴──────┘
```

| **芯片类型F4** | **CAN 控制器**              | **过滤器总数**           | **FilterBank 取值范围** |
| :------------- | :-------------------------- | :----------------------- | :---------------------- |
| 单 CAN         | 只有 CAN1（或 CAN2 未引出） | 14 个，**各自独占**      | 0 ~ 13                  |
| 双 CAN         | CAN1 + CAN2                 | 28 个，**两个 CAN 共享** | 0 ~ 27                  |

所以：

```c
f.FilterBank = 0;
```

只是告诉硬件：

> “我要配置第 0 号过滤器组。”

然后：

```c
每一个 FilterBank 过滤组本身有 32 bit硬件过滤资源。
但通过 FilterScale 可以有两种使用方式：
CAN_FILTERSCALE_32BIT
    一个过滤组作为一个完整的 32 位过滤项

CAN_FILTERSCALE_16BIT
    一个过滤组拆成两个 16 位过滤项
所以不是“一个过滤组里面永远都是一个 32 位过滤项”，而是：
物理资源：一个过滤组共 32 位
使用方式：可以按 32+0，或者 16+16 使用
```







### 2：FilterMode过滤模式

这里有两个核心模式：

```c
CAN_FILTERMODE_IDMASK
CAN_FILTERMODE_IDLIST
```

可以先把它们记成：

```c
IDMASK = “按照规则筛选”

IDLIST = “按照名单筛选”
```



###### 1：IDMASK：掩码模式

通过的条件：（收到的 ID & Mask）" 等于 "（Filter ID & Mask）

比如：

```
Filter ID   = 0x1314
Mask        = 0xFFFF
```

意思不是简单地说：

> “ID 等于 0x1314。”

而是：

> Mask 哪一位是 1，就检查哪一位；Mask 哪一位是 0，就不关心哪一位。

示例：

```c
Filter ID： 0x1314 //转为二进制  0001 0011 0001 0100
Mask：      0xFFFF //转为二进制  1111 1111 1111 1111
Mask = 1    的地方必须匹配。
所以：收到can报文帧id 为 0x1314 时
0001 0011 0001 0100
1111 1111 1111 1111 相与得
0001 0011 0001 0100 ，与我们在初始化填的Filter ID（0x1314） 相等时则通过
    
如果收到的是：0001 0011 0001 0101
0001 0011 0001 0101 和1111 1111 1111 1111 相与得 0001 0011 0001 0101 
与我们要的0x1314 不一致，则不通过
    
    
Mask = 0 的bit则不进行匹配
比如：
Filter ID = 1011 0010
Mask      = 1111 0000
那么实际上只检查：1011 后面的：0010完全不管。
所以：
收到 1011 0000   → 通过
收到 1011 0101   → 通过
收到 1011 1111   → 通过
收到 1010 1111   → 不通过
```



###### 2：IDLIST：名单模式

假设你配置：

```c
ID1 = 0x100
ID2 = 0x200
```

那么：

```c
收到 0x100 → 收
收到 0x200 → 收
收到 0x101 → 不收
收到 0x300 → 不收
```

所以：

```c
IDMASK
= 给一个“规则”

IDLIST
= 给一个“名单”
```

这个理解非常重要。





### 3：FilterScale16 位还是 32 位？

```
f.FilterScale = CAN_FILTERSCALE_32BIT;
```

意思是：

> **这个过滤器组采用 32 位过滤器结构。**

另一种：

```
CAN_FILTERSCALE_16BIT
```

就是：

> **这个过滤器组采用把32位过滤组拆分为  2个16 位过滤器结构。**



比较容易理解错误的地方：

```c
32BIT 不表示项目使用 32 位 CAN-ID；
它表示一个过滤项宽度为 32 位的过滤项；
16BIT 表示一个过滤器组拆成两个 16 位过滤项；

标准帧可以用 16 位或 32 位；
扩展帧必须使用 32 位，才能完整容纳 29 位 ID 和格式位（扩展帧，远程帧，数据帧）
```





###### 实例一：过滤两个标准帧 ID，用 16 bit模式

假设我们要接收两个标准帧：

```c
0x201
0x601
```

它们都是 11 位标准 ID。

因为一个标准 ID 只有 11 位，所以一个 16 位过滤项可以容纳它。

配置思路：

```c
filter.FilterScale = CAN_FILTERSCALE_16BIT;
```

此时一个过滤器组被拆成：

```c
过滤器组 0：

FilterIdHigh       → 过滤项 A
FilterMaskIdHigh   → 过滤项 A 的掩码

FilterIdLow        → 过滤项 B
FilterMaskIdLow    → 过滤项 B 的掩码
```

可以安排成：

```
过滤项 A：匹配 0x201
过滤项 B：匹配 0x601
```

对应c语言代码：

```c
filter.FilterScale = CAN_FILTERSCALE_16BIT;

/* 过滤项 A：标准帧 0x201。 */
filter.FilterIdHigh = (uint16_t)(0x201U << 5);   //0x201转二进制010 0000 0001  左移 5 位补为16位  0100 0000 0010 0000 对应十六进制：0x4020
filter.FilterMaskIdHigh = (uint16_t)(0x7FFU << 5);//0x7FF 的 11 位二进制是0x7FF = 111 1111 1111左移 5 位后：1111 1111 1110 0000对应：0xFFE0

/* 过滤项 B：标准帧 0x601。 */
filter.FilterIdLow = (uint16_t)(0x601U << 5);	//0x601 的 11 位二进制是11000000001 左移 5 位：1100 0000 0010 0000对应十六进制： 0xC020
filter.FilterMaskIdLow = (uint16_t)(0x7FFU << 5);//0x7FF = 111 1111 1111左移 5 位补成 16 位：1111 1111 1110 0000对应十六进制：0xFFE0

这里左移 5 位，是把 11 位标准 ID 放到 16 位过滤字段的高 11 位，低 5 位留给 CAN 帧格式相关位。
//由于使用的是 16 位过滤模式，这里的 FilterIdLow 和 FilterMaskIdLow 表示第二个 16 位过滤项。
```

所以，16 位模式的优势是：

```
一个过滤器组可以同时放两个标准帧过滤项
```



###### 实例二：过滤一个扩展帧 ID，用 32 位模式

假设要接收扩展帧：

```
0x18FF50E5
```

扩展帧 ID 是 29 位：

```
29 位 ID
```

一个 16 位过滤项肯定放不下完整的 29 位 ID，所以必须使用：

```
filter.FilterScale = CAN_FILTERSCALE_32BIT;
```

此时一个过滤器组只提供一个完整的 32 位过滤比较项：

```c
过滤器组 1：

FilterIdHigh + FilterIdLow
    → 共同组成一个 32 位过滤值

FilterMaskIdHigh + FilterMaskIdLow
    → 共同组成一个 32 位掩码
```

概念上可以理解为：

```c
32 位过滤项
┌────────────────────────────────┐
│ 29 位扩展 ID + 帧格式相关位     │
└────────────────────────────────┘
//bxCAN 过滤器比较值最低的 3 位用于帧格式相关信息
bit2：IDE，标准帧还是扩展帧
bit1：RTR，数据帧还是远程帧
bit0：保留位
    
//不同组合用于判断不同帧类型
标准数据帧：
IDE = 0
RTR = 0
低 3 位 = 000b

标准远程帧：
IDE = 0
RTR = 1
低 3 位 = 010b

扩展数据帧：
IDE = 1
RTR = 0
低 3 位 = 100b

扩展远程帧：
IDE = 1
RTR = 1
低 3 位 = 110b
```

扩展 ID 要按照 bxCAN 的布局进行对齐，常见写法是：

```c
uint32_t ext_id = 0x18FF50E5U;//29 位扩展 ID 的二进制是：11000111111110101000011100101左移 3 位：11000111111110101000011100101 000
uint32_t filter_value = (ext_id << 3) | CAN_ID_EXT; //CAN_ID_EXT 通常是：CAN_ID_EXT = 0x04也就是二进制：100
//把它写入低 3 位： filter_value = (ext_id << 3) | CAN_ID_EXT; 得 1100 0111 1111 1010 1000 0111 0010 1100
```

再拆到 HAL 的两个字段：

```c
32 位过滤值：
1100 0111 1111 1010 1000 0111 0010 1100
└──── 高 16 位 ────┘└──── 低 16 位 ────┘
       0xC7FA             0x872C
    
filter.FilterIdHigh = (uint16_t)(filter_value >> 16);
filter.FilterIdLow  = (uint16_t)(filter_value & 0xFFFFU);
```

掩码也同样拆成高低 16 位：

```c
uint32_t filter_mask = (0x1FFFFFFFU << 3) | CAN_ID_EXT;//0x1FFFFFFFU是29个1：0001 1111 1111 1111 1111 1111 1111 1111
												  //左移 3 位 0xFFFFFFF8
再与 CAN_ID_EXT = 0x04 按位或：0xFFFFFFF8 | 0x00000004 = 0xFFFFFFFC //对应二进制 1111 1111 1111 1111 1111 1111 1111 1100
拆成高低 16 位：
filter.FilterMaskIdHigh = (uint16_t)(filter_mask >> 16);
filter.FilterMaskIdLow  = (uint16_t)(filter_mask & 0xFFFFU);
结果：
filter.FilterMaskIdHigh = 0xFFFF
filter.FilterMaskIdLow  = 0xFFFC
这里的低 3 位中，CAN_ID_EXT = 0x04 用来标记这是扩展帧。掩码的 0xFFFC 也会比较这个扩展帧标志位。
```

概念效果是：

```c
扩展帧 0x18FF50E5 → 通过
其他扩展帧       → 不通过
标准帧           → 不通过
```

所以，32 位模式的优势是：

```
一个过滤器组可以完整比较一个 29 位扩展帧 ID
```





###### 实例三：两个 ID是一段连续范围

```
0x200、0x201、0x202 ... 0x20F
```

它们的区别只在低 4 位，所以可以用掩码：

```
过滤 ID = 0x200
掩码    = 0x7F0
```

含义：

```
高 7 位必须等于 0x200
低 4 位不比较
```

放到 16 位标准帧过滤项中：

```c
filter.FilterIdHigh =
    (uint16_t)(0x200U << 5);

filter.FilterMaskIdHigh =
    (uint16_t)(0x7F0U << 5);
```

结果：

```
0x200 → 通过
0x201 → 通过
...
0x20F → 通过
0x210 → 不通过
```

如果同一个过滤器组还想接收 `0x601`，就可以利用第二个 16 位过滤项：

```
filter.FilterIdLow =
    (uint16_t)(0x601U << 5);

filter.FilterMaskIdLow =
    (uint16_t)(0x7FFU << 5);
```

最终一个 16 位过滤器组同时负责：

```
过滤项 A：0x200 ~ 0x20F
过滤项 B：0x601
```

这就是 16 位模式适合多个标准帧 ID 的原因。







###### 实例四：使用 **32 位掩码模式**过滤标准帧 `0x200~0x20F`

```c
static void CAN_Filter_Config(void)
{
    CAN_FilterTypeDef filter = {0};

    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;

    /*
     * 标准帧 ID 0x200 左移 5 位，放入 32 位过滤值的高 16 位。
     * 0x200~0x20F 的低 4 位不参与比较，所以掩码使用 0x7F0。
     */
    filter.FilterIdHigh = (uint16_t)(0x200U << 5);
    filter.FilterMaskIdHigh = (uint16_t)(0x7F0U << 5);

    /*
     * bit2 = IDE：0 表示标准帧
     * bit1 = RTR：0 表示数据帧
     * 0x0006 表示同时检查 IDE 和 RTR。
     */
    filter.FilterIdLow = 0x0000U;
    filter.FilterMaskIdLow = 0x0006U;
	
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;

    HAL_CAN_ConfigFilter(&Can_Handle, &filter);
}
```

完整的 32 位掩码可以理解为：

```c
FilterMask =
    (FilterMaskIdHigh << 16) | FilterMaskIdLow
          ↓
    (0xFE00 << 16) | 0x0006
          ↓
    0xFE000006
```

其中：

```c
高 16 位 0xFE00
    用于比较标准 CAN-ID 的高 7 位

低 16 位 0x0006
    用于比较 IDE 和 RTR
```

ID 匹配关系是：

```
过滤 ID   = 0x200
掩码      = 0x7F0
```

因此：

```c
0x200 & 0x7F0 = 0x200  → 通过
0x201 & 0x7F0 = 0x200  → 通过
0x205 & 0x7F0 = 0x200  → 通过
0x20F & 0x7F0 = 0x200  → 通过

0x210 & 0x7F0 = 0x210  → 不通过
```

所以最终效果是：

```c
标准数据帧 0x200~0x20F → 进入 FIFO0
标准数据帧 0x210       → 被过滤
扩展帧                  → 被过滤
标准远程帧              → 被过滤
```

这里特别注意 32 位模式下：

```
FilterIdHigh + FilterIdLow
```

是一个完整的 32 位过滤项的高、低两部分；它们不是两个独立的 16 位过滤器。

也就是说：

```c
32 位模式：

FilterIdHigh       → 完整过滤值的高 16 位
FilterIdLow        → 完整过滤值的低 16 位
FilterMaskIdHigh   → 完整掩码的高 16 位
FilterMaskIdLow    → 完整掩码的低 16 位
```

而在 16 位模式下，才是：

```c
FilterIdHigh       → 第一个过滤项
FilterIdLow        → 第二个过滤项
```

这就是两种过滤宽度下 `High/Low` 含义的区别。



### 4：FilterFIFOAssignment：FIFO存储器

bxCAN 有两个接收 FIFO：

```c
FIFO0
FIFO1
```

当前数据流是：

```c
CAN 总线收到帧
        ↓
硬件过滤器匹配成功
        ↓
放入 FIFO0
        ↓
触发 CAN1_RX0_IRQHandler()
        ↓
HAL_CAN_IRQHandler()
        ↓
HAL_CAN_RxFifo0MsgPendingCallback()
```

所以它必须和后面的读取代码对应：

```
HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, ...);
```

也必须和中断入口对应：

```c
void CAN1_RX0_IRQHandler(void)
{
    HAL_CAN_IRQHandler(&Can_Handle);
}
```

如果改成：

```
filter.FilterFIFOAssignment = CAN_FILTER_FIFO1;
```

那么帧会进入 FIFO1，后续就应该使用 FIFO1 对应的中断和读取方式。否则会出现：

```c
帧已经进入 FIFO1
程序却一直读取 FIFO0
```

最终表现就是“总线有帧，但应用层收不到”。

当前项目统一使用 FIFO0，是因为阶段7先建立一条简单接收链路：

```
FIFO0 → CAN1_RX0_IRQHandler → BSP 接收队列
```







### 5：FilterActivation：是否启用过滤器

```
ENABLE  // 启用
DISABLE // 禁用
```

当前设置为：

```
filter.FilterActivation = ENABLE;
```

表示调用：

```
HAL_CAN_ConfigFilter(&Can_Handle, &filter);
```

之后，这个过滤器组立即参与 CAN 接收判断。

如果写成：

```
filter.FilterActivation = DISABLE;
```

那么即使前面配置了：

```
FilterBank
FilterMode
FilterScale
FilterId
FilterMask
```

这个过滤器也不会正常参与匹配。

可以理解为：

```
FilterMode / FilterScale / ID / Mask
    决定“怎么过滤”

FilterActivation
    决定“这个过滤器是否开机工作”
```

注意，它控制的是**当前过滤器组**，不是整个 CAN 外设的总开关。CAN 外设是否启动由：

```
HAL_CAN_Start(&Can_Handle);
```

决定。







### 6：SlaveStartFilterBank：分配过滤器组

这个成员用于 CAN1 和 CAN2 共用过滤器组的情况。

STM32F429 的 bxCAN 过滤器组通常编号为：

```
FilterBank 0 ~ 27
```

CAN1 和 CAN2 共用这 28 组过滤器，但需要划分边界：

```c
CAN1 使用边界前的过滤组
CAN2 使用边界后的过滤组
```

`SlaveStartFilterBank` 表示：

> 从哪一个过滤器组开始，分配给从 CAN，也就是 CAN2。

设置为：

```
filter.SlaveStartFilterBank = 14;
```

可以理解为：

```
CAN1：FilterBank 0 ~ 13
CAN2：FilterBank 14 ~ 27
```

当前项目只使用：

```
CAN1
```

并且当前只用到了过滤组0。

```
filter.FilterBank = 0;
```







# 4：can外设通信比特率设置

```c
void CAN_Config(void)
{
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
}
```

可以先分成三组：

```c
外设选择：
    Can_Handle.Instance

通信行为：
    TimeTriggeredMode
    AutoBusOff
    AutoWakeUp
    AutoRetransmission
    ReceiveFifoLocked
    TransmitFifoPriority
    Mode

位时序：
    SyncJumpWidth
    TimeSeg1
    TimeSeg2
    Prescaler
最后由：HAL_CAN_Init(&Can_Handle);把这些配置写入 CAN1 的硬件寄存器。
```





### 1：TimeTriggeredMode时间触发模式

全名可以拆成：

```c
TimeTriggeredMode
│      │       │
│      │       └── Mode：模式
│      └────────── Triggered：触发
└───────────────── Time：时间
```

就是：

> 时间触发模式（Time Triggered Communication）。

普通 CAN 通信一般是：

```c
有报文要发送
      ↓
CAN开始发送
```

而时间触发通信则引入了**时间信息/时间基准**，让 CAN 节点可以根据预定的时间关系进行通信。



当前项目中CAN 工作方式还是普通的事件驱动式 CAN：

```c
程序产生数据
      ↓
调用 CAN 发送
      ↓
CAN 外设发送报文
```

而不是：

```c
系统时间到达某个时间点
        ↓
触发 CAN 通信
```



假设开启：

STM32 CAN 外设可以使用它内部的时间触发机制，在 CAN 报文发送/接收过程中记录相关的时间信息。

这个功能主要和一些**时间同步、时间调度、确定性通信**场景有关。





### 2：AutoBusOff自动 Bus-off 管理

CAN 节点如果持续发送错误，错误计数器可能增加，最终进入：Bus-off

Bus-off 表示 CAN 控制器暂时退出总线，不能继续正常发送。

开启 `AutoBusOff` 后，允许硬件自动执行 Bus-off 恢复流程。恢复条件满足后，CAN 控制器可以重新参与总线通信。

可以简单理解为：

```c
AutoBusOff = ENABLE
    ↓
发生严重总线错误进入 Bus-off
    ↓
硬件按 CAN 规范等待恢复条件
    ↓
自动恢复通信
```

如果关闭：

```
AutoBusOff = DISABLE;
```

进入 Bus-off 后通常需要软件处理恢复，例如：

```
停止 CAN
清理错误
重新初始化或重新启动 CAN
```

当前项目还会通过：

```
can_bus_off_count
```

统计 Bus-off 次数。

所以：

```
自动恢复 ≠ 不记录故障
```

即使硬件自动恢复，也应该保留错误统计，方便诊断接线、波特率、终端电阻或收发器问题。







### 3：AutoWakeUp自动唤醒

CAN 外设可能因为总线处于空闲状态而进入睡眠或低功耗状态。开启自动唤醒后，检测到总线活动时，允许 CAN 控制器自动恢复工作。

```c
AutoWakeUp = ENABLE
    ↓
CAN 处于休眠/低功耗
    ↓
总线上出现活动
    ↓
CAN 自动唤醒
```

当前项目阶段暂时没有设计 CAN 低功耗流程，所以这个配置不是核心功能，但开启它不会改变正常模式下的普通收发







### 4：AutoRetransmission 自动重发

自动重发：

CAN 发送不是把数据写入邮箱就结束了。发送过程中如果出现：

```c
没有收到 ACK
仲裁丢失后需要再次发送
总线错误
发送错误
```

CAN 控制器可能需要重新发送。

开启自动重发后：

```c
发送帧进入邮箱
    ↓
发送失败或未完成
    ↓
CAN 控制器自动尝试重发
```

如果关闭自动重发：

```
AutoRetransmission = DISABLE;
```

发送失败后，通常由软件决定是否再次提交。

当前项目开启自动重发，是为了让 CAN 控制器按照 CAN 硬件机制自动处理普通发送失败。







### 5：ReceiveFifoLocked 接收 FIFO 满时是否锁定

这个配置控制接收 FIFO 满了以后，新到的帧如何处理。

先看 FIFO 正常状态：

```
FIFO 有空位
    ↓
新帧进入 FIFO
```

如果 FIFO 已满，就要决定：

```
丢新帧？
还是丢旧帧，保存新帧？
```

在 bxCAN 中通常可以这样理解：

```c
ReceiveFifoLocked = ENABLE
    FIFO 满后锁定
    后续新帧被丢弃
    FIFO 中已有旧帧保留

ReceiveFifoLocked = DISABLE
    FIFO 不锁定
    FIFO 满时允许新消息覆盖最旧消息
```

当前设置：

```c
ReceiveFifoLocked = DISABLE;
```

意思是更偏向保留最新总线状态，FIFO 满时允许丢弃较旧的消息。

但是这里要区分两层 FIFO：

```c
bxCAN 硬件 FIFO
    这里的 ReceiveFifoLocked 控制

BSP 软件环形队列
    rx_queue.frames[16]
    由 can_rx_overflow_count 统计
```

所以当前完整接收链路里可能有两处溢出：

```c
硬件 FIFO 满
    由 bxCAN 硬件处理

硬件 FIFO 取出后，软件队列满
    由 can_rx_overflow_count 统计
```

当前注释“FIFO 满时允许覆盖旧消息策略”表达的是 `DISABLE` 对应的硬件行为。





### 6：TransmitFifoPriority 发送邮箱优先级

bxCAN 有多个发送邮箱。多个邮箱同时有待发送帧时，需要决定发送顺序。

当设置为：

```
DISABLE
```

通常使用 CAN 的自然仲裁机制：

```
CAN-ID 数值较小的帧
    优先级更高
    更容易先获得总线
```

例如：

```
0x081 Heartbeat
0x181 TPDO1
0x581 SDO 响应
```

如果它们同时竞争总线，CAN 总线自身会根据帧 ID 进行仲裁。

这符合 CAN 的基本规则：

```
显性位 0 优先于隐性位 1
帧 ID 越小，通常优先级越高
```

如果设置为：

```
TransmitFifoPriority = ENABLE;
```

则更接近按照发送请求进入邮箱的先后顺序处理，而不优先根据 CAN-ID 排列。

当前设置为 `DISABLE`，让 CAN-ID 仲裁发挥作用。项目中：

```
Heartbeat：0x701
TPDO1：     0x181
TPDO2：     0x281
SDO响应：   0x581
```

发送优先级最终仍由 CAN 总线仲裁决定。

注意：

```
TransmitFifoPriority
    影响同一控制器多个发送邮箱之间的发送顺序

CAN 总线仲裁
    决定多个节点同时发送时谁最终获得总线
```





### 7：Mode，CAN 工作模式设置

当前使用：

```
Normal 模式
```

表示 CAN1 连接真实 CAN 收发器和总线，正常参与：

```
发送
接收
仲裁
ACK
错误处理
```

常见模式还有：

```
CAN_MODE_LOOPBACK
```

内部回环模式用于初期自测：

```
CAN 控制器发送
    ↓
内部直接回到自己的接收路径
```

不需要外部 CAN 总线就可以验证：

```
发送邮箱
接收 FIFO
接收中断
HAL 回调
帧转换
```

当前阶段先使用 Normal 模式，是因为工程已经准备接入真实 CANopen 总线。但如果后续做最小链路测试，也可以暂时改成 Loopback。

两者区别：

| 模式                | 外部总线 | 是否需要其他节点 ACK | 主要用途               |
| ------------------- | -------- | -------------------- | ---------------------- |
| `CAN_MODE_NORMAL`   | 需要     | 需要                 | USB-CAN 或真实节点通信 |
| `CAN_MODE_LOOPBACK` | 不需要   | 不依赖外部节点       | 内部收发链路自测       |





# 5：位时序配置

```c
完整时序是：
Sync_Seg = 1 TQ        // 固定，不由某个 Init 字段填写
BS1      = 11 TQ       // TimeSeg1 配置
BS2      = 3 TQ        // TimeSeg2 配置
SJW      = 1 TQ        // SyncJumpWidth 配置
```







### 1:0.5Mbps来源

```c
当前项目的计算应该是：
APB1 = 45 MHz
Prescaler = 6
CAN TQ 时钟 = 45 MHz ÷ 6 = 7.5 MHz

Sync_Seg  = 1 TQ
TimeSeg1  = 11 TQ
TimeSeg2  = 3 TQ

每个 CAN bit = 1 + 11 + 3 = 15 TQ

CAN 波特率 = 7.5 MHz ÷ 15
           = 500 kbit/s
           = 0.5 Mbps
因此当前 CANopen 工程中的配置：
Can_Handle.Init.Prescaler = 6;
Can_Handle.Init.TimeSeg1 = CAN_BS1_11TQ;
Can_Handle.Init.TimeSeg2 = CAN_BS2_3TQ;
Can_Handle.Init.SyncJumpWidth = CAN_SJW_1TQ;
对应：
500 kbit/s = 0.5 Mbps
```





CAN 的每一个 bit 不是一个简单的高低电平，而是由多个时间量子组成。

当前配置：

```c
Sync_Seg = 1 TQ
TimeSeg1 = 11 TQ
TimeSeg2 = 3 TQ
```

一个 CAN bit 总共：

```c
1 + 11 + 3 = 15 TQ
```







### 2:Sync_Seg 

Sync_Seg 是 CAN 位时序规定的固定同步段。它在 STM32 HAL 的 `CAN_InitTypeDef` 中没有单独的配置字段，通常固定为：

```
Sync_Seg = 1 TQ
```

用来定义一个 CAN bit 的起始同步段



### 3:BS1

给信号传播和采样点前的同步留出时间；



### 4:BS2

位于采样点之后，为相位调整提供余量；



### 5:SJW 

表示一次重新同步最多可以调整多少个 TQ

当前 SJW 的含义

```
Can_Handle.Init.SyncJumpWidth = CAN_SJW_1TQ;
```

表示：

```
一次重新同步最多调整 1 TQ
```

例如某个同步边沿相对于本地时钟存在偏差：

```
实际误差小于或等于 1 TQ
```

控制器可以在本次 bit 时序中进行相应调整。

如果理论偏差超过 1 TQ：

```
不能一次全部修正
```

但不能简单理解为“完全不修正”，实际行为还要遵循 CAN 控制器的同步规则，后续边沿可能继续帮助校正。





### 6:逻辑

一个 bit 的结构是：

```
| Sync_Seg | TimeSeg1 | 采样点 | TimeSeg2 |
|   1 TQ   |   BS1     |        |   BS2    |
```

当前 CANopen 项目配置：

```
Sync_Seg = 1 TQ
BS1      = 11 TQ
BS2      = 3 TQ
```

因此：

```
一个 bit = 1 + 11 + 3 = 15 TQ
```

当前 CANopen 项目的 bit 时序可以画成：

```c
一个 CAN bit，共 15 TQ

起点                                           采样点
 ↓                                                ↓
┌────────┬────────────────────────────────────────┬────────────┐
│ Sync   │                 BS1                    │    BS2     │
│ 1 TQ   │                11 TQ                   │    3 TQ    │
└────────┴────────────────────────────────────────┴────────────┘
                                                   ↑
                                             在 80% 处采样
```

当前项目的采样点

采样点位于：

```c
Sync_Seg + BS1
----------------
总 TQ 数
```

代入当前配置：

```
(1 + 11) ÷ 15
= 12 ÷ 15
= 80%
```







### 7:项目总览

```c
当前 CANopen IO Node 使用 CAN1，APB1 时钟为 45 MHz，
CAN 波特率要求为 500 kbit/s。

配置：
    Prescaler = 6
    Sync_Seg  = 1 TQ
    TimeSeg1  = 11 TQ
    TimeSeg2  = 3 TQ
    SJW       = 1 TQ

CAN 时间量子频率：
    45 MHz ÷ 6 = 7.5 MHz

一个 CAN bit 的时间量子数：
    1 + 11 + 3 = 15 TQ

CAN 波特率：
    7.5 MHz ÷ 15 = 500 kbit/s

采样点：
    (1 + 11) ÷ 15 = 80%

Sync_Seg 是每个 CAN bit 的起始同步段，
不是独立发送的数据 bit。

TimeSeg1 位于采样点之前，
用于容纳信号传播延迟、输入输出延迟和相位同步余量。

TimeSeg2 位于采样点之后，
用于采样点后的相位缓冲，并配合重新同步调整。

SJW=1TQ 表示一次重新同步最多调整 1TQ。
它不是永久修改 BS1、BS2 的初始化值，
而是 CAN 控制器运行时对当前 bit 时序进行的临时调整。
```





# 6：中断触发源

代码：

```c
HAL_CAN_ActivateNotification(&Can_Handle, /* 开启 HAL 通知源。 */
                                 CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_ERROR | CAN_IT_BUSOFF);

    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 6, 0); /* 可安全调用 FreeRTOS API 的中断优先级。 */
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);         /* 开启 FIFO0 中断。 */
    HAL_NVIC_SetPriority(CAN1_SCE_IRQn, 6, 0); /* 错误中断使用相同优先级。 */
    HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);         /* 开启状态变化错误中断。 */
```

可以整理成三层：

```c
① CAN通知源
   ↓
哪些事件可以产生中断请求？
   ├─ FIFO0收到报文
   ├─ CAN错误
   └─ Bus-Off

② NVIC
   ↓
CPU是否允许响应这些中断？
   ├─ CAN1_RX0_IRQn   → FIFO0接收中断
   └─ CAN1_SCE_IRQn   → 状态/错误中断

③ 中断优先级
   ↓
如果多个中断同时来，谁优先处理？
    
对应你的代码：

HAL_CAN_ActivateNotification(...);  // ① 开启事件通知

HAL_NVIC_SetPriority(...);          // ③ 设置优先级
HAL_NVIC_EnableIRQ(...);             // ② 开启NVIC通道

3 个事件不是对应 2 个 NVIC 中断号，两个是错误处理事件
RX_FIFO0_MSG_PENDING → CAN1_RX0_IRQn

ERROR + BUSOFF        → CAN1_SCE_IRQn
```





# 7：发送can报文帧至can邮箱

```c
co_status_t CAN_SendFrame(void *user, const can_frame_t *frame)
{
    /*1:定义帧头结构体*/
    CAN_TxHeaderTypeDef header = {0};	
    /*2：定义一个变量来接收使用的邮箱编号返回值*/
    uint32_t mailbox;  
    /*3：防止编译警告，此处不需要模拟发送函数fake了*/
    (void)user;                       									 
	/*4：检查*/
    if (frame == NULL) 										   //拒绝空帧指针								
        return CO_ERR_ARGUMENT;
    if (frame->is_extended || frame->is_remote || frame->is_fd || 
        frame->id > CO_CAN_ID_MAX || frame->dlc > 8u)             // 协议只允许标准经典数据帧
        return CO_ERR_FRAME_TYPE;
    if (HAL_CAN_GetTxMailboxesFreeLevel(&Can_Handle) == 0u)       // 检查三个邮箱是否全满
    {
        can_tx_busy_count++;                                      // 记录暂忙，调用者负责后续重试
        return CO_ERR_TX_BUSY;
    }
	
    /*5：对创建的帧头结构体赋值*/
    header.StdId = frame->id;                                               /* 填入 11 位标准 CAN-ID。 */
    header.IDE = CAN_ID_STD;                                                /* 填入标准帧。 */
    header.RTR = CAN_RTR_DATA;                                              /* 填土数据帧。 */
    header.DLC = frame->dlc;                                                /* 填入有效数据长度。 */
    header.TransmitGlobalTime = DISABLE;                                    /* 不使用时间戳功能。 */
    if (HAL_CAN_AddTxMessage(&Can_Handle, &header,                          /* 提交帧头信息和帧数据到邮箱。 */
                             (uint8_t *)frame->data, &mailbox) != HAL_OK)
        return CO_ERR_TX_FAILED;                                           /* HAL 提交失败。 */
    return CO_OK;                                                          /* 已成功进入发送邮箱。 */
}
```

形参和返回值：

```c
co_status_t：返回发送结果。
user：通用用户参数，当前项目没有使用。
frame：待发送的协议层 CAN 帧。
const：函数只能读取 frame，不能修改它里面的内容。
```







### pc端对应代码：

```c
typedef struct
{
    unsigned calls;
    can_frame_t frame;
    co_status_t result;
} fake_bus_t;

static co_status_t fake_tx(void *user, const can_frame_t *frame)
{
    fake_bus_t *bus = user;

    bus->calls++;
    bus->frame = *frame;
    return bus->result;
}

fake_bus_t bus = {0};

co_init(&ctx,1, fake_tx,&bus);//fake_tx发送函数指针地址，模拟pc测试结构体bus，存发送结果status，存发送的报文帧，存发送回调的次数
       

```

初始化后：

```
ctx.tx = fake_tx;
ctx.tx_user = &bus;
```

因此：

```
ctx->tx(ctx->tx_user, frame);
```

等价于：

```
fake_tx(&bus, frame);
```

进入 `fake_tx()` 后：

```
fake_bus_t *bus = user;
```

把通用的 `void *user` 还原成 `fake_bus_t *`，然后：

```
bus->calls++;
bus->frame = *frame;
return bus->result;
```

PC 端的报文被复制到了：

```
bus.frame
```







### 实机端对应代码：

```c
co_status_t CAN_SendFrame(void *user,
                          const can_frame_t *frame)
{
    CAN_TxHeaderTypeDef header = {0};
    uint32_t mailbox;

    (void)user;

    header.StdId = frame->id;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = frame->dlc;

    return HAL_CAN_AddTxMessage(&Can_Handle,
                                &header,
                                frame->data,
                                &mailbox) == HAL_OK
           ? CO_OK
           : CO_ERR_TX_FAILED;
}
co_init(&ctx,1,CAN_SendFrame,NULL); 
      
       CAN_SendFrame   /* 保存 CAN_SendFrame 函数地址 */
       NULL          /* 不需要额外的模拟记录 user 结构体 */
```

初始化后：

```
ctx.tx = CAN_SendFrame;
ctx.tx_user = NULL;
```

所以：

```
ctx->tx(ctx->tx_user, frame);
```

等价于：

```
CAN_SendFrame(NULL, frame);
```

进入 `CAN_SendFrame()` 后：

```
(void)user;
```

表示当前不使用 `NULL`，然后直接使用 BSP 的全局句柄：

```
Can_Handle
```

最终由：

```
HAL_CAN_AddTxMessage(&Can_Handle,
                     &header,
                     frame->data,
                     &mailbox);
```

把报文交给 CAN1 的硬件发送邮箱。







### 逻辑捋清：



###### 1：实机为什么不需要模拟发送结构体：

实机端不再需要模拟发送结构体了，

```c
typedef struct
{
    unsigned calls;     /* 记录发送回调被调用的次数 */
    can_frame_t frame;  /* 保存最近一次提交报文的副本 */
    co_status_t result; /* 预设发送回调的返回状态 */
} fake_bus_t;
```

```c
fake_bus_t  bus；
co_init(&ctx,1, fake_tx,&  bus);	//pc端
co_init(&ctx,1,CAN_SendFrame,NULL); //实机端，这里是直接发到can的邮箱去了，
```



 

###### 2：实例

```c
#include "bsp_can.h"

void Test_CAN_Send(void)
{
    can_frame_t frame = {0};
    co_status_t status;

    frame.id = 0x701;          /* 节点 1 的 Heartbeat CAN-ID。 */
    frame.dlc = 1;             /* 只有 1 个有效数据字节。 */
    frame.data[0] = 0x05;      /* Operational 状态。 */

    frame.is_extended = 0;     /* 标准帧。 */
    frame.is_remote = 0;       /* 数据帧。 */
    frame.is_fd = 0;           /* 经典 CAN。 */

    status = CAN_SendFrame(NULL, &frame);

    if (status == CO_OK)
    {
        /* 帧已经成功放入 CAN 发送邮箱。 */
    }
}
//创建报文帧 frame 
//接受返回状态 status    的变量不在函数CAN_SendFrame（）内部创建。

然后CAN_SendFrame（）函数内部的实现过程我们做一下解析
CAN_TxHeaderTypeDef header = {0};
uint32_t mailbox;
(void)user;

header.StdId = frame->id;
header.IDE = CAN_ID_STD;
header.RTR = CAN_RTR_DATA;
header.DLC = frame->dlc;

return HAL_CAN_AddTxMessage(&Can_Handle,
                            &header,
                            frame->data,
                            &mailbox) == HAL_OK
       ? CO_OK
       : CO_ERR_TX_FAILED;
//CAN_SendFrame（）函数创建了一个帧头 header，一个接收返回值的 mailbox 邮箱变量。
```





###### 3：header

它不保存完整报文数据，主要保存 CAN 帧的控制信息：

```c
header.StdId = frame->id;      /* CAN-ID */
header.IDE = CAN_ID_STD;       /* 标准帧 */
header.RTR = CAN_RTR_DATA;     /* 数据帧 */
header.DLC = frame->dlc;       /* 数据长度 */
例如协议层报文是：
frame->id = 0x701;
frame->dlc = 1;
frame->data[0] = 0x05;
组装后：
header.StdId = 0x701
header.IDE   = 标准帧
header.RTR   = 数据帧
header.DLC   = 1
frame->data  = 05
所以：
header     保存帧头控制信息
frame->data 保存实际数据
    
这些信息填写完毕以后，在函数HAL_CAN_AddTxMessage（）中发出去，形参的第二个和第三个填的就是&header和frame->data
```



###### 4：mailbox 

```c
这个变量不是邮箱本身，也不保存报文数据。
它只是让 HAL 返回：
本次使用了第几个发送邮箱
例如：
mailbox = 0
表示使用发送邮箱 0；也可能返回邮箱 1 或邮箱 2。
    
我们在HAL_CAN_AddTxMessage（）第四个参数填入&mailbox，就是让他把本次采用的邮箱编号返回回来
    
```





###### 5：全局句柄CAN1

把全局 CAN HAL 句柄的地址传给 HAL。这个句柄中包含：

```
Can_Handle.Instance = CAN1;
```

所以 HAL 通过它知道要操作 CAN1。

真正提交发送的是：

```
HAL_CAN_AddTxMessage(&Can_Handle,
                     &header,
                     frame->data,
                     &mailbox);
```

四个参数分别是：

```
&Can_Handle  → 操作哪个 CAN 外设
&header      → 帧头控制信息
frame->data  → 要发送的数据
&mailbox     → 返回使用的邮箱编号
```

执行后，HAL 会把帧头和数据复制到 CAN1 的硬件发送邮箱中：

```
header + frame->data
        ↓
CAN1 硬件发送邮箱
        ↓
CAN 控制器发送到总线
```

最后这段：

```
return HAL_CAN_AddTxMessage(...) == HAL_OK
       ? CO_OK
       : CO_ERR_TX_FAILED;
```

使用的是 C 语言三目运算符，等价于：

```
if (HAL_CAN_AddTxMessage(...) == HAL_OK)
{
    return CO_OK;
}
else
{
    return CO_ERR_TX_FAILED;
}
```

其中：

```
HAL_OK
```

表示 HAL 已经成功把报文提交到发送邮箱。





###### 6：局部变量和全局变量

这三个变量的关系可以这样记：

```c
Can_Handle
    全局变量
    长期存在
    指向并管理 CAN1

header
    局部变量
    保存本次发送的帧头信息

mailbox
    局部变量
    接收本次使用的邮箱编号
```

函数返回后：

```
header 和 mailbox 被释放
```

但报文已经被 HAL 写入：

```
CAN1 的硬件发送邮箱
```

所以它们不需要继续保留。



###### 7：总览

```c
HAL_CAN_AddTxMessage(&Can_Handle,
                     &header,
                     frame->data,
                     &mailbox);
```

这个函数需要：

- `&header`：读取本次发送的帧头信息；
- `frame->data`：读取要发送的数据；
- `&mailbox`：写入实际使用的邮箱编号。

函数执行期间，HAL 会读取：

```c
header
frame->data
```

然后把它们复制到 CAN 外设的硬件发送邮箱中。

因此，函数返回后：

```c
header		//这个局部变量可以释放，因为帧头信息已经被硬件邮箱接收。
```

```c
mailbox		//这个局部变量也可以释放，因为它只是保存“本次用了哪个邮箱”的返回结果。当前项目发送成功后不需要再根据邮箱编号做其他操作。
```

所以当前代码中：

```
uint32_t mailbox;
```

虽然是局部变量，但完全没有问题。

不过邮箱编号并不是永远没用。如果以后需要：

- 查询某个邮箱是否发送完成；
- 取消指定邮箱中的发送；
- 做发送完成统计；
- 管理多个待发送报文；

就可以保存这个编号，并在函数外继续使用。

当前流程是：

```
header 和 frame->data
        ↓
HAL_CAN_AddTxMessage()
        ↓
写入 CAN 硬件发送邮箱
        ↓
函数返回
        ↓
header 和 mailbox 局部变量释放
```

报文本身已经进入硬件邮箱，所以局部变量释放不会影响后续发送。

还要注意，`HAL_CAN_AddTxMessage()` 不会保存 `header` 或 `frame->data` 的指针，供以后慢慢读取；它是在函数调用期间立即读取并写入硬件。因此局部变量可以安全使用。







# 8：can邮箱取出一帧至接收环形队列

作用：HAL FIFO0 有待处理报文时，把硬件帧（can邮箱的报文帧）复制到协议层的软件环形队列



### 1：认识函数

RxFifo0

```c
表示这是 STM32 HAL 库中 CAN 外设相关的函数。
RxFifo0
表示：
Rx = Receive，接收
FIFO0 = 接收 FIFO 0
CAN 接收数据通常先进入硬件 FIFO。当前项目使用的是：
CAN_FILTER_FIFO0
所以收到的报文会进入 CAN 的 FIFO0。
```

Message Pending

```c
表示：
FIFO0 中有一条或多条报文等待读取
注意，它表示“有报文等待处理”，不表示已经读取完报文。
```

Callback

```c
表示这是一个回调函数。
完整含义就是：
当 CAN 接收 FIFO0 中有报文等待读取时，
HAL 库调用这个回调函数。
```

调用链大致是：

```c
CAN 总线收到报文
        ↓
报文进入 CAN1 FIFO0
        ↓
触发 CAN1_RX0_IRQHandler()			void CAN1_RX0_IRQHandler(void)
		↓						    {
 HAL_CAN_IRQHandler(&Can_Handle)  			 HAL_CAN_IRQHandler(&Can_Handle);
		 ↓							}
HAL 判断 FIFO0 有待处理报文        
        ↓
HAL_CAN_RxFifo0MsgPendingCallback(&Can_Handle)
```

这个函数不是我们主动调用的，通常由 HAL 在中断处理流程中自动调用。

HAL 内部一般已经提供了一个弱定义：

```c
__weak void HAL_CAN_RxFifo0MsgPendingCallback(
    CAN_HandleTypeDef *hcan)
{
}
```

我们在自己的工程中重新定义同名函数：

```c
void HAL_CAN_RxFifo0MsgPendingCallback(
    CAN_HandleTypeDef *hcan)
{
    /* 编写自己的接收处理代码 */
}
```

链接工程时，用户工程中的普通定义会覆盖 HAL 提供的弱定义，于是收到 CAN 报文后就会进入我们写的版本。

所以这个函数名可以直接记成：

```c
HAL_CAN_RxFifo0MsgPendingCallback
=
HAL_CAN 接收 FIFO0 有待处理报文回调
```





### 2：函数体实现

```c
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    uint8_t next; /* 这是本次中断处理中使用的临时变量，用来保存：当前写入位置的下一个索引它是局部变量，因为只在本次回调中计算和使用。 */

    /*1：从FIFO读取报文帧*/
    if (hcan != &Can_Handle) /* 如果以后还有 CAN2，HAL 也可能调用同一个回调函数，但传入的是 CAN2 的句柄。此时直接返回，避免误处理其他 CAN 实例的报文 */
        return;
    /*hcan→ 哪个 CAN 外设，CAN_RX_FIFO0 → 从 FIFO0 读取，&rx_header  → 把帧头写入 rx_header，rx_data     → 把数据写入 rx_data[8]*/
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK) /* 这一步从硬件 FIFO0 读取一帧报文。 */
        return;                                                                  // 如果读取失败：直接退出中断回调，不继续处理无效数据。

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
```



###### 1：next变量的计算

```c
next = (uint8_t)((rx_queue.head + 1u) % 16u);
```

当前队列定义是：

```
can_frame_t frames[16];
```

所以有效索引是：

```
0 到 15
```

例如：

```c
head = 0时  next = (uint8_t)((rx_queue.head + 1u) % 16u); → next = 1
head = 5时  next = (uint8_t)((rx_queue.head + 1u) % 16u); → next = 6
head = 15时 next = (uint8_t)((rx_queue.head + 1u) % 16u); → next = 0
```

`% 16u` 的作用就是到达数组末尾后回到 0。

这里的 `head` 是当前写入位置：

```
rx_queue.head
```

`next` 是写完当前帧之后，准备移动到的位置。





###### 2：判断队列是否已满

判断队列是否已满：

```c
if (next == rx_queue.tail)
{
    can_rx_overflow_count++;
    return;
}
```

环形队列中：

- `head` 指向下一个要写入的位置；
- `tail` 指向下一个要读取的位置。

当：

```c
next == tail
```

说明如果再写一帧，就会追上还没有被任务读取的数据，因此队列已满。

这时代码选择：

```c
can_rx_overflow_count++;
return;
```

也就是：

1. 记录一次溢出；
2. 丢弃当前收到的这一帧；
3. 不覆盖队列中尚未处理的旧帧。

因此数组虽然有 16 个元素，但这种设计实际最多保存 15 帧。空出一个位置，是为了区分：

```c
head == tail
```

到底表示“队列为空”，还是“队列已满”。







###### 3：完整示例

```c
static CAN_RxHeaderTypeDef rx_header;// 暂存HAL_CAN_GetRxMessage（）函数 读取出来的 CAN 帧头
static uint8_t rx_data[8];			// 暂存HAL_CAN_GetRxMessage（）函数读取出来的 CAN 数据

typedef struct
{
    can_frame_t frames[16];			//队列中最多可保存 15 个 can_frame_t报文帧
    volatile uint8_t head;			//中断回调中，暂存HAL_CAN_GetRxMessage（）下一次写入的frames[]数组的索引
    volatile uint8_t tail;			//任务下一次读取的frames[]数组的索引
} can_rx_queue_t;					//软件接收队列声明

static can_rx_queue_t rx_queue;//软件接收队列

以上变量均为`static` 变量，因此它们在整个 `bsp_can.c` 文件运行期间一直存在。
```



假设 CAN 总线上收到这帧：

```c
CAN-ID：0x601
类型：标准数据帧
DLC：3
数据：11 22 33
```

此时假设软件队列当前状态是：

```c
rx_queue.head = 2
rx_queue.tail = 0
```

表示下一帧要写入：

```c
rx_queue.frames[2] //head为2，下一次写入就是写入队列报文帧数组中的第3个，也就是frames[2]4
当 FIFO0 有报文时，HAL 自动调用：
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
进入回调后，先执行：
HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0,&rx_header, rx_data);
执行完成后，文件级变量里面变成：                   
rx_header.StdId = 0x601
rx_header.DLC   = 3
rx_header.IDE   = CAN_ID_STD
rx_header.RTR   = CAN_RTR_DATA                     
rx_header.fd    =0       //bxCAN 只支持经典 CAN
rx_data[0] = 0x11
rx_data[1] = 0x22
rx_data[2] = 0x33
然后计算：
next = (uint8_t)((rx_queue.head + 1u) % 16u);
当前：
head = 2
所以：
next = 3
接下来把暂存区内容复制到队列的第 2 个元素：因为head=2
rx_queue.frames[rx_queue.head].id = rx_header.StdId;   /* 转换标准 CAN-ID。 */
这里假设软件队列的状态是：rx_queue.head = 2
那就是rx_queue.frames[2].id = rx_header.StdId; //此时rx_header.StdId 就是接受到的报文帧id，也就是0x0x601

//等价于 
rx_queue.frames[2].id = 0x601
//下面同理
rx_queue.frames[rx_queue.head].dlc = rx_header.DLC; 等价于 rx_queue.frames[2].dlc = 3;
rx_queue.frames[rx_queue.head].is_extended =(rx_header.IDE == CAN_ID_EXT);收到的是标准帧，因此：rx_queue.frames[2].is_extended = 0;
rx_queue.frames[rx_queue.head].is_remote = (rx_header.RTR == CAN_RTR_REMOTE);  收到的是数据帧，因此：rx_queue.frames[2].is_remote = 0;
rx_queue.frames[rx_queue.head].is_fd = 0;    bxCAN 只支持经典 CAN，因此：rx_queue.frames[2].is_fd = 0;

//随后
复制 3 个有效数据字节：
for (uint8_t i = 0; i < rx_header.DLC; i++)
{
    rx_queue.frames[rx_queue.head].data[i] = rx_data[i];
}
等价于：
rx_queue.frames[2].data[0] = rx_data[0]; /* 0x11 */
rx_queue.frames[2].data[1] = rx_data[1]; /* 0x22 */
rx_queue.frames[2].data[2] = rx_data[2]; /* 0x33 */

于是队列第 2 个元素变成：
rx_queue.frames[2]
├── id          = 0x601
├── dlc         = 3
├── data[0]     = 0x11
├── data[1]     = 0x22
├── data[2]     = 0x33
├── is_extended = 0
├── is_remote   = 0
└── is_fd       = 0
    
//最后进行索引更新
rx_queue.head = next;等价于：rx_queue.head 从2→ 3；
表示：
第 2 个位置已经写完
下一帧要写入第 3 个位置
整个回调函数做的事情就是：
CAN 总线收到报文
        ↓
报文进入硬件 FIFO0
        ↓
HAL_CAN_GetRxMessage()
        ↓
rx_header 和 rx_data 暂存硬件报文
        ↓
复制到 rx_queue.frames[head]
        ↓
head 移到 next
        ↓
任务稍后从 rx_queue 取出完整 can_frame_t

```

```c
所以这几个对象的分工是：
rx_header
    HAL 接收帧头暂存区

rx_data[8]
    HAL 接收数据暂存区

rx_queue
    软件接收队列

rx_queue.frames[16]
    保存已经转换好的协议层报文

head
    中断写入位置

tail
    任务读取位置
```







# 9：从接收环形队列取出一帧用于后续判断

```c
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
```

函数作用：

```
从软件接收环形队列 rx_queue 中取出一帧，
复制到调用者提供的 frame 变量中。

返回值：1 → 成功取出一帧  0 → 参数无效或队列为空
```



### 1：函数解析

```c
/*第一句*/
uint8_t tail;		//这是本次读取使用的局部变量。它不是队列成员本身，只是临时保存本次要读取的索引。
/*第二句*/
if (frame == NULL || rx_queue.tail == rx_queue.head)
    return 0;
这里检查两个条件。
第一个：frame == NULL ，表示调用者没有提供输出地址。例如：CAN_ReceiveFrame(NULL); //函数无法把报文复制出去，所以返回 0。

第二个：rx_queue.tail == rx_queue.head，表示软件队列为空。因为：head → 下一次写入位置 tail → 下一次读取位置
当两者相等时，表示当前没有待读取报文。 
    
 所以这句的含义是： 输出地址无效，或者队列中没有报文，直接返回失败。
 /*第三句*/   
 tail = rx_queue.tail; //把队列当前的读取位置复制到局部变量 tail。
假设rx_queue.tail = 2 执行后：tail = 2 这表示本次要读取： rx_queue.frames[2]
    
/*第四句*/
*frame = rx_queue.frames[tail]; //这是结构体整体复制语句
解析：
can_frame_t frame;
CAN_ReceiveFrame(&frame);
此时函数参数：frame是一个指针，指向调用者的 frame 变量。
所以：*frame 解引用，因此*frame就是外部定义的frame结构体变量
假设：tail = 2;
这句就相当于：frame = rx_queue.frames[2];
也就是把：
rx_queue.frames[2].id
rx_queue.frames[2].dlc
rx_queue.frames[2].data[8]
rx_queue.frames[2].is_extended
rx_queue.frames[2].is_remote
rx_queue.frames[2].is_fd
一次性复制到调用者的 frame 中。这里不需要逐个成员赋值，因为 can_frame_t 是可以整体复制的结构体。
    
/*第五句*/
rx_queue.tail = (uint8_t)((tail + 1u) % 16u);
本次报文已经取走，所以读取位置向后移动一个位置。
例如：tail = 2 计算： (2 + 1) % 16 = 3
于是：rx_queue.tail = 3;
表示下一次读取：rx_queue.frames[3]
如果当前：tail = 15
那么：
(15 + 1) % 16 = 0
读取位置就绕回数组开头。
  
/*结尾*/
return 1;  表示成功取出一帧。
```





### 2：取出来后可以用于后续的判断

```c
can_frame_t frame;

if (CAN_ReceiveFrame(&frame))
{
    /* frame 中现在已经有一帧完整 CAN 报文。 */
    co_classify_rx(&ctx, &frame, &kind);
}
```







### 3：示例

假设当前队列中有三帧：

```c
frames[0] → 第一帧
frames[1] → 第二帧
frames[2] → 第三帧
```

并且：

```c
head = 3
tail = 0
```



第一次调用：CAN_ReceiveFrame(&frame); 后，

tail 和  rx_queue.tail 起始值均为0，执行一次函数后，tail =0，rx_queue.tai由0→1

执行：

```c
tail = 0
frame = frames[0]
rx_queue.tail = 1
return 1
```





第二次调用CAN_ReceiveFrame(&frame); 后，

tail 和  rx_queue.tail  此时一个为0 一个为1，执行一次函数后，tail =1，rx_queue.tai由1→2

```c
tail = 1
frame = frames[1]
rx_queue.tail = 2
return 1
```



第三次调用CAN_ReceiveFrame(&frame); 后，

tail 和  rx_queue.tail  此时一个为1 一个为2，执行一次函数后，tail =2，rx_queue.tai由2→3

```c
tail = 2
frame = frames[2]
rx_queue.tail = 3
return 1
```

此时：

```c
head = 3
tail = 3
```

队列为空。再次调用时：

```c
rx_queue.tail == rx_queue.head
```

条件成立，函数直接：

```c
return 0;
```

通常任务会这样连续取完当前队列：

```c
can_frame_t frame;

while (CAN_ReceiveFrame(&frame))
{
    /* 处理一帧报文。 */
}
```

所以两个函数的分工是：

```c
HAL_CAN_RxFifo0MsgPendingCallback()
    中断中：
    硬件 FIFO → rx_queue.frames[head]
    head 向前移动

CAN_ReceiveFrame()
    任务中：
    rx_queue.frames[tail] → 调用者的 frame
    tail 向前移动
```

这就是软件接收环形队列的完整读写配合。





### 4：head 和tail 的决定因素

实际运行时，`head` 和 `tail` 都是 `rx_queue` 里的成员变量，会随接收和读取不断变化：

```
static can_rx_queue_t rx_queue;
```

刚启动时它们会初始化为 0：

```
head = 0
tail = 0
```

收到一帧后，接收回调把报文写入 `frames[head]`，再推进 `head`：

```
写 frames[0]，head 变成 1
```

这时 `tail` 仍然是 0，表示任务还没读取这帧。

任务调用 `CAN_ReceiveFrame(&frame)` 后，从 `frames[tail]` 复制报文，再推进 `tail`：

```
读 frames[0]，tail 变成 1
```

所以实际值由两件事共同决定：

- 接收回调每写入一帧，推进 `head`。
- 任务每读取一帧，推进 `tail`。

例如接收回调比任务多写了三帧，就可能出现 `head = 3、tail = 0`；任务读完三帧后，两者又会相等，队列变为空。数组走到索引 15 后会绕回 0。

由接收回调写入和任务读取共同推动变化：

- 每成功接收并写入一帧，回调就推进 `head`。
- 每成功从队列取出一帧，`CAN_ReceiveFrame()` 就推进 `tail`。







# 10：CAN 错误回调

```c
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
    if (hcan != &Can_Handle) /* 只统计当前 CAN1。 */
        return;
    can_error_count++;                                      /* 记录一次 HAL 错误回调。 */
    if ((HAL_CAN_GetError(hcan) & HAL_CAN_ERROR_BOF) != 0u) /* 判断 Bus-off 位。 */
        can_bus_off_count++;                                /* 记录 Bus-off 次数。 */
}
```

这是 HAL 的 CAN 错误回调。当 HAL 检测到 CAN 错误时，会调用它。它用来统计错误，不是由我们主动调用的。





理解这一句：

```c
if ((HAL_CAN_GetError(hcan) & HAL_CAN_ERROR_BOF) != 0u)
```

HAL_CAN_GetError(hcan)

返回的不是固定的 `1`，而是一个**错误标志位集合**。错误回调被调用，只能说明“发生了某种 CAN 错误”，不代表一定发生了 Bus-off。

例如假设：

```c
HAL_CAN_ERROR_BOF = 0x04;// Bus-off错误置位bit4
如果：HAL_CAN_GetError(hcan) = 0x01

此时HAL_CAN_GetError(hcan) & HAL_CAN_ERROR_BOF  就是 0x01 & 0x04 = 0x00 ，因此不是 Bus-off错误。
```



如果：

```c
HAL_CAN_ERROR_BOF = 0x04;// Bus-off错误置位bit4
HAL_CAN_GetError(hcan) = 0x05
此时HAL_CAN_GetError(hcan) & HAL_CAN_ERROR_BOF  就是 0x05 & 0x04 ≠0 ，if 通过，因此是  Bus-off错误
所以can_bus_off_count++;  /* 记录 Bus-off 次数。 */
```

整个函数可以概括为：

```c
HAL 发现 CAN 错误
    ↓
调用 HAL_CAN_ErrorCallback()
    ↓
确认错误来自 CAN1
    ↓
错误回调计数加一
    ↓
检查错误标志是否包含 Bus-off
    ↓
如果是，Bus-off 计数加一
```

两个计数的区别是：

```c
can_error_count
→ CAN 错误回调总共进入了多少次

can_bus_off_count
→ 这些回调中，有多少次包含 Bus-off 错误标志
```

这里要注意：Bus-off 是 CAN 控制器因发送错误累计到一定程度后进入的总线关闭状态。这个回调只是检查并计数，**并没有在这里执行恢复操作**。是否自动恢复由 CAN 初始化配置中的 `AutoBusOff` 等设置决定。
