# 1：Canopen_App_NmtReceive（）函数

```c
co_status_t Canopen_App_NmtReceive(const can_frame_t *frame)
{
    return co_nmt_receive(&nmt, frame);
}
//形参const can_frame_t *frame
表示收到的一帧 CAN 报文。
- frame 是指针，避免复制整帧；
- const 表示这个函数不会修改收到的报文；
- 例如上位机发送 NMT 启动命令：
 CAN-ID：0x000
Data：  01 01
    
 
 
```

这个函数是 `canopen_app.c` 对 NMT 接收功能做的一层**应用封装**



```c#
//内部调用co_nmt_receive(&nmt, frame);
```

### 第一个参数：`&nmt`

`nmt` 是 `canopen_app.c` 中定义的 NMT 模块对象：

```c
static co_nmt_t nmt;
```

它的类型是：

```c
typedef struct
{
    co_context_t *node;
    co_device_od_t *device;
    co_nmt_notify_fn notify;
    void *user;
} co_nmt_t;
```

传入：

```
&nmt
```

就是把这个 NMT 模块对象的地址传给协议层。

`co_nmt_receive()` 通过它可以找到：

```c
nmt.node    → CANopen 节点上下文、Node-ID、当前状态
nmt.device  → 对象字典和相关配置
nmt.notify  → 状态变化时的通知回调
nmt.user    → 回调使用的用户数据
```

其中最核心的是 `nmt.node`，NMT 收到启动、停止、进入预操作等命令后，需要通过它修改节点状态。



### 第二个参数：`frame`

```c
const can_frame_t *frame
```

这是刚从 CAN 接收队列取出的报文帧，例如：

```c
CAN-ID：0x000
Data：  01 01
```

`co_nmt_receive()` 会读取这个帧的：

```c
frame->id
frame->dlc
frame->data[0]
frame->data[1]
```

然后判断：

- 是否为 NMT 报文；
- 命令字是什么；
- 目标 Node-ID 是本节点还是广播；
- 是否需要切换 NMT 状态。

完整关系是：

```c
	&nmt
    ↓
告诉协议层“使用哪个 NMT 模块和节点状态”

	frame
    ↓
告诉协议层“刚收到的 NMT 报文是什么”
```

所以这句可以翻译成：

> 使用应用层保存的 NMT 模块状态，解析刚收到的 CAN 报文，并根据命令更新节点状态。



### 返回值

```c
return co_nmt_receive(&nmt, frame);
```

把协议层返回结果原样交给上层，例如：

```c
CO_OK            NMT 命令处理成功
CO_IGNORED       报文不是发给本节点，或当前状态忽略
CO_ERR_ARGUMENT   参数无效
```



### 在任务中的调用链

实际运行时路径是：

```c
CAN 接收中断
    ↓
CAN 软件接收队列
    ↓
Canopen_Task
    ↓
CAN_ReceiveFrame(&frame)		//它负责从 CAN 软件接收队列取出一帧，取出队头帧，复制到 frame
    ↓
dispatch_frame(&frame)			//它负责根据 CAN-ID 判断这帧属于哪种 CANopen 报文，然后分发给对应的应用接口
    ↓
Canopen_App_NmtReceive(&frame)	//函数封装接口
    ↓
co_nmt_receive(&nmt, frame)		//修改nmt状态的实际操作函数
    ↓
更新 nmt.state
```





# 2：Canopen_App_SdoReceive（）函数

```c
co_status_t Canopen_App_SdoReceive(const can_frame_t *frame)
{
    return co_sdo_receive(&sdo, frame);
}
```

它做的事情是：

```c
CAN_ReceiveFrame(&frame)
    ↓
dispatch_frame() 判断为 SDO 请求
    ↓
Canopen_App_SdoReceive(&frame)
    ↓
co_sdo_receive(&sdo, frame)
```

区别只是 NMT 使用的是：

```
&nmt
```

而 SDO 使用的是：

```
&sdo
```

其中：

```
static co_sdo_t sdo;
```

是 `canopen_app.c` 保存的 SDO 模块上下文，里面关联了节点、对象字典和 SDO 服务所需状态。

`frame` 仍然是刚从 CAN 软件接收队列中取出的报文。



### 

### 第一个参数：`&sdo`

`sdo` 是 `canopen_app.c` 中定义的 SDO 模块对象：

```
static co_sdo_t sdo;
```

它的类型是：

```c
typedef struct
{
    co_context_t *node;
    co_device_od_t *device;
} co_sdo_t;
```

传入：

```
&sdo
```

就是把这个 SDO 模块对象的地址传给协议层。

`co_sdo_receive()` 通过它可以找到：

```c
sdo.node
    → CANopen 节点上下文、Node-ID、当前 NMT 状态、发送接口

sdo.device
    → 对象字典，负责查找、读取和写入对象
```

其中：

- `sdo.node` 用来判断当前节点状态、计算 SDO 响应 CAN-ID，并发送响应；
- `sdo.device` 用来访问对象字典，例如 `0x6200:01`、`0x6401:01` 等。



### 第二个参数：`frame`

```c
const can_frame_t *frame
```

这是从 CAN 软件接收队列中取出的 SDO 请求帧。

例如上位机读取节点 1 的 `0x6200:01`：

```c
CAN-ID：0x601
DLC：   8
Data：  40 00 62 01 00 00 00 00
```

`co_sdo_receive()` 会读取：

```c
frame->id
frame->dlc
frame->data[0]
frame->data[1]
frame->data[2]
frame->data[3]
```

然后判断：

- 是否是发给本节点的 SDO 请求；
- 当前帧是读取还是写入命令；
- 对象索引是什么；
- 子索引是什么；
- 数据长度是否合法；
- 当前 NMT 状态是否允许 SDO 操作。

这里：

```c#
data[0] = 0x40
```

表示 Upload，也就是主站请求读取对象。

```c
data[1] = 0x00
data[2] = 0x62
```

按小端组合：

```c
Index = 0x6200
data[3] = 0x01
```

表示：

```c
Sub-index = 0x01
```

### 返回值

```
return co_sdo_receive(&sdo, frame);
```

把 SDO 协议层返回结果原样交给上层，例如：

```c
CO_OK //表示 SDO 请求已成功处理并发送响应。
```

```c
CO_IGNORED//表示这不是本节点的 SDO 请求，或者当前 NMT 状态不允许处理。
```

```c
CO_ERR_DLC //表示 SDO 报文不是当前项目要求的固定 8 字节。
```

