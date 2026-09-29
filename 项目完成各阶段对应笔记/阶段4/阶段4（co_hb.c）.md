## 解析：



## 1：static co_status_t validate(const co_hb_t *hb)

```c
static co_status_t validate(const co_hb_t *hb)
{
    if (hb == NULL || hb->node == NULL || hb->node->tx == NULL ||
        hb->device == NULL || hb->device->table.entries != hb->device->entries ||
        hb->device->table.count != CO_DEVICE_OD_COUNT)
        return CO_ERR_ARGUMENT;
    if (hb->node->node_id == 0u || hb->node->node_id > CO_NODE_ID_MAX)
        return CO_ERR_NODE_ID;
    switch (hb->node->state)
    {
    case CO_NMT_INITIALIZATION:
    case CO_NMT_PRE_OPERATIONAL:
    case CO_NMT_OPERATIONAL:
    case CO_NMT_STOPPED:
        return CO_OK;
    default:
        return CO_ERR_ARGUMENT;
    }
}
```

这个 `validate()` 是 Heartbeat 模块内部的统一参数检查函数。它先确认 `co_hb_t` 及其关联对象都准备好了，再确认节点号和 NMT 状态合法。



#### 1：第一组参数检查

```c
if (hb == NULL || hb->node == NULL || hb->node->tx == NULL ||
    hb->device == NULL || hb->device->table.entries != hb->device->entries ||
    hb->device->table.count != CO_DEVICE_OD_COUNT)
    return CO_ERR_ARGUMENT;
```

这几个条件使用 `||` 连接，只要有一个条件成立，就返回参数错误。



###### `hb == NULL`

```
hb == NULL
```

表示调用者根本没有提供 Heartbeat 对象：

```c
co_hb_process(NULL, 100);
```

此时不能访问：

```
hb->node
```

否则会解引用空指针，所以直接返回：

```
CO_ERR_ARGUMENT
```



###### `hb->node == NULL`

```
hb->node == NULL
```

表示 Heartbeat 对象存在，但没有绑定节点上下文：

```
co_hb_t hb = {0};
```

此时：

```
hb.node == NULL
```

Heartbeat 不知道节点号、NMT 状态和发送回调，因此不能工作。



###### `hb->node->tx == NULL`

```
hb->node->tx == NULL
```

表示节点虽然存在，但没有发送回调。

Heartbeat 最终需要：

```
组装心跳帧
    ↓
co_send()
    ↓
node->tx()
```

如果 `tx` 是空指针，就没有办法把报文交给传输层。





###### `hb->device == NULL`

```
hb->device == NULL
```

表示没有绑定设备对象字典。

Heartbeat 必须从对象字典读取：

```
0x1017:00 = Heartbeat 周期
```

没有 `device` 就无法知道应该多久发送一次。



###### `hb->device->table.entries != hb->device->entries`

```
hb->device->table.entries != hb->device->entries
```

这是在检查对象表是否正确绑定。

设备结构体中有：

```
co_od_entry_t entries[CO_DEVICE_OD_COUNT];
co_od_table_t table;
```

初始化成功后，应该是：

```
device->table.entries = device->entries;
```

也就是：

```
table.entries ──┐
                └── 指向同一组对象条目
device->entries ┘
```

如果两个地址不相等，说明对象字典没有正确初始化，或者结构体被错误复制、破坏过。





###### `hb->device->table.count != CO_DEVICE_OD_COUNT`

```
hb->device->table.count != CO_DEVICE_OD_COUNT
```

检查对象条目数量是否符合项目预期。

当前项目规定：

```
#define CO_DEVICE_OD_COUNT 36u
```

所以正常情况应该是：

```
device->table.count == 36
```

如果数量不对，Heartbeat 使用的设备对象表就不可信，因此返回参数错误。

第一组检查整体上可以理解为：

```
Heartbeat 对象存在吗？
    ↓
节点对象存在吗？
    ↓
节点有发送回调吗？
    ↓
设备对象字典存在吗？
    ↓
对象表内部绑定正确吗？
    ↓
对象数量正确吗？
```





#### 2：第二组检查

检查 Node-ID

```c
if (hb->node->node_id == 0u || hb->node->node_id > CO_NODE_ID_MAX)
    return CO_ERR_NODE_ID;
```

当前项目规定：

```
CO_NODE_ID_MAX = 127
```

有效节点号是：

```
1 ~ 127
```

因此：

```
Node-ID = 0       非法，0 专门表示 NMT 广播目标
Node-ID = 1~127   合法
Node-ID > 127     非法
```

Heartbeat 的 CAN-ID 是：

```
0x700 + Node-ID
```

例如：

```
Node-ID = 2
CAN-ID  = 0x702
```

如果 Node-ID 是 0，Heartbeat 就会错误地使用：

```
0x700
```

这不是普通节点的 Heartbeat CAN-ID，所以必须拒绝。





#### 3：第三组检查

检查 NMT 状态

```c
switch (hb->node->state)
{
case CO_NMT_INITIALIZATION:
case CO_NMT_PRE_OPERATIONAL:
case CO_NMT_OPERATIONAL:
case CO_NMT_STOPPED:
    return CO_OK;
default:
    return CO_ERR_ARGUMENT;
}
```

这里检查节点当前状态是否是项目定义的四种合法 NMT 状态：

```c
CO_NMT_INITIALIZATION       // 0x00
CO_NMT_STOPPED              // 0x04
CO_NMT_OPERATIONAL          // 0x05
CO_NMT_PRE_OPERATIONAL      // 0x7F
```

只要属于其中之一，就返回：

```
CO_OK
```

注意，返回 `CO_OK` 只代表：

> Heartbeat 上下文和节点状态合法，可以继续执行。

它不代表此刻一定要发送 Heartbeat。

例如：

```
node->state = CO_NMT_INITIALIZATION;
```

`validate()` 仍然返回 `CO_OK`，因为 Initialization 是合法状态。但后续 `co_hb_process()` 会单独判断：

```
if (hb->node->state == CO_NMT_INITIALIZATION)
    return CO_IGNORED;
```

也就是说：

```c
validate()
    检查状态值是否合法

co_hb_process()
    决定当前状态是否允许产生周期 Heartbeat
```

这两个职责不同。



#### 4：为什么这里不检查 Heartbeat 周期？

`validate()` 没有检查：

```c
0x1017:00
```

是否为 0、500 或 1000。

因为：

```c
周期为 0 是合法配置，表示关闭 Heartbeat
周期为非 0 是合法配置，表示发送周期
```

所以周期不是上下文参数错误，而是业务配置，由 `co_hb_process()` 读取并处理。

此外，`validate()` 也不检查 `elapsed_ms` 是否超过周期，因为计时状态本身可以暂时大于周期，后续函数会决定是否发送。

------

整个函数可以概括为：

```c
检查 Heartbeat 对象
    ├── hb 指针有效
    ├── node 指针有效
    ├── tx 回调有效
    ├── device 指针有效
    ├── 对象表地址绑定正确
    └── 对象条目数量正确
            ↓
检查 Node-ID 是否为 1~127
            ↓
检查 NMT 状态是否是四种合法状态
            ↓
返回 CO_OK 或具体错误码
```

它只负责回答一个问题：

> 当前 `co_hb_t` 是否具备继续运行 Heartbeat 服务的基本条件？





## 2：co_hb_init（）

```c
co_status_t co_hb_init(co_hb_t *hb, co_context_t *node,
                       co_device_od_t *device)
{
    co_hb_t candidate = {node, device, 0u, 0u};
    uint16_t period;
    co_status_t status;

    if (hb == NULL)
        return CO_ERR_ARGUMENT;
    status = validate(&candidate);
    if (status != CO_OK)
        return status;
    status = co_device_od_get_heartbeat(device, &period,
                                        &candidate.observed_writes);
    if (status != CO_OK)
        return status;
    (void)period;
    *hb = candidate;
    return CO_OK;
}
```

这个 `co_hb_init()` 的作用是：

> 把节点和设备对象字典绑定到 Heartbeat 对象，并记录当前 `0x1017:00` 的写入次数，为后续计时做好初始准备。



#### 1. 先创建临时对象 `candidate`

```c
co_hb_t candidate = {node, device, 0u, 0u};
```

这行创建了一个临时的 `co_hb_t`：

```c
candidate.node           = node
candidate.device         = device
candidate.elapsed_ms     = 0
candidate.observed_writes = 0
```

它相当于先准备一份“候选配置”，而不是一开始就直接修改调用者传入的 `*hb`。

为什么要这样做？

因为后面还要进行检查。如果检查失败，函数应该返回错误，并且尽量不破坏原来的 `hb` 内容。

流程是：