其他对象错误会被协议层转换成对应的 SDO Abort 响应。



### 在任务中的调用链

```c
CAN 接收中断
    ↓
CAN 软件接收队列
    ↓
Canopen_Task
    ↓
CAN_ReceiveFrame(&frame)
    ↓
dispatch_frame(&frame)
    ↓
识别为 SDO 请求
    ↓
Canopen_App_SdoReceive(&frame)
    ↓
co_sdo_receive(&sdo, frame)
    ↓
访问对象字典
    ↓
组装 SDO 响应
    ↓
通过 sdo.node->tx 发送响应
```

例如读取 `0x6200:01`：

```c
上位机：
0x601 : 40 00 62 01 00 00 00 00
    ↓
协议层查找对象字典 0x6200:01
    ↓
节点回复：
0x581 : 4F 00 62 01 00 00 00 00
```

所以这句可以翻译成：

> 使用应用层保存的 SDO 模块上下文，解析刚收到的 SDO 请求，访问对象字典并发送对应响应。

它和前面的 `Canopen_App_NmtReceive()` 结构完全相同，只是：

```
&nmt → 交给 NMT 协议层处理
&sdo → 交给 SDO 协议层处理
```





# 3：Canopen_App_PdoReceive（）函数

```c
co_status_t Canopen_App_PdoReceive(const can_frame_t *frame)
{
    return co_pdo_receive(&pdo, frame);
}
```



### 第一个参数：`&pdo`

`pdo` 是 `canopen_app.c` 中定义的 PDO 模块对象：

```c
static co_pdo_t pdo;
```

它的类型通常包含：

```c
typedef struct
{
    co_context_t *node;
    co_device_od_t *device;
} co_pdo_t;
```

传入：

```
&pdo
```

就是把应用层保存的 PDO 模块对象地址交给协议层。

`co_pdo_receive()` 通过它可以访问：

```c
pdo.node
    → CANopen 节点上下文、Node-ID、当前 NMT 状态

pdo.device
    → 对象字典，负责读取或写入 PDO 映射对象
```

对于本项目，接收 PDO 主要是处理 RPDO1，把主站发来的 DO 控制数据写入：

```
0x6200:01
```



### 第二个参数：`frame`

```c
const can_frame_t *frame
```

这是从 CAN 软件接收队列取出的 PDO 报文。

例如主站发送 RPDO1 控制 RGB 灯：

```
CAN-ID：0x201
DLC：   1
Data：  01
```

其中：

```
0x201 = 0x200 + Node-ID
```

对于节点 1：

```
0x200 + 1 = 0x201
```

`co_pdo_receive()` 会读取：

```
frame->id
frame->dlc
frame->data[0]
```

并判断：

- 是否是本节点的 RPDO1；
- 当前 NMT 状态是否允许处理 PDO；
- DLC 是否正确；
- DO 数据是否包含非法保留位；
- 将数据写入对象字典 `0x6200:01`。

例如：

```
Data = 01
```

写入后：

```
0x6200:01 = 0x01
```

随后 `Canopen_Task()` 会通过：

```c
Canopen_App_GetOutputs(&outputs)
```

读取对象字典中的输出值，再交给：

```c
io_output_queue
    ↓
IOTask
    ↓
apply_board_do(outputs)
    ↓
RGB 灯 GPIO
```



### 返回值

```c
return co_pdo_receive(&pdo, frame);
```

把 PDO 协议层的处理结果原样返回，例如：

```c
CO_OK	//表示 RPDO 已成功处理。
```

```c
CO_IGNORED //表示不是本节点的 PDO，或者当前状态不允许处理。
```

```c
CO_ERR_DLC //表示报文长度不符合 RPDO1 的要求。
```

其他错误则可能表示帧格式、对象写入或状态限制错误。



### 在任务中的调用链

```c
CAN 接收中断
    ↓
CAN 软件接收队列
    ↓
Canopen_Task
    ↓
CAN_ReceiveFrame(&frame)
    ↓
dispatch_frame(&frame)
    ↓
识别为 RPDO
    ↓
Canopen_App_PdoReceive(&frame)
    ↓
co_pdo_receive(&pdo, frame)
    ↓
写入对象字典 0x6200:01
    ↓
Canopen_App_GetOutputs()
    ↓
io_output_queue
    ↓
IOTask
    ↓
apply_board_do()
    ↓
真实 RGB 灯
```

例如：

```
上位机发送：
0x201 : 01
```

完整链路可以理解为：

```
主站发送 DO 值 01
    ↓
PDO 协议层解析
    ↓
0x6200:01 = 01
    ↓
CanopenTask 读取输出值
    ↓
IOTask 控制真实灯
    ↓
红灯亮
```

所以这句可以翻译成：

> 使用应用层保存的 PDO 模块上下文，解析收到的 RPDO 报文，并把主站下发的 DO 数据写入对象字典，后续再由任务层输出到真实 GPIO。





# 4：Canopen_App_ProcessHeartbeat（）函数

```c
co_status_t Canopen_App_ProcessHeartbeat(uint32_t elapsed_ms)
{
    return co_hb_process(&heartbeat, elapsed_ms);
}
```

这个函数是 Heartbeat 的应用层封装：



### 第一个参数：`&heartbeat`

`heartbeat` 是 `canopen_app.c` 中定义的 Heartbeat 模块对象：

```c
static co_hb_t heartbeat;
```

它的类型是：

```c
typedef struct
{
    co_context_t *node;
    co_device_od_t *device;
    uint32_t elapsed_ms;
    uint32_t observed_writes;
} co_hb_t;
```

传入：

```c
&heartbeat
```

就是把 Heartbeat 模块对象的地址传给协议层。

`co_hb_process()` 通过它可以找到：

```c
heartbeat.node
    → CANopen 节点上下文、Node-ID、当前 NMT 状态、发送接口

heartbeat.device
    → 对象字典，读取 0x1017:00 心跳周期

heartbeat.elapsed_ms
    → 已经累计的时间

heartbeat.observed_writes
    → 记录 0x1017:00 是否被主站重新写入过，是否修改过心跳周期！！！！！！！
```

其中最核心的是：

- `node`：决定发送哪个节点的 Heartbeat，以及当前应该发送什么状态；
- `device`：读取对象字典中的 Heartbeat 周期；
- `elapsed_ms`：累计距离上次处理经过了多少毫秒。



### 第二个参数：`elapsed_ms`

```c
uint32_t elapsed_ms
```

表示：**从上一次调用 Heartbeat 处理函数到现在，经过了多少毫秒。**

在 `Canopen_Task()` 中当前是这样调用的：

```c
remember_status(Canopen_App_ProcessHeartbeat(1u));
```

这里传入：

```c
elapsed_ms = 1
```

表示按照任务设计，每次调用都让 Heartbeat 内部时间推进 1 ms。

注意，这个函数不是自己等待 1 ms，也不是自己创建定时器。它只是接收任务层告诉它的“这次经过了多少时间”，然后交给协议层累计。



### `co_hb_process()` 会做什么

它会根据：

```c
heartbeat.node
heartbeat.device
elapsed_ms
```

执行 Heartbeat 逻辑：

1. 检查 Heartbeat 上下文是否有效；
2. 如果节点还处于 Initialization，暂不发送周期 Heartbeat；
3. 从对象字典读取 `0x1017:00` 的周期；
4. 累加 `elapsed_ms`；
5. 判断是否到达发送周期；
6. 到期后发送：

```c
CAN-ID：0x700 + Node-ID
DLC：   1
Data：  当前 NMT 状态
```

例如：

```c
Node-ID = 1
0x1017:00 = 1000 ms
当前状态 = Operational
```

CanopenTask 每轮传入 `1u`：

```
第 1 次：累计 1 ms
第 2 次：累计 2 ms
...
第 1000 次：累计 1000 ms
```

到期后发送：

```c
CAN-ID：0x701
DLC：   1
Data：  05
```

其中 `0x05` 是当前节点的 Operational 状态值。





### 返回值

```
return co_hb_process(&heartbeat, elapsed_ms);
```

把协议层处理结果原样返回：

```
CO_OK
```

表示 Heartbeat 处理成功，可能包括成功发送一帧。

```
CO_IGNORED
```

表示当前没有到发送时间、Heartbeat 周期为 0，或者节点处于不发送周期 Heartbeat 的状态。

其他错误码表示对象字典、节点或底层 CAN 发送出现问题。



### 在任务中的调用链

```c
Canopen_Task 每轮运行
    ↓
Canopen_App_ProcessHeartbeat(1u)
    ↓
co_hb_process(&heartbeat, 1u)
    ↓
读取 0x1017:00
    ↓
累计 1 ms
    ↓
判断是否到期
    ↓
未到期：CO_IGNORED
    ↓
到期：发送 0x700 + Node-ID 的 Heartbeat
```

所以这句可以翻译成：

> 使用应用层保存的 Heartbeat 上下文，把本轮经过的时间交给 Heartbeat 协议模块，由协议模块判断是否应该发送下一帧心跳。



### 心跳的本质：

Heartbeat 报文的核心内容就是：

```c
CAN-ID：0x700 + Node-ID
DLC：   1
Data：  当前节点的 NMT 状态
```

节点 ID 为 1 时：

```
Heartbeat CAN-ID = 0x701
```

例如：

```
Data = 0x05
```

表示节点当前处于 Operational。

它能持续正常发送，至少说明这条链路在工作：

```c
FreeRTOS 调度器
    ↓
CanopenTask 被周期调度
    ↓
Canopen_App_ProcessHeartbeat()
    ↓
co_hb_process()
    ↓
Heartbeat 到期判断
    ↓
CAN_SendFrame()
    ↓
CAN 硬件成功发送
```

所以心跳可以作为主站判断节点“还活着、通信还在运行”的依据。

不过要注意：**Heartbeat 正常不等于整个系统所有任务都正常。**

例如：

```
CanopenTask 正常运行
IOTask 卡死
```

此时 CanopenTask 仍可能继续发送 Heartbeat，但真实 AI、DI 采样已经停止。当前项目用：

```c
MonitorTask
    ↓
检查 IOTask 采样计数
    ↓
发现异常
    ↓
EMCY / 安全输出 / 看门狗
```

来补充检测任务和 I/O 健康状态。

因此可以这样记：

> Heartbeat 主要证明 CANopen 节点和 CanopenTask 的通信主循环仍在运行，并报告当前 NMT 状态；它不是整个系统健康状态的完整证明。





# 5：Canopen_App_ProcessPdo（）函数

```c
co_status_t Canopen_App_ProcessPdo(uint32_t elapsed_ms)
{
    return co_pdo_process(&pdo, elapsed_ms);
}
```

这个函数是 PDO 的**周期处理入口**：



### 第一个参数：`&pdo`

`pdo` 是 `canopen_app.c` 中定义的 PDO 模块对象：

```c
static co_pdo_t pdo;
```

它的类型是：

```c
typedef struct
{
    co_context_t *node;
    co_device_od_t *device;
    uint32_t elapsed_tpdo1;
    uint32_t elapsed_tpdo2;
    uint32_t observed_tpdo1_writes;
    uint32_t observed_tpdo2_writes;
    uint8_t last_di;
    uint16_t last_ai1;
    uint16_t last_ai2;
    uint8_t have_sample;
    uint8_t previous_operational;
    uint8_t pending_tpdo1;
    uint8_t pending_tpdo2;
} co_pdo_t;
```

传入：

```
&pdo
```

就是把 PDO 模块对象地址交给协议层。

`co_pdo_process()` 通过它可以找到：

```c
pdo.node
    → 节点上下文、Node-ID、当前 NMT 状态、CAN 发送接口

pdo.device
    → 对象字典中的 DI、AI、TPDO 配置

pdo.elapsed_tpdo1
    → TPDO1 已累计的时间

pdo.elapsed_tpdo2
    → TPDO2 已累计的时间

pdo.last_di
    → 上一次保存的 DI 值

pdo.last_ai1 / last_ai2
    → 上一次保存的 AI 值

pdo.pending_tpdo1
    → TPDO1 是否等待发送

pdo.pending_tpdo2
    → TPDO2 是否等待发送
```



### 第二个参数：`elapsed_ms`

```c
uint32_t elapsed_ms
```

表示从上一次调用 PDO 处理函数到现在经过了多少毫秒。

当前 `Canopen_Task()` 中是：

```c
remember_status(Canopen_App_ProcessPdo(1u));
```

所以每轮给 PDO 模块推进：

```c
elapsed_ms = 1
```

注意，这个函数本身不等待 1 ms，只是把时间交给 `co_pdo_process()`，由它累计 TPDO1 和 TPDO2 的事件周期。



### `co_pdo_process()` 主要处理什么