```c
先构造 candidate
    ↓
检查 candidate 是否有效
    ↓
读取对象字典状态
    ↓
全部成功后
    ↓
一次性写入 *hb
```



#### 2. 定义临时变量

```c
uint16_t period;
co_status_t status;
```

`period` 用于接收当前心跳周期：

```
0x1017:00
```

例如：

```
period = 1000
```

表示当前配置为 1000 ms。

`status` 用于保存各个函数的返回状态：

```
status = validate(&candidate);
```





#### 3. 检查 `hb` 指针

```c
if (hb == NULL)
    return CO_ERR_ARGUMENT;
```

这里检查的是“最终要写入的目标地址”。

例如：

```
co_hb_init(NULL, &node, &device);
```

即使 `node` 和 `device` 都有效，也没有地方保存初始化结果，所以直接返回：

```
CO_ERR_ARGUMENT
```

注意这里没有立即检查 `node` 和 `device`，而是把它们先放进 `candidate`，交给后面的：

```
validate(&candidate)
```

统一检查。





#### 4. 检查候选配置

```c
status = validate(&candidate);
if (status != CO_OK)
    return status;
```

这里会检查：

```c
candidate.node 是否为空
node->tx 是否存在
candidate.device 是否为空
对象表是否正确初始化
对象条目数量是否正确
Node-ID 是否在 1~127
NMT 状态是否有效
```

如果有一项不合格，就直接返回对应错误。

例如：

```c
co_hb_t hb;
co_context_t node = {0};
co_device_od_t device = {0};

co_hb_init(&hb, &node, &device);
```

因为 `node.tx` 还没有绑定，`validate()` 会返回：

```
CO_ERR_ARGUMENT
```

此时 `*hb` 还没有被赋值。



#### 5. 读取当前 Heartbeat 信息

```c
status = co_device_od_get_heartbeat(device, &period,&candidate.observed_writes); 
```

这个函数从设备对象字典中读取两个值：

```c
period
    当前 0x1017:00 的周期值

candidate.observed_writes
    当前 0x1017:00 成功写入次数
```

例如对象字典当前是：

```
0x1017:00 value  = 1000
0x1017:00 writes = 3
```

调用完成后：

```
period                    = 1000
candidate.observed_writes = 3
```

Heartbeat 对象将来就知道：

```
当前周期是 1000 ms
我已经观察到第 3 次写入
```

不过这里的 `period` 只是被读取出来验证接口调用成功，当前函数没有把它保存到 `co_hb_t` 中。

```
(void)period;
```



#### 6. `(void)period` 是什么意思？

```
(void)period;
```

它表示：

> 当前函数确实需要传入 `period` 的地址来调用读取函数，但读取出来的周期暂时不直接使用。

如果不写这一句，编译器可能警告：

```
period 被赋值但没有使用
```

Heartbeat 的设计是每次执行：

```
co_hb_process()
```

时重新读取当前周期，而不是在初始化时永久保存周期。

所以初始化时真正需要保留下来的是：

```
candidate.observed_writes
```

而不是 `period`。





#### 7.提交初始化结果

```
*hb = candidate;
```

只有前面的检查都成功后，才把临时对象完整复制到调用者的 `hb` 中。

例如最终得到：

```c
hb->node            = node
hb->device          = device
hb->elapsed_ms      = 0
hb->observed_writes = 当前对象的 writes
```

这里是结构体整体赋值，复制的是四个成员：

```c
hb->node = candidate.node;
hb->device = candidate.device;
hb->elapsed_ms = candidate.elapsed_ms;
hb->observed_writes = candidate.observed_writes;
```

其中两个指针仍然指向原来的：

```c
node
device
```

不会复制整个节点对象或整个设备对象。



#### 8. 返回成功

```
return CO_OK;
```

表示：

```
Heartbeat 对象已经成功绑定
计时从 0 开始
已经记录当前 0x1017 的写入次数
```

但它不会：

```
发送 Heartbeat
改变 NMT 状态
发送 Boot-up
修改对象字典
```

发送工作由后面的：

```
co_hb_process()
```

负责。





#### 9：整个函数可以画成

```c
co_hb_init(&hb, &node, &device)
            │
            ▼
创建 candidate 临时对象
            │
            ▼
检查 hb、node、device 和对象表
            │
            ├── 失败 → 返回错误，hb 不提交
            │
            ▼
读取 0x1017 当前值和 writes
            │
            ├── 失败 → 返回错误，hb 不提交
            │
            ▼
*hb = candidate
            │
            ▼
返回 CO_OK
```