它主要处理节点发出的两个 TPDO：

```c
TPDO1：发送 DI
TPDO2：发送 AI1、AI2
```

处理流程大致是：

```c
检查 PDO 上下文
    ↓
判断节点是否处于 Operational
    ↓
读取对象字典中的 DI、AI1、AI2
    ↓
读取 TPDO1、TPDO2 事件周期
    ↓
判断 DI 是否变化
    ↓
判断 AI 是否变化
    ↓
累计两个 TPDO 的时间
    ↓
判断事件周期是否到期
    ↓
发送需要发送的 TPDO
```





### DI 变化时

对象字典中的：

```c
0x6000:01
```

保存当前 DI。

如果发现：

```c
di != pdo->last_di
```

就设置：

```
pdo->pending_tpdo1 = 1u;
```

之后发送 TPDO1：

```c
CAN-ID：0x181
Data：  DI值
```

例如按下 KEY1：

```
DI = 0x01
```

则可能发送：

```
0x181 : 01
```



### AI 变化时

对象字典中的：

```c
0x6401:01 → AI1
0x6401:02 → AI2
```

如果：

```
0x6423:00 = 1
```

并且 AI1 或 AI2 发生变化：

```
ai1 != pdo->last_ai1 || ai2 != pdo->last_ai2
```

就设置：

```c
pdo->pending_tpdo2 = 1u;
```

之后发送 TPDO2：

```c
CAN-ID：0x281
Data：  AI1低字节 AI1高字节 AI2低字节 AI2高字节
```

如果：

```c
0x6423:00 = 0
```

即使 AI 变化，也不会通过“输入变化”触发 TPDO2；但如果配置了 TPDO2 事件周期，仍可能按照周期发送。







### 事件周期

TPDO1 和 TPDO2 各自有独立计时器：

```c
0x1800:05 → TPDO1 事件周期
0x1801:05 → TPDO2 事件周期
```

每次调用：

```c
co_pdo_process(&pdo, 1u);
```

都会执行：

```
elapsed_tpdo1 += 1
elapsed_tpdo2 += 1
```

如果 TPDO1 周期为 500 ms：

```c
累计到 500 ms
    ↓
pending_tpdo1 = 1
    ↓
发送 TPDO1
```

TPDO2 同理，两个计时互不影响。

### 刚进入 Operational

当节点刚从 Pre-operational 进入 Operational 时：

```
pdo->previous_operational == 0u
```

函数会安排：

```
pending_tpdo1 = 1
pending_tpdo2 = 1
```

因此 TPDO1 和 TPDO2 会各发送一次当前值，不需要等待 DI 或 AI 变化。



### 返回值

```c
return co_pdo_process(&pdo, elapsed_ms);
```

把协议层结果原样返回：

```c
CO_OK  //表示至少成功发送了一帧 TPDO。
```

```c
CO_IGNORED //表示当前没有需要发送的 PDO，或者节点不在 Operational。
```

其他错误码表示对象读取或 CAN 发送失败。



### 在任务中的调用链

```c
Canopen_Task 每约 1 ms 运行
    ↓
Canopen_App_ProcessPdo(1u)
    ↓
co_pdo_process(&pdo, 1u)
    ↓
读取 DI、AI 和 PDO 配置
    ↓
判断输入变化或事件周期
    ↓
发送 TPDO1 / TPDO2
```

例如按下 KEY1：

```c
IOTask 读取 DI=0x01
    ↓
更新对象字典 0x6000:01
    ↓
CanopenTask 调用 Canopen_App_ProcessPdo(1u)
    ↓
发现 DI 与 last_di 不同
    ↓
设置 pending_tpdo1=1
    ↓
发送 TPDO1：0x181，数据 01
```

所以这句可以翻译成：

> 使用应用层保存的 PDO 上下文，把经过的时间交给 PDO 协议模块，由它检查 DI/AI 变化和事件周期，并决定是否发送 TPDO1 或 TPDO2。

这里还要和前一个函数区分：

```c
Canopen_App_PdoReceive()
    → 接收 RPDO，主要处理主站下发的 DO

Canopen_App_ProcessPdo()
    → 处理 TPDO，主要上报节点的 DI 和 AI
```







# 6：Canopen_App_UpdateInputs（）函数

```c
co_status_t Canopen_App_UpdateInputs(uint8_t di,
                                     uint16_t ai1,
                                     uint16_t ai2)
{
    return co_device_od_update_inputs(&device, di, ai1, ai2);
}
```

这个函数负责把 I/O 任务采集到的真实输入值，更新到对象字典



### 第一个参数：`di`

```c
uint8_t di
```

表示当前读取到的数字输入值。

当前开发板有两个按键：

```c
KEY1 → DI bit0
KEY2 → DI bit1
```

例如：

```c
di = 0x00 → 两个按键都未按下
di = 0x01 → KEY1 按下
di = 0x02 → KEY2 按下
di = 0x03 → KEY1、KEY2 都按下
```

这个值最终更新到对象字典：

```c
0x6000:01
```





### 第二个参数：`ai1`

```c
uint16_t ai1
```

表示 ADC 采集并平均处理后的 AI1 数值。

当前项目中：

```c
AI1 → PC3 电位器
```

它会更新到：

```
0x6401:01
```





### 第三个参数：`ai2`

```c
uint16_t ai2
```

表示 ADC 采集并平均处理后的 AI2 数值。

当前项目中：

```
AI2 → PA4 温度传感器
```

它会更新到：

```
0x6401:02
```



### 函数内部的 `&device`

```c
co_device_od_update_inputs(&device, di, ai1, ai2);
```

`device` 是 `canopen_app.c` 中定义的对象字典设备对象：

```
static co_device_od_t device;
```

传入：

```
&device
```

就是告诉对象字典模块：

> 请把这三个新采样值写入当前节点自己的对象字典。



### 这个函数本身不发送 TPDO

这是一个容易混淆的地方：

```c
Canopen_App_UpdateInputs()
```

只负责：

```c
DI、AI数据
    ↓
写入对象字典
```

它不会直接发送 TPDO。

后续由：

```c
Canopen_App_ProcessPdo(1u)
```

读取对象字典，比较新旧值：

```c
当前 DI != 上一次 DI
    ↓
触发 TPDO1

当前 AI != 上一次 AI
且 0x6423:00 = 1
    ↓
触发 TPDO2
```



### 在任务中的调用链

```c
真实按键、ADC
    ↓
IOTask 读取和处理
    ↓
生成 io_sample_t
    ↓
写入 io_sample_queue
    ↓
CanopenTask 取出 sample
    ↓
Canopen_App_UpdateInputs(sample.di,
                          sample.ai1,
                          sample.ai2)
    ↓
更新对象字典
    ├── 0x6000:01 ← DI
    ├── 0x6401:01 ← AI1
    └── 0x6401:02 ← AI2
    ↓
Canopen_App_ProcessPdo(1u)
    ↓
根据变化决定是否发送 TPDO
```

例如按下 KEY1：

```c
IOTask 读取到：
di  = 0x01
ai1 = 当前电位器值
ai2 = 当前温度值
```

CanopenTask 取出后调用：

```
Canopen_App_UpdateInputs(0x01, ai1, ai2);
```

对象字典变为：

```
0x6000:01 = 0x01
```

下一次执行：

```
Canopen_App_ProcessPdo(1u);
```

发现 DI 变化后，才发送：

```
TPDO1：CAN-ID 0x181，Data = 01
```

所以这句可以翻译成：

> 把 IOTask 采集到的 DI、AI1、AI2 写入 CANopen 对象字典，为后续 SDO 读取和 PDO 上报提供最新输入数据。





链路代码：

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
```

实际链路是：

```c
xQueueReceive(io_sample_queue, &sample, 0u)	//就在第2步
```

取出 IOTask 产生的最新采样：

```c
sample.di
sample.ai1
sample.ai2
```

然后：

```
Canopen_App_UpdateInputs(sample.di, sample.ai1, sample.ai2)
```

立即更新对象字典：

```c
0x6000:01 ← DI
0x6401:01 ← AI1
0x6401:02 ← AI2
```

接着 CanopenTask 同一轮继续执行：

```c
//就在第3步
Canopen_App_ProcessHeartbeat(1u);
Canopen_App_ProcessPdo(1u);
```

其中 `Canopen_App_ProcessPdo(1u)` 会马上读取刚更新的对象字典，并比较：

```c
当前 DI vs 上一次 DI
当前 AI vs 上一次 AI
```

如果满足触发条件，就直接组装并发送 TPDO：

```c
DI 变化 → TPDO1
AI 变化且 0x6423:00=1 → TPDO2
```

所以链路是：

```c
IOTask 采样完成
    ↓
io_sample_queue
    ↓
CanopenTask 取出
    ↓
更新对象字典
    ↓
同一轮调用 ProcessPdo()
    ↓
比较变化
    ↓
发送 TPDO
```

中间没有再经过一个额外的任务周期，也没有等待下一次 1 ms 调度。最多只会受到这些因素影响：

- IOTask 什么时候把采样写入队列；
- CanopenTask 什么时候取到队列数据；
- 当前 CAN 发送邮箱是否空闲；
- CAN 总线本身的发送时间。

所以“更新输入到发送 TPDO 只隔两句代码”这个理解是对的；真正的判断和组帧都封装在这两个应用层函数调用进去的纯 C 协议函数里。

# 7：Canopen_App_GetOutputs（）函数

```c
co_status_t Canopen_App_GetOutputs(uint8_t *outputs)
{
    co_status_t status;

    if (outputs == NULL)
        return CO_ERR_ARGUMENT;
    if (safe_output_active != 0u)
    {
        status = co_device_od_force_safe_outputs(&device);
        if (status != CO_OK)
            return status;
        safe_output_applied = 1u;
        *outputs = 0u;
        return CO_OK;
    }
    if (node.state != CO_NMT_OPERATIONAL)
    {
        if (safe_output_applied == 0u)
        {
            status = co_device_od_force_safe_outputs(&device);
            if (status != CO_OK)
                return status;
            safe_output_applied = 1u;
        }
        *outputs = 0u;
        return CO_OK;
    }
    safe_output_applied = 0u;
    return co_device_od_get_outputs(&device, outputs);
}
```

这个函数负责从应用层取得当前应该输出到真实 DO 的值，同时在故障或非 Operational 状态下强制输出安全值 `0`



### 参数：`outputs`

```c
uint8_t *outputs
```

这是一个输出参数，函数会把最终的 DO 值写到它指向的变量中。

例如：

```c
uint8_t outputs;

Canopen_App_GetOutputs(&outputs);
```

执行后：

```
outputs = 0x01
```

表示输出 bit0，有可能对应红灯。

它是指针，所以函数内部使用：

```c
*outputs = 0u;
```

就是把最终值写回调用者的变量。



### 局部变量：`status`

```c
co_status_t status;
```

用于保存对象字典操作结果，例如：

```c
co_device_od_force_safe_outputs()
co_device_od_get_outputs()
```

是否执行成功。

------



### 第一道判断：`outputs == NULL`

```c
if (outputs == NULL)
{
    return CO_ERR_ARGUMENT;
}
```

如果调用者没有传入有效的输出变量地址，函数就没有地方保存 DO 值，因此直接返回参数错误。

例如错误调用：

```
Canopen_App_GetOutputs(NULL);
```

会返回：

```
CO_ERR_ARGUMENT
```





### 第二道判断：是否处于安全输出状态

```c
if (safe_output_active != 0u)
```

`safe_output_active` 是应用层的故障安全标志：

```c
static uint8_t safe_output_active;
```

当 `process_reliability()` 检测到严重故障时，会调用：

```c
Canopen_App_SetSafeOutput(1u);
```

此时：

```c
safe_output_active = 1
```

进入这个分支。



### 强制对象字典输出为安全值

```c
status = co_device_od_force_safe_outputs(&device);
if (status != CO_OK)
{
    return status;
}
```

这一步不仅是让本次输出返回 `0`，还会把对象字典中的 DO 对象强制写为安全值，避免故障期间主站之前写入的旧 DO 值继续留在对象字典中。

然后：

```c
safe_output_applied = 1u;
*outputs = 0u;
return CO_OK;
```

结果是：

```
对象字典 DO = 0
outputs      = 0
```

最终 IOTask 会关闭真实输出。

状态可以理解为：

```c
safe_output_active = 1
safe_output_applied = 1
outputs = 0
```

------



### 第三道判断：节点是否处于 Operational

```
if (node.state != CO_NMT_OPERATIONAL)
```

即使没有发生 EMCY 故障，只要节点不在 Operational，也不能直接使用对象字典中的 DO 控制值。

例如节点处于：

```
Initialization
Pre-operational
Stopped
```

都会进入这个分支。



### 只在第一次进入非 Operational 时强制安全输出

```c
if (safe_output_applied == 0u)
{
    status = co_device_od_force_safe_outputs(&device);
    if (status != CO_OK)
    {
        return status;
    }

    safe_output_applied = 1u;
}
```

如果 `safe_output_applied` 还是 `0`，说明还没有执行过安全输出处理，于是：

```c
对象字典 DO 被清零
safe_output_applied = 1
```

之后任务每 1 ms 调用这个函数时，就不需要反复写对象字典。

然后无论是否刚刚执行过强制清零：

```c
*outputs = 0u;
return CO_OK;
```

最终都返回：

```
outputs = 0
```

这样在节点没有进入 Operational 时，真实 DO 保持关闭。

------



### 正常 Operational 路径

如果前面两个条件都不成立，就说明：

```c
safe_output_active = 0
node.state = CO_NMT_OPERATIONAL
```

执行：

```c
safe_output_applied = 0u;
return co_device_od_get_outputs(&device, outputs);
```

先把：

```c
safe_output_applied = 0u;
```

清零，表示已经恢复到正常输出模式。

然后从对象字典读取主站写入的 DO 值：

```c
co_device_od_get_outputs(&device, outputs);
```

例如上位机发送：

```
0x201 : 01
```

对象字典：

```
0x6200:01 = 0x01
```

调用这个函数后：

```
outputs = 0x01
```

随后 CanopenTask 把它写入：

```c
io_output_queue
    ↓