## 3：co_hb_process（）

```c
co_status_t co_hb_process(co_hb_t *hb, uint32_t elapsed_ms)
{
    can_frame_t frame = {0};
    uint16_t period;
    uint32_t writes;
    co_status_t status = validate(hb);

    if (status != CO_OK)
        return status;
    if (hb->node->state == CO_NMT_INITIALIZATION)
    {
        hb->elapsed_ms = 0u;
        return CO_IGNORED;
    }
    status = co_device_od_get_heartbeat(hb->device, &period, &writes);
    if (status != CO_OK)
        return status;
    if (writes != hb->observed_writes)
    {
        hb->observed_writes = writes;
        hb->elapsed_ms = 0u;
    }
    if (period == 0u)
    {
        hb->elapsed_ms = 0u;
        return CO_IGNORED;
    }
    if (elapsed_ms > UINT32_MAX - hb->elapsed_ms)
        hb->elapsed_ms = UINT32_MAX;
    else
        hb->elapsed_ms += elapsed_ms;
    if (hb->elapsed_ms < period)
        return CO_IGNORED;

    frame.id = UINT32_C(0x700) + hb->node->node_id;
    frame.dlc = 1u;
    frame.data[0] = (uint8_t)hb->node->state;
    status = co_send(hb->node, &frame);
    if (status == CO_OK)
        hb->elapsed_ms %= period;
    return status;
}
```

这个函数是阶段 4 的核心：

> 每次被调用时，告诉 Heartbeat 模块“又经过了多少毫秒”，它根据 `0x1017:00` 判断是否到期；到期后组装并发送一帧 Heartbeat。

```
co_status_t co_hb_process(co_hb_t *hb, uint32_t elapsed_ms)
```

例如应用层每隔一段时间调用：

```
co_hb_process(&heartbeat, 10);
```

表示：

> 从上次调用到现在，经过了 10 ms。





#### 1. 准备局部变量

```c
can_frame_t frame = {0};
uint16_t period;
uint32_t writes;
co_status_t status = validate(hb);
```

分别表示：

```c
frame   准备发送的 Heartbeat CAN 帧
period  从 0x1017:00 读取出的周期
writes  从 0x1017:00 读取出的成功写入次数
status  函数执行状态
```

`frame = {0}` 会先把整个 CAN 帧清零，因此：

```c
is_extended = 0
is_remote   = 0
is_fd       = 0
data[]      全部为 0
```

这样只需要之后填写：

```c
frame.id
frame.dlc
frame.data[0]
```



#### 2. 先检查 Heartbeat 上下文

```c
co_status_t status = validate(hb);

if (status != CO_OK)
    return status;
```

这里调用刚刚学习的 `validate()`，检查：

```c
hb 是否有效
node 是否有效
tx 发送回调是否存在
device 对象字典是否初始化
Node-ID 是否为 1~127
NMT 状态是否有效
```

例如：

```c
co_hb_process(NULL, 10);
```

会返回：

```c
CO_ERR_ARGUMENT
```

如果上下文不合法，后续不会读取对象，也不会发送报文。





#### 3. Initialization 状态不发送周期 Heartbeat

```c
if (hb->node->state == CO_NMT_INITIALIZATION)
{
    hb->elapsed_ms = 0u;
    return CO_IGNORED;
}
```

这里非常关键。

`Initialization` 状态对应数值：

```
CO_NMT_INITIALIZATION = 0x00
```

而 Boot-up 报文也是：

```
CAN-ID = 0x700 + Node-ID
DLC    = 1
Data   = 00
```

所以不能在 Initialization 状态下反复发送普通 Heartbeat，否则会把：

```
Boot-up：只发送一次
```

错误地变成：

```
周期 Heartbeat：不断发送 00
```

因此：

```
节点处于 Initialization
        ↓
清零累计时间
        ↓
不发送 Heartbeat
        ↓
返回 CO_IGNORED
```

例如：

```
node.state = CO_NMT_INITIALIZATION;
co_hb_process(&hb, 5000);
```

结果：

```
不会发送
hb.elapsed_ms = 0
返回 CO_IGNORED
```

等阶段 3 的 Boot-up 成功后，节点进入：

```
CO_NMT_PRE_OPERATIONAL
```

Heartbeat 才开始正常计时。