IOTask
    ↓
apply_board_do(0x01)
    ↓
红灯亮
```

------



### 三条路径汇总

##### 1. 故障安全输出

```c
safe_output_active = 1
    ↓
强制对象字典 DO 清零
    ↓
outputs = 0
    ↓
真实 DO 关闭
```



##### 2. 非 Operational

```c
node.state != CO_NMT_OPERATIONAL
    ↓
强制对象字典 DO 清零
    ↓
outputs = 0
    ↓
真实 DO 关闭
```



##### 3. 正常 Operational

```c
safe_output_active = 0
node.state = CO_NMT_OPERATIONAL
    ↓
读取对象字典 DO
    ↓
outputs = 主站下发的 DO 值
    ↓
IOTask 控制真实 GPIO
```



### 在任务中的调用链

```c
CanopenTask
    ↓
Canopen_App_GetOutputs(&outputs)
    ↓
判断故障状态和 NMT 状态
    ↓
得到安全值 0 或正常 DO 值
    ↓
xQueueOverwrite(io_output_queue, &outputs)
    ↓
IOTask
    ↓
apply_board_do(outputs)
    ↓
真实 LED/DO
```

所以这个函数的核心作用是：

> 正常时读取对象字典中的 DO；发生故障或节点不在 Operational 时，强制返回 `0`，让真实输出进入安全状态。







# 8：Canopen_App_IsOperational（）函数

```c
uint8_t Canopen_App_IsOperational(void)
{
    return (uint8_t)(node.state == CO_NMT_OPERATIONAL);
}
```

这个函数用于判断当前 CANopen 节点是否处于 Operational 状态：



### `node`

`node` 是 `canopen_app.c` 中定义的节点上下文：

```c
static co_context_t node;
```

它保存 CANopen 节点的核心状态，包括：

```c
node.node_id
    → 节点 ID

node.state
    → 当前 NMT 状态

node.tx
    → CAN 发送回调
```

这里函数只关心：

```
node.state
```



### 判断表达式

```c
node.state == CO_NMT_OPERATIONAL
```

这是一个比较表达式，结果只有两种：

```c
相等 → 1
不相等 → 0
```

也就是：

```c
当前状态是 Operational → true
当前状态不是 Operational → false
```

外层再强制转换成：

```
uint8_t
```

确保函数返回一个明确的 `0` 或 `1`。



### 返回值

```
返回 1
```

表示节点当前处于：

```
CO_NMT_OPERATIONAL
```

此时通常允许：

- 处理 TPDO；
- 发送周期和事件触发 TPDO；
- 使用对象字典中的正常 DO 值；
- 根据主站 RPDO 控制真实输出。

```
返回 0
```

表示节点处于其他状态，例如：

```
CO_NMT_INITIALIZATION
CO_NMT_PRE_OPERATIONAL
CO_NMT_STOPPED
```

此时通常不应该进行正常运行输出。



### 在阶段 9 中的调用

它主要被 `freertos_demo.c` 的 `process_reliability()` 使用：

```c
if (healthy != 0u && Canopen_App_IsOperational() != 0u)
{
    reliability_recovery_ms++;
}
else
{
    reliability_recovery_ms = 0u;
}
```

它的含义是：

```c
I/O 正常
    且
节点处于 Operational
    ↓
累计稳定恢复时间
```

只有满足：

```
稳定约 1000 ms
```

外层才会调用：

```c
Canopen_App_ClearEmcy();
```



### 这个函数不做状态切换

它只是读取并判断：

```c
node.state
```

不会把节点切换到 Operational，也不会发送 NMT 报文。

状态切换由：

```
co_nmt_receive()
```

完成，例如主站发送：

```
ID：0x000
Data：01 01
```

协议层处理后：

```
node.state = CO_NMT_OPERATIONAL;
```

之后 `Canopen_App_IsOperational()` 才会返回：

```
1
```

所以它可以理解成：

> 查询当前节点是否已经进入 Operational，返回 1 表示是，返回 0 表示不是。



# 9：Canopen_App_SetSafeOutput（）函数

```c
void Canopen_App_SetSafeOutput(uint8_t active)
{
    safe_output_active = (active != 0u) ? 1u : 0u;
}
```

这个函数用于设置“是否启用安全输出模式”的标志





### 参数：`active`

```c
uint8_t active
```

表示调用者想不想启用安全输出：

```c
active = 0 → 关闭安全输出模式
active ≠ 0 → 开启安全输出模式
```

这里没有强制调用者只能传 `0` 或 `1`，例如：

```c
Canopen_App_SetSafeOutput(1u);
Canopen_App_SetSafeOutput(0u);
Canopen_App_SetSafeOutput(5u);
```

传入 `5u` 也会被当作“开启”。



### 三目运算符

```c
(active != 0u) ? 1u : 0u
```

意思是：

```c
active 不等于 0 → 保存 1
active 等于 0    → 保存 0
```

所以无论调用者传入：

```
1、2、5、255
```

最后都会统一保存：

```
safe_output_active = 1
```



### 修改的变量

```
safe_output_active
```

是 `canopen_app.c` 中的静态变量：

```c
static uint8_t safe_output_active;
```

它表示当前是否处于安全输出模式。





### 它不会直接控制 GPIO

这个函数只设置标志，不会直接：

```c
关闭 RGB 灯
修改 GPIO
清零对象字典
发送 CAN
```

真正读取这个标志的是：

```
Canopen_App_GetOutputs()
```

调用链是：

```c
检测到严重故障
    ↓
Canopen_App_SetSafeOutput(1u)
    ↓
safe_output_active = 1
    ↓
Canopen_App_GetOutputs()
    ↓
强制输出 0
    ↓
io_output_queue
    ↓
IOTask
    ↓
apply_board_do(0)
    ↓
真实 DO 关闭
```



### 开启安全输出

```c
Canopen_App_SetSafeOutput(1u);
```

结果：

```
safe_output_active = 1
```

下一次 `Canopen_App_GetOutputs()` 会进入：

```
if (safe_output_active != 0u)
```

然后让输出值变成：

```
outputs = 0
```



### 关闭安全输出

```
Canopen_App_SetSafeOutput(0u);
```

结果：

```
safe_output_active = 0
```

但它不会立刻把灯重新点亮。后续还要满足：

```
节点处于 Operational
故障已经清除
```

下一次 `Canopen_App_GetOutputs()` 才会重新从对象字典读取正常 DO 值。

所以这个函数可以理解成：

> 设置安全输出模式开关；它只改变状态标志，真正把 DO 置零由 `Canopen_App_GetOutputs()` 完成。



### 总览：

完整流程是：

```c
故障发生
    ↓
SetSafeOutput(1)
    ↓
安全模式开启
    ↓
DO 持续为 0

系统恢复稳定
    ↓
ClearEmcy()
    ↓
SetSafeOutput(0)
    ↓
节点回到 Operational
    ↓
主站重新发送 RPDO DO 值
    ↓
DO 恢复正常输出
```

所以准确说是：**开启安全模式时 DO 被强制关闭；系统恢复后关闭安全模式，再重新下发 DO 控制值，输出才恢复。**





# 10：Canopen_App_ReportEmcy（）函数

```c
co_status_t Canopen_App_ReportEmcy(uint16_t error_code, uint8_t error_register,
                                   const uint8_t manufacturer[CO_EMCY_MANUFACTURER_LENGTH])
{
    return co_emcy_report(&emcy, error_code, error_register, manufacturer);
}
```

这个函数是应用层对 EMCY 故障报告功能的封装



### 第一个参数：`error_code`

```c
uint16_t error_code
```

表示具体发生了什么故障。

例如：

```c
CO_EMCY_ERROR_CAN_RX_OVERFLOW  // 0xFF01，CAN 接收队列溢出
CO_EMCY_ERROR_BUS_OFF          // 0xFF02，CAN Bus-off
CO_EMCY_ERROR_ADC              // 0xFF04，ADC/I/O 采样故障
```

这个错误码最终放在 EMCY 数据区的：

```c
Byte 0～1
```

并且按低字节在前发送。

例如：

```
0xFF02 → 02 FF
```



### 第二个参数：`error_register`

```c
uint8_t error_register
```

表示这类故障对应的 Error Register 位。

例如：

```
CO_EMCY_REGISTER_COMMUNICATION  // 0x10，通信错误
CO_EMCY_REGISTER_VOLTAGE        // 0x04，电压相关错误
CO_EMCY_REGISTER_TEMPERATURE    // 0x08，温度相关错误
```

传入 `co_emcy_report()` 后，纯 C EMCY 模块会自动加上通用错误位：

```
0x10 | 0x01 = 0x11
```

最终更新对象字典：

```
0x1001:00 = 0x11
```



### 第三个参数：`manufacturer`

```
const uint8_t manufacturer[CO_EMCY_MANUFACTURER_LENGTH]
```

表示 5 字节厂商自定义信息。

如果没有自定义信息，就传：

```
NULL
```

例如：

```
Canopen_App_ReportEmcy(
    CO_EMCY_ERROR_BUS_OFF,
    CO_EMCY_REGISTER_COMMUNICATION,
    NULL);
```

此时 EMCY 报文后 5 个字节为：

```
00 00 00 00 00
```



### 函数内部的 `&emcy`

`emcy` 是 `canopen_app.c` 中保存的 EMCY 模块对象：

```c
static co_emcy_t emcy;
```

传入：

```
&emcy
```

就是把应用层维护的 EMCY 状态交给纯 C 协议层。

这个对象里面保存：

```c
emcy.node
    → 节点上下文和 CAN 发送接口

emcy.device
    → 对象字典

emcy.active
    → 当前是否已有活动故障

emcy.active_error_*
    → 已成功上报的故障内容

emcy.pending_frame
    → 发送失败、等待重试的 EMCY 帧
```





### 返回值

```c
return co_emcy_report(&emcy,
                      error_code,
                      error_register,
                      manufacturer);
```

把纯 C EMCY 模块的结果原样返回给调用者：

```
CO_OK
```

表示 EMCY 报告帧发送成功。

```
CO_ERR_TX_BUSY
```

表示发送通道忙，或者已有其他待重试报文。

```
CO_IGNORED
```

表示相同故障已经报告过，或者相同报文已经在等待重试。

其他错误码表示参数、对象字典或报文检查失败。



### 在故障处理中的调用链

以 CAN Bus-off 为例：

```c
HAL_CAN_ErrorCallback()
    ↓
can_bus_off_count 增加
    ↓
Canopen_Task 调用 process_reliability()
    ↓
发现 Bus-off 计数发生变化
    ↓
Canopen_App_ReportEmcy(
    CO_EMCY_ERROR_BUS_OFF,
    CO_EMCY_REGISTER_COMMUNICATION,
    NULL)
    ↓
co_emcy_report(&emcy, ...)
    ↓
组装 CAN-ID 0x081 的 EMCY 帧
    ↓
更新 0x1001:00
    ↓
发送成功或发送失败。保存至 pending_frame
```

然后 `process_reliability()` 还会继续执行：

```c
Canopen_App_SetSafeOutput(1u);
```

让 DO 进入安全状态。

所以要分清：

```c
Canopen_App_ReportEmcy()
    → 报告故障

Canopen_App_SetSafeOutput()
    → 设置安全输出标志