#### 4. 从对象字典读取当前周期和写入次数

```c
status = co_device_od_get_heartbeat(hb->device, &period, &writes);
if (status != CO_OK)
    return status;
```

这里读取的是阶段 2 的对象：

```
0x1017:00
```

例如：

```c
period = 1000
writes = 3
```

含义是：

```
Heartbeat 周期为 1000 ms
这个对象已经成功写入过 3 次
```

Heartbeat 模块每次处理时重新读取 `period`，因此主站修改 `0x1017:00` 后，新周期能够生效。





#### 5. 检查 Heartbeat 周期是否被重新写入

```c
if (writes != hb->observed_writes)
{
    hb->observed_writes = writes;
    hb->elapsed_ms = 0u;
}
```

`hb->observed_writes` 保存的是上一次观察到的写入次数。

假设初始化时：

```
hb->observed_writes = 0
```

主站后来写入：

```
0x1017:00 = 500
writes    = 1
```

下一次调用时：

```
writes                 = 1
hb->observed_writes    = 0
```

比较结果：

```
1 != 0
```

于是执行：

```
hb->observed_writes = 1;
hb->elapsed_ms = 0;
```

意思是：

> 心跳配置刚刚发生变化，从这一刻重新开始计时。





#### 6.同值重写也会重启

即使主站再次写入相同的值：

```
原来：500 ms，writes = 1
再次写入：500 ms，writes = 2
```

周期数值没变，但：

```
writes = 2
observed_writes = 1
```

仍然满足：

```
writes != hb->observed_writes
```

因此计时器也会清零。

这就是阶段 2 中 `writes` 计数的用途。





#### 7. 周期为 0 时关闭 Heartbeat

```c
if (period == 0u)
{
    hb->elapsed_ms = 0u;
    return CO_IGNORED;
}
```

CANopen 中：

```
0x1017:00 = 0
```

表示关闭 Heartbeat Producer。

例如：

```
period = 0
```

则：

```
不累计时间
不发送报文
返回 CO_IGNORED
```

当主站以后重新写入：

```
period = 500
```

写入次数发生变化，函数会先将：

```
hb->elapsed_ms = 0;
```

然后开始新的 500 ms 计时。





#### 8. 累计经过的时间，并防止整数溢出

```c
if (elapsed_ms > UINT32_MAX - hb->elapsed_ms)
    hb->elapsed_ms = UINT32_MAX;
else
    hb->elapsed_ms += elapsed_ms;
```

正常情况下直接累加：

```
原来的 elapsed_ms = 300
这次经过 elapsed_ms = 200
新的 elapsed_ms = 500
```

等价于：

```
hb->elapsed_ms += elapsed_ms;
```

但 `uint32_t` 最大值有限：

```
UINT32_MAX = 4294967295
```

如果当前：

```
hb->elapsed_ms = UINT32_MAX - 10
```

这次又经过：

```
elapsed_ms = 100
```

直接相加就会溢出，数值反而变小。

所以函数先判断：

```
elapsed_ms > UINT32_MAX - hb->elapsed_ms
```

如果会溢出，就把计时器固定为最大值：

```
hb->elapsed_ms = UINT32_MAX;
```

这叫饱和处理：

```
达到上限后保持最大值，不回绕成小数
```



#### 9. 未到周期，不发送

```c
if (hb->elapsed_ms < period)
    return CO_IGNORED;
```

例如：

```c
period       = 1000
elapsed_ms   = 600
```

因为：

```
600 < 1000
```

所以：

```
还没到发送时间
不组装报文
不调用 co_send()
返回 CO_IGNORED
```

只有：

```c
elapsed_ms >= period
```

才会继续发送。

注意这里使用的是“大于等于”逻辑：

```
elapsed_ms == period
```

也算到期。





#### 10. 组装 Heartbeat 报文

```c
frame.id = UINT32_C(0x700) + hb->node->node_id;
frame.dlc = 1u;
frame.data[0] = (uint8_t)hb->node->state;
```

假设：

```
Node-ID = 2
NMT 状态 = CO_NMT_OPERATIONAL
```

而：

```
CO_NMT_OPERATIONAL = 0x05
```

则组装出的帧是：

```
CAN-ID = 0x700 + 2 = 0x702
DLC    = 1
Data   = 05
```

对应代码：

```
frame.id     = 0x702
frame.dlc    = 1
frame.data[0] = 0x05
```

如果节点状态是：

```
Pre-operational
```