process_reliability()
    → 检测故障并协调这两个动作
```

这个函数自己不会检测故障，也不会直接关闭 LED；它只是把上层已经确认的故障交给 EMCY 协议模块处理。





# 11：Canopen_App_ClearEmcy（）函数

```c
co_status_t Canopen_App_ClearEmcy(void)
{
    return co_emcy_clear(&emcy);
}
```

这个函数是应用层对 EMCY 清除功能的封装：



### `&emcy`

`emcy` 是 `canopen_app.c` 中定义的 EMCY 模块对象：

```
static co_emcy_t emcy;
```

传入：

```
&emcy
```

就是把当前 EMCY 状态交给纯 C EMCY 模块。

其中可能保存着：

```c
emcy.active
    → 当前是否有已经成功上报、仍未清除的故障

emcy.active_error_code
    → 当前活动故障的错误码

emcy.active_error_register
    → 当前活动故障的 Error Register

emcy.pending
    → 是否有 EMCY 报文等待重试

emcy.pending_frame
    → 等待重试的完整 EMCY 报文

emcy.pending_is_reset
    → 待重试的是故障报告帧还是清除帧
```



### 调用的协议函数

```
co_emcy_clear(&emcy);
```

真正执行清除逻辑的是纯 C 协议函数 `co_emcy_clear()`，它会：

1. 检查 EMCY 模块是否有效；
2. 检查是否已有待重试报文；
3. 检查当前是否确实存在活动故障；
4. 组装全零 EMCY 清除帧；
5. 清零对象字典 `0x1001:00`；
6. 调用 `submit(..., 1u)` 发送清除帧。

清除帧格式为：

```
CAN-ID：0x080 + Node-ID
DLC：   8
Data：  00 00 00 00 00 00 00 00
```

节点 ID 为 1 时：

```
CAN-ID：0x081
```



### 返回值

这个函数直接把 `co_emcy_clear()` 的结果返回：

```
CO_OK
```

表示清除帧发送成功，活动故障记录被清除。

```
CO_IGNORED
```

可能表示：

- 当前没有活动故障；
- 清除帧已经在等待重试；
- 当前没有必要重复清除。

```
CO_ERR_TX_BUSY
```

表示故障报告帧还在等待重试，当前不能跳过报告直接清除。

其他错误码表示参数、对象字典或底层发送失败。



### 在系统恢复中的调用链

`Canopen_Task` 每轮调用：

```
process_reliability();
```

当它判断：

```
I/O 已恢复
CAN 通信正常
节点处于 Operational
连续稳定约 1 秒
```

才会调用：

```
Canopen_App_ClearEmcy();
```

完整链路是：

```
故障发生
    ↓
报告 EMCY
    ↓
DO 进入安全状态
    ↓
系统恢复稳定
    ↓
Canopen_App_ClearEmcy()
    ↓
co_emcy_clear(&emcy)
    ↓
发送全零 EMCY 清除帧
    ↓
active=0
    ↓
清除活动故障记录
```

注意，`Canopen_App_ClearEmcy()` 自己不判断系统是否稳定。稳定性判断由外层的 `process_reliability()` 完成；这个函数只负责把清除请求转交给纯 C EMCY 模块执行。





# 12：Canopen_App_ProcessEmcy（）函数



```c
co_status_t Canopen_App_ProcessEmcy(void)
{
    return co_emcy_process(&emcy);
}
```

这个函数是应用层对 EMCY **待重试处理**的封装：



### 参数：`&emcy`

`emcy` 是 `canopen_app.c` 中保存的 EMCY 模块对象：

```
static co_emcy_t emcy;
```

传入：

```
&emcy
```

就是把当前 EMCY 状态交给纯 C 协议层，让它查看有没有发送失败、等待重试的报文。

它会读取：

```
emcy.pending
    → 是否存在待重试帧

emcy.pending_frame
    → 保存的完整 EMCY 报文

emcy.pending_is_reset
    → 0：故障报告帧
      1：清除帧
```

### 调用的协议函数

```
co_emcy_process(&emcy);
```

真正执行重试逻辑的是纯 C 函数 `co_emcy_process()`：

```
co_status_t co_emcy_process(co_emcy_t *emcy)
{
    co_status_t status = validate(emcy);

    if (status != CO_OK)
        return status;

    if (emcy->pending == 0u)
        return CO_IGNORED;

    return submit(emcy,
                  &emcy->pending_frame,
                  emcy->pending_is_reset);
}
```

它的流程是：

```
检查 EMCY 模块
    ↓
pending == 0？
    ├── 是：没有待重试帧，返回 CO_IGNORED
    └── 否：把 pending_frame 交给 submit() 重试
```



### 故障报告帧重试

如果：

```
pending = 1
pending_is_reset = 0
```

说明等待重试的是故障报告帧：

```
pending_frame = 故障报告帧
```

重试成功后：

```
active = 1
pending = 0
active_error_* 保存故障内容
```

重试仍然失败：

```
pending = 1
pending_frame 保持不变
pending_is_reset = 0
```



### 清除帧重试

如果：

```
pending = 1
pending_is_reset = 1
```

说明等待重试的是清除帧：

```
pending_frame = 全零清除帧
```

重试成功后：

```
active = 0
pending = 0
active_error_* 清零
```

重试仍然失败：

```
pending = 1
pending_is_reset = 1
```

继续等待下一次重试。





### 在 `Canopen_Task` 中的调用位置

它不是每轮单独直接调用，而是由：

```
process_reliability()
```

调用：

```
remember_status(Canopen_App_ProcessEmcy());
```

大致流程是：

```
Canopen_Task 每约 1 ms 运行
    ↓
process_reliability()
    ↓
检测 CAN、I/O 或看门狗故障
    ↓
报告 EMCY 或发现已有 pending
    ↓
Canopen_App_ProcessEmcy()
    ↓
重试 pending_frame
```

如果当前没有待重试帧：

```
co_emcy_process()
    ↓
CO_IGNORED
```

这个结果不会被当成严重错误处理。

所以要区分三个函数：

```
Canopen_App_ReportEmcy()
    → 报告一个新的故障

Canopen_App_ClearEmcy()
    → 报告当前活动故障已经清除

Canopen_App_ProcessEmcy()
    → 重试之前发送失败的 EMCY 报文
```

这个函数本身不检测故障，也不判断系统是否恢复，只负责：

> 检查有没有待重试 EMCY，如果有，就把保存的完整报文再次交给发送流程。