那么发送：

```
frame.data[0] = 0x7F
```

如果节点状态是：

```
Stopped
```

那么发送：

```
702 [04]
```

所以 Heartbeat 数据字节不是固定值，而是发送时读取：

```
hb->node->state
```





#### 11.通过统一发送入口发送

```c
status = co_send(hb->node, &frame);
```

Heartbeat 自己不直接调用底层 `tx`，而是调用阶段 1 的统一入口：

```c
co_send()
    ↓
检查节点上下文
    ↓
检查 CAN 帧格式
    ↓
调用 node->tx
```

这样 Heartbeat 就不需要知道：

```c
USB-CAN 如何发送
STM32 CAN1 如何发送
测试 fake_tx 如何记录
```

它只负责组装协议帧。





#### 12. 发送成功后保留多余时间

```c
if (status == CO_OK)
    hb->elapsed_ms %= period;
```

假设：

```c
period     = 1000
elapsed_ms = 1050
```

说明这次调用时已经超过周期 50 ms。

发送成功后：

```
1050 % 1000 = 50
```

因此：

```
hb->elapsed_ms = 50;
```

保留这 50 ms，而不是直接清零。

如果直接清零，长期运行时可能产生节拍误差：

```c
实际每次 1050 ms 才调用
却每次发送后归零
```

会让发送节奏越来越偏。

使用余数后：

```c
1050 ms 到期发送，保留 50 ms
再过 950 ms 就到下一次
```

更接近真实周期。





#### 13. 发送失败时不清零

```c
if (status == CO_OK)
    hb->elapsed_ms %= period;
return status;
```

只有发送成功才执行取余。

如果发送失败：

```c
co_send(...) == CO_ERR_TX_BUSY
```

那么：

```
hb->elapsed_ms 仍然保持到期状态
```

下一次调用时，即使：

```
elapsed_ms = 0
```

仍然满足：

```
hb->elapsed_ms >= period
```

于是可以继续重试。

例如：

```c
第一次到期：发送忙，返回 CO_ERR_TX_BUSY
第二次调用：再次尝试发送
发送成功：才更新计时器
```

这与阶段 3 Boot-up 的失败重试逻辑一致：

```
发送失败不能假装已经完成
```





#### 整个函数流程

```c
co_hb_process(&hb, elapsed_ms)
              │
              ▼
检查 Heartbeat 上下文
              │
              ├── 失败 → 返回错误
              │
              ▼
节点是否处于 Initialization？
              │
              ├── 是 → 清零计时，不发送
              │
              ▼
读取 0x1017 周期和 writes
              │
              ▼
writes 是否变化？
              │
              ├── 是 → 更新记录，计时清零
              │
              ▼
周期是否为 0？
              │
              ├── 是 → Heartbeat 关闭
              │
              ▼
累计 elapsed_ms
              │
              ▼
是否达到周期？
              │
              ├── 否 → 返回 CO_IGNORED
              │
              ▼
组装 0x700 + Node-ID 心跳帧
              │
              ▼
调用 co_send()
              │
              ├── 失败 → 保留到期状态，返回错误
              │
              └── 成功 → 保留超出周期的余量，返回 CO_OK
```

这一个函数把前面三个阶段连接起来了：

```c
阶段 1：co_send()
    └── 负责检查并提交 CAN 帧

阶段 2：co_device_od_get_heartbeat()
    └── 提供 0x1017:00 周期和写入次数

阶段 3：node->state
    └── 提供当前 NMT 状态字节

阶段 4：co_hb_process()
    └── 负责计时、组帧和定期发送
```





## 4：和应用层的关系

因此完整逻辑是：

```c
应用层每 200 ms 调用一次
              │
              ▼
co_hb_process(&hb, 200)
              │
              ▼
hb->elapsed_ms 累加 200
              │
              ▼
累计值是否达到 period？
              │
       ┌──────┴──────┐
       │             │
      否             是
       │             │
返回 CO_IGNORED    发送 Heartbeat
```

发送成功后还有这句：

```
if (status == CO_OK)
    hb->elapsed_ms %= period;
```

如果刚好累计到 1000：

```
1000 % 1000 = 0
```

所以计时重新从 0 开始。

如果某次调用间隔不规则，例如累计到了 1150：

```
1150 % 1000 = 150
```

那么发送后保留 150 ms，下一次只需再累计 850 ms 就会发送下一帧。



#### 逻辑：

应用层需要周期性调用：

```
co_hb_process(&hb, elapsed_ms);
```

Heartbeat 模块本身没有独立线程，也没有硬件定时器。它不会自动运行，必须由应用层持续“喂入”经过的时间。

例如应用层每 200 ms 调用一次：

```
co_hb_process(&hb, 200);
```

完整过程是：

```c
应用层定时任务
      │
      ├── 第一次：co_hb_process(&hb, 200)
      ├── 第二次：co_hb_process(&hb, 200)
      ├── 第三次：co_hb_process(&hb, 200)
      ├── 第四次：co_hb_process(&hb, 200)
      └── 第五次：co_hb_process(&hb, 200)
                              │
                              ▼
                     累计到 1000 ms
                              │
                              ▼
                     发送一帧 Heartbeat
```

例如在 PC 测试中，可以这样模拟：

```c
for (;;)
{
    co_hb_process(&hb, 200);
    /* 等待或模拟经过 200 ms */
}
```

在 STM32 + FreeRTOS 中，可以放进周期任务：

```c
void protocol_task(void *argument)
{
    for (;;)
    {
        co_hb_process(&heartbeat, 10);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

这里：

```c
vTaskDelay(10 ms)
    模拟或等待 10 ms

co_hb_process(&heartbeat, 10)
    告诉 Heartbeat 模块又过去了 10 ms
```

如果周期对象设置为：

```
0x1017:00 = 1000 ms
```

那么大约调用 100 次后，累计时间达到 1000 ms，就会发送一帧 Heartbeat。

还可以使用系统节拍差值，而不要求固定调用周期：

```c
uint32_t now = get_tick_ms();
uint32_t delta = now - last_tick;
last_tick = now;

co_hb_process(&heartbeat, delta);
```

这样即使任务有轻微延迟，也能把真实经过的时间传给 Heartbeat 模块。

需要区分两件事：

```c
应用层循环调用 co_hb_process()
    负责驱动 Heartbeat 逻辑

co_hb_process() 内部
    负责累计时间、检查周期、组帧和发送
```

所以当前设计是“被动服务”：

```c
应用层负责定时调用
Heartbeat 模块负责协议行为
```

而不是在 `co_hb.c` 里自己创建线程或死循环。这样纯 C 协议核心可以在：

```c
PC 测试程序
裸机主循环
FreeRTOS 任务
```

中复用。



#### 两个容易混淆的变量：

这里有两个不同的 `elapsed_ms`：

```c
函数参数 elapsed_ms
    形参
    表示本次调用新增的时间

hb->elapsed_ms
    结构体成员
    表示已经累计的时间
```

调用时：

```
形参 elapsed_ms = 200
hb->elapsed_ms  = 之前保存的累计值
```

函数中的这句：

```
hb->elapsed_ms += elapsed_ms;
```

意思是：

```c
结构体原有的累计时间
+
本次传入的新时间
=
更新后的累计时间
```

例如：

```c
调用前：
hb->elapsed_ms = 600

本次调用：
elapsed_ms = 200

执行：
hb->elapsed_ms += elapsed_ms

结果：
hb->elapsed_ms = 800
```

它们虽然名字相同，但属于不同层次：

```c
elapsed_ms
    └── 当前函数调用中的局部形参

hb->elapsed_ms
    └── hb 指向的 co_hb_t 结构体中的成员
```

可以把代码改写成更容易区分的伪代码：

```c
co_status_t co_hb_process(co_hb_t *hb, uint32_t new_time_ms)
{
    hb->elapsed_ms += new_time_ms;
}
```

只是正式代码使用了相同的名字。



#### 完整关系：

```c
co_hb_process(&hb, 200)
                         │
                         ├── 形参 elapsed_ms = 200
                         │
                         └── hb 指向的结构体：
                             hb->elapsed_ms = 原累计值
```

然后执行：

```
hb->elapsed_ms += elapsed_ms;
```

最后：

```
结构体中的累计值被更新并保留下来
形参本身随着本次函数调用结束
```

这正是 Heartbeat 能够跨多次调用累计时间的原因。





## 5：总览

```c
阶段 4 的 .h 和 .c 核心函数已经梳理清楚了：
co_hb_t
    保存节点、对象字典和计时状态

co_hb_init()
    绑定节点与对象字典，记录初始写入次数

co_hb_process()
    读取周期、累计时间、判断到期并发送 Heartbeat
```

