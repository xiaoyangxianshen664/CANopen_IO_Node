# 解析：

## 1：static co_status_t validate(const co_nmt_t *nmt)

```c
static co_status_t validate(const co_nmt_t *nmt)
{
    if (nmt == NULL || nmt->node == NULL || nmt->node->tx == NULL ||
        nmt->device == NULL || nmt->device->table.entries != nmt->device->entries ||
        nmt->device->table.count != CO_DEVICE_OD_COUNT)
        return CO_ERR_ARGUMENT;
    if (nmt->node->node_id == 0 || nmt->node->node_id > CO_NODE_ID_MAX)
        return CO_ERR_NODE_ID;
    switch (nmt->node->state) {
    case CO_NMT_INITIALIZATION:
    case CO_NMT_PRE_OPERATIONAL:
    case CO_NMT_OPERATIONAL:
    case CO_NMT_STOPPED: return CO_OK;
    default: return CO_ERR_ARGUMENT;
    }
}
```

这个 `validate()` 的作用是：在执行 NMT 操作之前，检查传入的 NMT 上下文是否满足基本要求。它只检查并返回结果，不修改节点状态，也不发送报文。

我们沿用昨天的具体变量来理解。

```c
co_context_t my_node = {0};
co_device_od_t my_device = {0};
co_nmt_t my_nmt = {0};
```

假设 `my_node` 和 `my_device` 已经通过各自的初始化函数初始化，`my_nmt` 已连接到它们：

```
my_nmt.node = &my_node;
my_nmt.device = &my_device;
```

调用：

```c
static co_status_t validate(const co_nmt_t *nmt)
co_status_t result = validate(&my_nmt);
形参nmt=&my_nmt 									//进入函数后，参数 nmt 接收到的就是 &my_nmt：
nmt ──> my_nmt
        ├── node ──────> my_node
        │               ├── node_id
        │               ├── state
        │               ├── tx
        │               └── tx_user
        │
        └── device ────> my_device
                        ├── fields[]
                        ├── entries[]
                        └── table
    const co_nmt_t *nmt：通过指针读取 NMT 上下文，不通过它修改 my_nmt 本身的成员。
```



#### 1：第一组检查

```c
if (nmt == NULL || nmt->node == NULL || nmt->node->tx == NULL ||
    nmt->device == NULL || nmt->device->table.entries != nmt->device->entries ||
    nmt->device->table.count != CO_DEVICE_OD_COUNT)
    return CO_ERR_ARGUMENT;
```

`||` 表示“或者”。**只要其中一个条件成立，就返回参数错误。**

把这些条件换成我们的具体变量，就容易理解了：

| 检查条件                            | 对应的具体含义                         |
| ----------------------------------- | -------------------------------------- |
| `nmt == NULL`                       | 有没有传入 NMT 上下文地址              |
| `nmt->node == NULL`                 | `my_nmt.node` 有没有连接节点上下文     |
| `nmt->node->tx == NULL`             | `my_node.tx` 有没有绑定发送函数        |
| `nmt->device == NULL`               | `my_nmt.device` 有没有连接设备对象字典 |
| `table.count != CO_DEVICE_OD_COUNT` | 条目数量是否为当前规定的 36            |
| table.entries != device->entries``  | 查找表是否指向这份设备自己的条目数组   |



其中`table.entries != device->entries` 比较难理解，此处做一下解析：

我们创建一个实际变量和指针：

```c
co_device_od_t my_device = {0};

co_device_od_t *device = &my_device;
它们的关系：
device 指向结构体的地址────> my_device
                             ├── fields[36]
                             ├── entries[36]
                             └── table
                                 ├── entries
                                 └── count
这里有两个都叫 entries 的成员，但它们的类型不同。
typedef struct
{
    co_od_entry_t *entries;  // 指针成员
    size_t count;
} co_od_table_t;

typedef struct
{	
    co_device_value_t fields[CO_DEVICE_OD_COUNT];//存数值的结构体成员
    co_od_entry_t entries[36]; // 数组成员
    co_od_table_t table;       // 结构体成员
} co_device_od_t;

-> 的意思是：通过结构体指针，访问它所指向结构体的成员。 它并不意味着“访问到的成员一定是指针”，成员是什么类型，要看声明。
device->entries  // entries声明为结构体数组，所以这里访问的是数组成员
device->table    // table声明为结构体，所以这里访问的是结构体成员
```



接下来，初始化时执行：

```c
device->table.entries = device->entries;
给my_device 的结构体成员table 的子成员结构体指针变量entries赋值，让他指向entries[36]数组首地址
    
数组名用于这里的赋值表达式时，会转换为首元素地址，因此相当于：
my_device.table.entries = &my_device.entries[0];
也就是把左边的指针连接到右边的实际数组：
my_device.table.entries
          │ 保存首元素地址
          ▼
my_device.entries[0]
my_device.entries[1]
my_device.entries[2]
...
my_device.entries[35]

```



再回到我们的检查函数没弄懂的这一句： 

```c
device->table.entries != device->entries
右边数组同样转换为首元素指针，所以相当于：
my_device.table.entries != &my_device.entries[0]
它在问：
table.entries 保存的地址，是否与这份设备自己的条目数组首地址不同？

- 相同：连接正确，!= 的结果为假。
- 不同：连接不符合要求，!= 的结果为真，返回错误。
device->entries
// 访问数组成员；在本次比较中转换为首元素指针

device->table.entries
// 访问table中的指针成员，它本身就是指针
```



#### 2：第二组检查

```c
if (nmt->node->node_id == 0 ||nmt->node->node_id > CO_NODE_ID_MAX)
  return CO_ERR_NODE_ID;

//换成具体变量：
if (my_node.node_id == 0 || my_node.node_id > 127)
    return CO_ERR_NODE_ID;
```

例如：

```
my_node.node_id = 1     → 通过
my_node.node_id = 127   → 通过
my_node.node_id = 0     → 返回 CO_ERR_NODE_ID
my_node.node_id = 128   → 返回 CO_ERR_NODE_ID
```

这里检查的是**节点自己的编号**。NMT 报文中的目标地址可以用 `0` 表示广播，但节点自身不能取编号 `0`。



#### 3：最后检查节点当前状态：

```c
switch (nmt->node->state) {
case CO_NMT_INITIALIZATION:
case CO_NMT_PRE_OPERATIONAL:
case CO_NMT_OPERATIONAL:
case CO_NMT_STOPPED:
    return CO_OK;

default:
    return CO_ERR_ARGUMENT;
}
```

相当于检查：

```
switch (my_node.state)
```

前面几个 `case` 后面没有执行语句，它们共同使用：

```
return CO_OK;
```

所以：

```
INITIALIZATION ─────┐
PRE_OPERATIONAL ────┤
OPERATIONAL ────────┼──> CO_OK
STOPPED ────────────┘

其他数值 ──────────────> CO_ERR_ARGUMENT
```

这里**没有改变状态**。比如原来是：

```
my_node.state = CO_NMT_STOPPED;
```

检查后仍然是 `CO_NMT_STOPPED`，只是函数返回 `CO_OK`，说明这是一个合法状态。

也要区分：

```c
my_node.state   // 节点处于什么状态
result         // 本次检查是否通过
```

`validate()` 返回 `CO_OK`，不表示节点进入了 Operational，也不表示当前状态允许执行所有 NMT 操作；具体操作的状态限制由后面的函数继续判断。



#### 4：总览

整个函数的执行顺序就是：

```c
validate(&my_nmt)
        │
        ▼
检查指针、发送回调、对象表连接和数量
        │ 不满足 → CO_ERR_ARGUMENT
        ▼
检查本节点编号是否为1..127
        │ 不满足 → CO_ERR_NODE_ID
        ▼
检查当前状态是否属于四种合法状态
        │ 不满足 → CO_ERR_ARGUMENT
        ▼
返回 CO_OK
另外，这里没有检查 notify 和 user 是否为空：通知回调本来就是可选的；user 是否需要非空，由具体回调的使用方式决定。
```





## 2：static void change_state(co_nmt_t *nmt, co_nmt_state_t state)

```c
static void change_state(co_nmt_t *nmt, co_nmt_state_t state)
{
    if (nmt->node->state == state)
        return;
    nmt->node->state = state;
    if (nmt->notify != NULL)
        nmt->notify(nmt->user, state);
}
```

这个函数的作用是：**把节点切换到新的 NMT 状态，并在状态真正发生变化时通知外部应用。**

先准备具体变量：

```c
co_context_t my_node = {0};
co_nmt_t my_nmt = {0};
app_data_t my_app = {0};
```

```c
my_nmt.node = &my_node;
my_nmt.notify = app_on_nmt_state;
my_nmt.user = &my_app;
```

然后：

```c
co_nmt_t *nmt = &my_nmt;
此时函数调用：
change_state(nmt, CO_NMT_OPERATIONAL); //也即change_state(&my_nmt, CO_NMT_OPERATIONAL);
```



#### 1. 函数声明

```
static void change_state(co_nmt_t *nmt,
                         co_nmt_state_t state)
```

各部分含义：

```c
static
    只允许当前 co_nmt.c 文件内部调用

void
    不返回结果

co_nmt_t *nmt
    指向要操作的 NMT 上下文

co_nmt_state_t state
    希望切换到的目标状态
```

注意，`state` 是一个普通的枚举变量，保存目标状态；`nmt` 是指针，指向 NMT 上下文。





#### 2. 判断状态是否已经相同

```
if (nmt->node->state == state)
    return;
```

假设当前状态是：

```
my_node.state = CO_NMT_PRE_OPERATIONAL;
```

调用：

```
change_state(&my_nmt, CO_NMT_PRE_OPERATIONAL);
```

函数内部实际比较的是：

```
my_nmt.node->state == CO_NMT_PRE_OPERATIONAL
```

因为：

```
my_nmt.node == &my_node
```

所以等价于：

```
my_node.state == CO_NMT_PRE_OPERATIONAL
```

条件成立后直接：

```
return;
```

这表示：

```
目标状态和当前状态相同
        │
        ├── 不重复赋值
        ├── 不调用通知回调
        └── 函数结束
```

例如节点已经是 `Operational`，主站又重复发送一次 Start：

```
当前状态：0x05 Operational
Start目标：0x05 Operational
```

不会再次通知应用层。

这和文档中的要求一致：

```
重复 Start、Stop、Pre-op：
保持当前状态，不重复执行状态变化通知
```



#### 3. 写入新的状态

如果当前状态不同，就执行：

```
nmt->node->state = state;
```

例如：

```
my_node.state = CO_NMT_PRE_OPERATIONAL;
```

调用：

```
change_state(&my_nmt, CO_NMT_OPERATIONAL);
```

进入函数后：

```
nmt->node->state = state;
```

实际等价于：

```
my_node.state = CO_NMT_OPERATIONAL;
```

变化过程是：

```
调用前：

my_node.state = CO_NMT_PRE_OPERATIONAL

执行赋值：

my_node.state = CO_NMT_OPERATIONAL

调用后：

my_node.state = CO_NMT_OPERATIONAL
```

这里没有修改 `nmt` 指针本身，也没有创建新的节点。通过：

```
nmt->node
```

找到原来的 `my_node`，然后修改它的 `state` 成员。

访问路径可以拆成：

```
nmt
 │
 └── nmt->node
       │
       └── 指向 my_node
             │
             └── ->state
                   │
                   └── 修改 my_node.state
```



#### 4. 判断是否有通知函数

```
if (nmt->notify != NULL)
    nmt->notify(nmt->user, state);
```

这里检查：

```
nmt->notify != NULL
```

也就是：

```
my_nmt.notify != NULL
```

如果调用者初始化 NMT 时传入了通知函数：

```
my_nmt.notify = app_on_nmt_state;
```

那么条件成立，执行：

```
nmt->notify(nmt->user, state);
```

实际相当于：

```
app_on_nmt_state(&my_app, CO_NMT_OPERATIONAL);
```

回调收到两个参数：

```
第一个参数：
nmt->user
    └── &my_app

第二个参数：
state
    └── CO_NMT_OPERATIONAL
```

外部应用就能知道：

```
节点刚刚进入 Operational
```

然后可以执行应用层动作，例如：

```
允许 PDO
清除启动阶段标志
恢复正常过程数据处理
```



#### 5. 如果 notify 是 NULL

调用者也可以不提供通知回调：

```
my_nmt.notify = NULL;
my_nmt.user = NULL;
```

这时状态仍然会改变：

```
nmt->node->state = state;
```

但下面的判断不成立：

```
nmt->notify != NULL
```

因此不会调用函数。

也就是说：

```
notify == NULL
    ├── 状态仍然切换
    └── 只是没有通知外部应用
```

这就是为什么 `notify` 是可选回调。



#### 6. 完整执行例子

假设：

```c
my_node.state = CO_NMT_PRE_OPERATIONAL;
my_nmt.node = &my_node;
my_nmt.notify = app_on_nmt_state;
my_nmt.user = &my_app;
```

调用：

```c
change_state(&my_nmt, CO_NMT_OPERATIONAL);
```

执行顺序：

```
① 比较当前状态和目标状态

my_node.state == CO_NMT_OPERATIONAL
PRE_OPERATIONAL != OPERATIONAL
条件不成立，继续

② 修改节点状态

my_node.state = CO_NMT_OPERATIONAL

③ 检查 notify

my_nmt.notify != NULL
条件成立，继续

④ 调用回调

app_on_nmt_state(&my_app, CO_NMT_OPERATIONAL)
```

最终结果：

```c
my_node.state = CO_NMT_OPERATIONAL
my_app.last_state = CO_NMT_OPERATIONAL
my_app.notify_count 增加1
```





#### 7. 这个函数本身不做什么

`change_state()` 只负责两件事：

```
1. 修改 node->state
2. 调用状态通知回调
```

它本身不负责：

```
不发送 Boot-up
不发送 Heartbeat
不解析 CAN 帧
不检查 NMT 命令是否合法
不恢复对象字典
不直接操作 GPIO
```

这些工作由其他函数完成：

```
co_nmt_bootup()
    负责发送 Boot-up

co_nmt_receive()
    负责识别命令并决定目标状态

co_device_od_reset()
    负责复位对象数据

notify 回调
    由外部应用决定具体如何处理状态变化
```

整体关系是：

```
co_nmt_receive()
        │
        │ 收到 Start
        ▼
change_state(nmt, CO_NMT_OPERATIONAL)
        │
        ├── 修改 nmt->node->state
        │
        └── 调用 nmt->notify(nmt->user, state)
```

所以可以把它记成：

```
change_state(nmt, state);
```

就是：

> **如果目标状态和当前状态不同，就修改节点状态，并通知外部应用；如果相同，则什么都不做。**





## 3：co_status_t co_nmt_init

```c
co_status_t co_nmt_init(co_nmt_t *nmt, co_context_t *node,
                        co_device_od_t *device, co_nmt_notify_fn notify, void *user)
{
    co_nmt_t candidate = {node, device, notify, user};
    co_status_t status;
    if (nmt == NULL)
        return CO_ERR_ARGUMENT;
    status = validate(&candidate);
    if (status != CO_OK)
        return status;
    if (node->state != CO_NMT_INITIALIZATION)
        return CO_ERR_ARGUMENT;
    *nmt = candidate;
    if (notify != NULL)
        notify(user, CO_NMT_INITIALIZATION);
    return CO_OK;
}
```

这个函数的作用是：**把已经准备好的节点上下文和设备对象字典绑定到 NMT 上下文中，并确认节点当前确实处于 Initialization 状态。**

它本身不发送 Boot-up，也不切换状态。Boot-up 由后面的 `co_nmt_bootup()` 完成。



#### 1. 先准备实际变量

```c
co_nmt_t my_nmt = {0};
co_context_t my_node = {0};
co_device_od_t my_device = {0};
```

前面阶段 1 和阶段 2 先分别初始化：

```c
co_init(&my_node, 1, fake_tx, &bus);
co_device_od_init(&my_device, 1, NULL);
```

此时：

```c
my_node
├── node_id = 1
├── state   = CO_NMT_INITIALIZATION
├── tx      = fake_tx
└── tx_user = &bus

my_device
├── fields[]
├── entries[]
└── table
    ├── entries → my_device.entries
    └── count   = 36

my_nmt
├── node   = NULL
├── device = NULL
├── notify = NULL
└── user   = NULL
```

现在调用：

```c
co_nmt_init(&my_nmt,
            &my_node,
            &my_device,
            app_on_nmt_state,
            &my_app);
```

函数参数对应关系是：

```c
nmt    = &my_nmt
node   = &my_node
device = &my_device
notify = app_on_nmt_state
user   = &my_app
```



#### 2. 先创建临时的 `candidate`

函数第一句：

```c
co_nmt_t candidate = {node, device, notify, user};
```

这相当于：

```c
co_nmt_t candidate = {
    &my_node,
    &my_device,
    app_on_nmt_state,
    &my_app
};
```

所以此时 `candidate` 的内容是：

```c
candidate
├── node   ──────> my_node
├── device ──────> my_device
├── notify ──────> app_on_nmt_state()
└── user   ──────> my_app
```

注意：

> `candidate` 只是一个临时的局部结构体，它保存的是指针和函数地址，不会复制整个 `my_node` 或 `my_device`。

也就是说：

```c
candidate.node == &my_node
candidate.device == &my_device
candidate.notify == app_on_nmt_state
candidate.user == &my_app
```



#### 3. 为什么先验证 `candidate`，而不是直接写入 `*nmt`

下一段：

```c
co_status_t status;

if (nmt == NULL)
    return CO_ERR_ARGUMENT;
```

这里检查的是输出目标：

```
nmt
```

也就是调用者传进来的：

```
&my_nmt
```

如果调用：

```c
co_nmt_init(NULL, &my_node, &my_device,app_on_nmt_state, &my_app);
//应该填
co_nmt_init(&my_nmt, &my_node, &my_device,app_on_nmt_state, &my_app);
```

那么没有地方保存初始化结果，所以直接返回：

```
CO_ERR_ARGUMENT
```

而且此时还没有修改任何 NMT 对象。

接着：

```
status = validate(&candidate);
```

这里不是验证 `my_nmt`，因为 `my_nmt` 还没有被填充；验证的是已经装入参数的临时对象：

```
candidate
```

它会检查：

```
candidate.node 是否为空
candidate.node->tx 是否为空
candidate.device 是否为空
candidate.device->table.entries 是否正确
candidate.device->table.count 是否为36
candidate.node->node_id 是否在1..127
candidate.node->state 是否为合法状态
```

例如：

```
candidate.node->tx
```

实际就是：

```
my_node.tx
```

如果 `my_node` 还没有调用 `co_init()`，导致：

```
my_node.tx == NULL
```

那么 `validate(&candidate)` 返回：

```
CO_ERR_ARGUMENT
```

此时：

```
my_nmt
```

仍保持原来的内容，不会被部分写入。

这就是 `candidate` 的重要作用：

```c
先把待绑定内容放到临时变量
        │
        ▼
先完整检查
        │
        ├── 失败：直接返回，my_nmt 不变
        │
        └── 成功：一次性写入 my_nmt
```



#### 4. 保存 `validate()` 的返回结果

```c
status = validate(&candidate);
```

`status` 是一个 `co_status_t` 类型的变量，用于保存检查结果：

```c
CO_OK
CO_ERR_ARGUMENT
CO_ERR_NODE_ID
```

随后：

```
if (status != CO_OK)
    return status;
```

如果检查失败，原样返回具体错误。

例如：

```
my_node.node_id = 0;
```

那么：

```
validate(&candidate)
```

返回：

```
CO_ERR_NODE_ID
```

`co_nmt_init()` 也返回：

```
CO_ERR_NODE_ID
```

不会把所有错误都统一改成 `CO_ERR_ARGUMENT`。



#### 5. 再次确认节点处于 Initialization

```c
if (node->state != CO_NMT_INITIALIZATION)
    return CO_ERR_ARGUMENT;
```

这一句检查的是传入的实际节点：

```
node == &my_node
```

所以等价于：

```
if (my_node.state != CO_NMT_INITIALIZATION)
    return CO_ERR_ARGUMENT;
```

为什么 `validate()` 已经检查过状态，这里还要再检查一次？

因为这里有两个层次：

```
validate(&candidate)
    检查状态值是不是四种合法状态之一

co_nmt_init()
    进一步要求状态必须是 Initialization
```

也就是说：

```
合法状态 ≠ 适合初始化 NMT
```

例如：

```
my_node.state = CO_NMT_PRE_OPERATIONAL;
```

这个状态本身是合法的，所以 `validate()` 会返回 `CO_OK`。

但它不符合 NMT 初始化要求，因此后面的判断返回：

```
CO_ERR_ARGUMENT
```

只有：

```
my_node.state == CO_NMT_INITIALIZATION
```

才能继续绑定。

这可以理解为：

```
validate()
    “这个节点状态值有没有越界？”

co_nmt_init()
    “这个节点现在是不是还处于启动前的初始化状态？”
```





#### 6. 一次性把 `candidate` 复制到 `*nmt`

```
*nmt = candidate;
```

这里的 `nmt` 是指针：

```
nmt == &my_nmt
```

所以：

```
*nmt
```

表示它指向的实际结构体：

```
my_nmt
```

因此这一句等价于：

```
my_nmt = candidate;
```

执行后：

```
my_nmt
├── node   ──────> my_node
├── device ──────> my_device
├── notify ──────> app_on_nmt_state()
└── user   ──────> my_app
```

这是**结构体成员整体复制**，但复制的是：

```
node 指针的值
device 指针的值
notify 函数地址
user 指针的值
```

它不会复制：

```
my_node 的全部内容
my_device 的36个对象
my_app 的全部内容
```

所以复制后：

```c
my_nmt.node == &my_node;
my_nmt.device == &my_device;
my_nmt.user == &my_app;
```



#### 7. 初始化通知回调

```
if (notify != NULL)
    notify(user, CO_NMT_INITIALIZATION);
```

如果调用时传入了：

```
notify = app_on_nmt_state;
user = &my_app;
```

那么实际调用相当于：

```
app_on_nmt_state(&my_app, CO_NMT_INITIALIZATION);
```

这次通知表示：

> NMT 上下文已经绑定成功，目前节点状态是 Initialization。

例如回调可能执行：

```
static void app_on_nmt_state(void *user, co_nmt_state_t state)
{
    app_data_t *app = user;

    app->last_state = state;
    app->notify_count++;
}
```

执行后：

```
my_app.last_state   = CO_NMT_INITIALIZATION
my_app.notify_count = 1
```

如果调用者没有提供回调：

```
co_nmt_init(&my_nmt,
            &my_node,
            &my_device,
            NULL,
            NULL);
```

那么：

```
notify == NULL
```

不会调用通知函数，但 NMT 绑定仍然成功。



#### 8. 最后返回成功

```
return CO_OK;
```

到这里，初始化完成：

```c
my_nmt
├── node   → my_node
├── device → my_device
├── notify → app_on_nmt_state
└── user   → my_app
```

但节点状态仍然是：

```c
my_node.state == CO_NMT_INITIALIZATION
```

这点非常重要：

```c
co_nmt_init()
    只建立NMT上下文并验证初始状态

co_nmt_bootup()
    才负责发送Boot-up并进入Pre-operational
```



#### 9. 整个函数流程

```
co_nmt_init(&my_nmt, &my_node, &my_device, notify, &my_app)
        │
        ▼
创建candidate临时结构体
        │
        ▼
检查nmt输出指针
        │
        ├── NULL → 返回CO_ERR_ARGUMENT
        │
        ▼
validate(candidate)
        │
        ├── 节点/回调/对象表错误 → 返回错误
        │
        ▼
确认my_node.state == INITIALIZATION
        │
        ├── 不是 → 返回CO_ERR_ARGUMENT
        │
        ▼
my_nmt = candidate
        │
        ▼
如果notify存在，发送INITIALIZATION通知
        │
        ▼
返回CO_OK
```

可以把这个函数记成一句话：

> **先把参数组成临时 NMT 上下文，验证全部通过后，再一次性绑定到真正的 `my_nmt`；最后可选地通知应用当前仍处于 Initialization。





## 4：co_status_t co_nmt_bootup(co_nmt_t *nmt)

```c
co_status_t co_nmt_bootup(co_nmt_t *nmt)
{
    can_frame_t frame = {0};
    co_status_t status = validate(nmt);
    if (status != CO_OK)
        return status;
    if (nmt->node->state != CO_NMT_INITIALIZATION)
        return CO_IGNORED;
    frame.id = UINT32_C(0x700) + nmt->node->node_id;
    frame.dlc = 1;
    status = co_send(nmt->node, &frame);
    if (status == CO_OK)
        change_state(nmt, CO_NMT_PRE_OPERATIONAL);
    return status;
}
```

```c
这个函数负责完成节点的 Boot-up 发送流程：
1. 检查 NMT 上下文；
2. 确认节点当前仍在 Initialization；
3. 组装 Boot-up CAN 帧；
4. 通过阶段 1 的 co_send() 发送；
5. 发送被传输层接受后，把节点切换到 Pre-operational。
```



#### 1. 创建一个空的 CAN 帧

```
can_frame_t frame = {0};
```

这会创建局部变量 `frame`，并把所有成员清零：

```c
frame.id           = 0
frame.dlc          = 0
frame.data[0..7]   = 0
frame.is_extended  = 0
frame.is_remote     = 0
frame.is_fd         = 0
```

结合 `can_frame_t` 的定义：

```c
typedef struct
{
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint8_t is_extended;
    uint8_t is_remote;
    uint8_t is_fd;
} can_frame_t;
```

后面只修改：

```c
frame.id
frame.dlc
```

所以最终得到：

```
CAN-ID：0x700 + Node-ID
DLC：1
DATA[0]：0x00
```

为什么 `DATA[0]` 是 `0x00`？

因为：

```
can_frame_t frame = {0};
```

已经把 `data[]` 全部清零，而代码没有再修改 `frame.data[0]`。

对于 Node-ID=1：

```
frame.id  = 0x700 + 1 = 0x701
frame.dlc = 1
frame.data[0] = 0x00
```

最终 Boot-up 报文就是：

```
701  [00]
```





#### 2. 先调用 `validate()`

```c
co_status_t status = validate(nmt);
```

假设之前有：

```c
co_context_t my_node = {0};
co_device_od_t my_device = {0};
co_nmt_t my_nmt = {0};

co_init(&my_node, 1, fake_tx, &bus);
co_device_od_init(&my_device, 1, NULL);
co_nmt_init(&my_nmt, &my_node, &my_device, notify, &app);
```

调用：

```c
co_nmt_bootup(&my_nmt);
```

那么函数参数：

```
nmt == &my_nmt
```

`validate(nmt)` 会检查：

```c
nmt 是否为空
nmt->node 是否为空
nmt->node->tx 是否为空
nmt->device 是否为空
对象表地址是否正确
对象数量是否为36
Node-ID 是否在1..127
当前状态值是否是合法的四种状态
```

如果返回的不是 `CO_OK`：

```
if (status != CO_OK)
    return status;
```

函数立即结束，不构造有效 Boot-up，也不发送报文。

例如：

```
Node-ID = 0
    → validate() 返回 CO_ERR_NODE_ID
    → co_nmt_bootup() 返回 CO_ERR_NODE_ID
```

这里是**原样返回具体错误**，不会把所有错误都转换成同一个值。





#### 3. 确认当前状态必须是 Initialization

```c
if (nmt->node->state != CO_NMT_INITIALIZATION)
    return CO_IGNORED;
```

这句相当于：

```c
if (my_node.state != CO_NMT_INITIALIZATION)
    return CO_IGNORED;
```

Boot-up 只应该在：

```
Initialization → Pre-operational
```

这个启动过程里发送一次。

如果节点已经是：

```
CO_NMT_PRE_OPERATIONAL
CO_NMT_OPERATIONAL
CO_NMT_STOPPED
```

就不会再次发送 Boot-up，而是返回：

```
CO_IGNORED
```

例如：

```c
第一次调用：
Initialization
    → 发送 Boot-up
    → 进入 Pre-operational

第二次调用：
Pre-operational
    → 不再发送
    → 返回 CO_IGNORED
```

这和重复调用 `change_state()` 的逻辑不同：

- `change_state()` 处理状态相同时不重复通知；
- `co_nmt_bootup()` 处理状态已经不是初始化时，不重复发送 Boot-up。



#### 4. 计算 Boot-up 的 CAN-ID

```c
frame.id = UINT32_C(0x700) + nmt->node->node_id;
```

假设：

```c
my_node.node_id = 1;
```

那么：

```
frame.id = 0x700 + 1;
```

结果：

```
frame.id = 0x701;
```

如果节点号不同：

```c
Node-ID=1    → 0x701
Node-ID=2    → 0x702
Node-ID=127  → 0x77F
```

`UINT32_C(0x700)` 是一个宏，用于把常量明确表示成适合 `uint32_t` 的整数常量。

它不是 CANopen 的特殊命令，只是为了让常量类型更明确。实际运算仍然是：

```
0x700 + 节点号
```

这里也要区分：

```
0x700：Heartbeat/Boot-up 的基础 CAN-ID
0x701：Node-ID=1 时真正发送到总线的 CAN-ID
```





#### 5. 设置数据长度

```
frame.dlc = 1;
```

Boot-up 只有一个数据字节：

```
CAN-ID：0x701
DLC：1
DATA[0]：0x00
```

注意：

```
DLC = 1
```

表示有一个有效数据字节。

它不是“命令值 1”，真正的 Boot-up 内容是：

```
DATA[0] = 0x00
```

因为 `frame` 初始化时数据区已经全部清零。



#### 6. 通过 `co_send()` 发送

```
status = co_send(nmt->node, &frame);
```

这里传入两个参数：

```
nmt->node
```

它实际指向：

```
&my_node
```

以及：

```
&frame
```

即当前函数里组装好的局部 CAN 帧地址。

调用关系是：

```
co_nmt_bootup()
        │
        ▼
co_send(&my_node, &frame)
        │
        ├── 检查节点上下文
        ├── 检查 CAN 帧格式
        └── 调用 my_node.tx
```

最终阶段 1 中保存的发送回调会被调用：

```
my_node.tx(my_node.tx_user, &frame);
```

PC 测试中可能是：

```
fake_tx(&bus, &frame);
```

硬件接入后才会替换为真实 CAN 发送回调。

`co_send()` 返回的结果可能是：

```
CO_OK
CO_ERR_TX_BUSY
CO_ERR_TX_FAILED
CO_ERR_ARGUMENT
...
```

其中：

```
CO_OK
```

只表示发送回调接受了这帧，或者传输层接受了提交；不表示 CAN 总线已经完成 ACK。



#### 7. 只有发送成功才切换状态

```c
if (status == CO_OK)
    change_state(nmt, CO_NMT_PRE_OPERATIONAL);
```

这是这个函数最重要的设计点。

假设发送回调返回：

```
CO_ERR_TX_BUSY
```

执行过程：

```
组装 Boot-up
    ↓
co_send()
    ↓
发送忙
    ↓
返回 CO_ERR_TX_BUSY
    ↓
节点仍然是 Initialization
```

不会进入 `Pre-operational`。

只有当：

```
status == CO_OK
```

才执行：

```
change_state(nmt, CO_NMT_PRE_OPERATIONAL);
```

这会：

```
my_node.state
    Initialization
        ↓
    Pre-operational
```

如果设置了状态通知回调，还会调用：

```
notify(&my_app, CO_NMT_PRE_OPERATIONAL);
```

所以最终过程是：

```
发送回调接受 Boot-up
        │
        ▼
节点状态切换到 Pre-operational
        │
        ▼
通知外部应用
```

这样避免出现不一致：

```
Boot-up 没有成功提交
但节点却宣称自己已经完成启动
```



#### 8. 最后返回发送结果

```
return status;
```

函数返回的是 `co_send()` 的结果：

```
发送成功       → CO_OK
发送忙         → CO_ERR_TX_BUSY
发送失败       → CO_ERR_TX_FAILED
上下文错误     → 对应错误值
已经不是初始化 → CO_IGNORED
```

状态变化的条件只有一个：

```
status == CO_OK
```



#### 9. 完整执行示例

初始状态：

```
my_node.node_id = 1;
my_node.state = CO_NMT_INITIALIZATION;
```

调用：

```
co_status_t result = co_nmt_bootup(&my_nmt);
```

函数执行：

```
① validate(&my_nmt)
   检查通过

② 检查状态
   当前是 Initialization，继续

③ frame.id = 0x700 + 1
   frame.id = 0x701

④ frame.dlc = 1
   frame.data[0] = 0x00

⑤ co_send(&my_node, &frame)
   调用发送回调

⑥ 返回 CO_OK

⑦ change_state(&my_nmt, Pre-operational)

⑧ 返回 CO_OK
```

最后：

```
总线上：
701 [00]

my_node.state：
Initialization → Pre-operational
```

如果第五步返回发送忙：

```
总线上：
没有成功提交的 Boot-up

my_node.state：
仍然是 Initialization

函数返回：
CO_ERR_TX_BUSY
```

之后可以重新调用：

```
co_nmt_bootup(&my_nmt);
```

直到发送回调接受为止。

可以把这个函数记成：

> **确认节点还在初始化状态，发送一次 `0x700 + Node-ID`、数据为 `00` 的 Boot-up；只有发送成功后，才把节点推进到 Pre-operational。**





## 5：co_nmt_receive（）

```c
co_status_t co_nmt_receive(co_nmt_t *nmt, const can_frame_t *frame)
{
    co_rx_kind_t kind;
    co_status_t status = validate(nmt);
    if (status != CO_OK)
        return status;
    status = co_classify_rx(nmt->node, frame, &kind);
    if (status != CO_OK)
        return status;
    if (kind != CO_RX_NMT || nmt->node->state == CO_NMT_INITIALIZATION)
        return CO_IGNORED;
    switch (frame->data[0]) {
    case CO_NMT_START: change_state(nmt, CO_NMT_OPERATIONAL); break;
    case CO_NMT_STOP: change_state(nmt, CO_NMT_STOPPED); break;
    case CO_NMT_ENTER_PREOP: change_state(nmt, CO_NMT_PRE_OPERATIONAL); break;
    case CO_NMT_RESET_NODE:
    case CO_NMT_RESET_COMMUNICATION:
        /* Apply safe output/service notification before restoring parameters. */
        change_state(nmt, CO_NMT_INITIALIZATION);
        status = co_device_od_reset(nmt->device,
            (uint8_t)(frame->data[0] == CO_NMT_RESET_COMMUNICATION));
        if (status != CO_OK)
            return status;
        return co_nmt_bootup(nmt);
    default: return CO_IGNORED;
    }
    return CO_OK;
}
```

这个函数是阶段 3 的核心入口：**接收一帧 CAN 报文，确认它是不是发给本节点的 NMT 命令，然后根据命令切换状态或执行复位**

它接收两个指针：

```c
nmt
    NMT模块上下文，里面有节点、对象字典和通知回调

frame
    收到的CAN帧，只读取，不修改
```

可以用这组实际变量理解：

```c
co_context_t my_node = {0};
co_device_od_t my_device = {0};
co_nmt_t my_nmt = {0};
can_frame_t received = {0};
```

假设：

```
my_nmt.node = &my_node;
my_nmt.device = &my_device;
```

调用：

```
co_nmt_receive(&my_nmt, &received);
```

函数内部的：

```
nmt == &my_nmt
frame == &received
```



#### 1. 准备两个局部变量

```c
co_rx_kind_t kind;
co_status_t status = validate(nmt);
```

`kind` 用来保存阶段 1 分类器的结果：

```c
CO_RX_NMT
CO_RX_SDO
CO_RX_RPDO1
CO_RX_NONE
```

`status` 保存每一步函数的返回结果。

第一步调用：

```
status = validate(nmt);
```

它会确认：

```c
NMT上下文有效
节点上下文存在
发送回调存在
设备对象表有效
Node-ID合法
当前状态值合法
```

如果检查失败：

```c
if (status != CO_OK)
    return status;
```

函数立即返回对应错误，例如：

```c
CO_ERR_ARGUMENT
CO_ERR_NODE_ID
```

此时还没有读取接收帧，也没有改变节点状态。





#### 2. 使用阶段 1 的分类器判断报文类型

```c
status = co_classify_rx(nmt->node, frame, &kind);
```

这里传入：

```
nmt->node
```

实际就是：

```
&my_node
```

传入：

```
frame
```

实际就是：

```
&received
```

传入：

```
&kind
```

是为了让 `co_classify_rx()` 把分类结果写回来。

执行前：

```
kind：未初始化，不能直接读取
```

执行后，例如收到：

```
CAN-ID = 0x000
DLC    = 2
DATA   = 01 01
```

分类器会写入：

```
kind = CO_RX_NMT;
```

整个关系是：

```c
co_nmt_receive()
        │
        ▼
co_classify_rx()
        │
        ├── 检查CAN帧格式
        ├── 判断CAN-ID
        ├── 检查NMT目标Node-ID
        └── 把结果写入kind
```

如果收到的是：

```
CAN-ID = 0x601
```

分类结果可能是：

```
kind = CO_RX_SDO;
```

如果是无关报文，分类器返回：

```
CO_IGNORED
```

此时当前函数同样直接返回：

```
if (status != CO_OK)
    return status;
```

所以：

```
无关报文 → CO_IGNORED
```

不会进入后面的 NMT 命令处理。





#### 3. 确认它确实是 NMT，而且节点不是初始化中

```
if (kind != CO_RX_NMT ||
    nmt->node->state == CO_NMT_INITIALIZATION)
    return CO_IGNORED;
```

这有两个条件，只要一个成立就忽略。



###### 条件一：不是 NMT

```
kind != CO_RX_NMT
```

例如收到 SDO：

```
kind = CO_RX_SDO
```

那么：

```
kind != CO_RX_NMT
```

成立，直接返回：

```
CO_IGNORED
```

NMT 模块不处理 SDO，后续应该由 SDO 模块处理。



###### 条件二：节点仍处于 Initialization

```
nmt->node->state == CO_NMT_INITIALIZATION
```

即使收到的是目标正确的 NMT 帧，如果节点还没有完成 Boot-up，也会忽略：

```
Initialization状态
    └── 不执行Start、Stop、Pre-op等NMT命令
```

启动流程必须先完成：

```
co_nmt_bootup(&my_nmt);
```

成功后：

```
my_node.state == CO_NMT_PRE_OPERATIONAL
```

此时才会处理主站的 NMT 命令。

这个设计防止节点还没完成自身初始化，就接受外部状态控制。



#### 4. 根据 `frame->data[0]` 读取命令

经过前面的分类后，已经确认：

```c
这是本节点或广播的NMT帧
DLC == 2
```

因此访问：

```
frame->data[0]
```

是安全的。

NMT 报文格式是：

```
CAN-ID = 0x000
DLC    = 2
DATA[0] = 命令
DATA[1] = 目标Node-ID
```

所以：

```
frame->data[0]
```

表示命令字节。

函数用 `switch` 判断它：

```
switch (frame->data[0]) {
```



#### 5. Start 命令

```c
case CO_NMT_START:
    change_state(nmt, CO_NMT_OPERATIONAL);
    break;
```

`CO_NMT_START` 的值是：

```
0x01
```

收到：

```
CAN-ID = 0x000
DATA   = 01 01
```

执行：

```
change_state(nmt, CO_NMT_OPERATIONAL);
```

实际效果：

```
my_node.state
    Pre-operational
        ↓
    Operational
```

如果当前已经是 `Operational`，`change_state()` 内部发现状态相同，就直接返回，不重复通知。

此命令成功处理后，跳出 `switch`，最后执行：

```
return CO_OK;
```

注意：

```
NMT Start 本身没有响应帧
```

它是主站发出的管理命令，节点只改变自身状态。



#### 6. Stop 命令

```c
case CO_NMT_STOP:
    change_state(nmt, CO_NMT_STOPPED);
    break;
```

`CO_NMT_STOP` 的值是：

```
0x02
```

例如：

```
DATA = 02 01
```

状态变化：

```
Operational
    ↓ Stop
Stopped
```

执行后：

```
my_node.state == CO_NMT_STOPPED
```

如果设置了通知回调，还会通知：

```
notify(user, CO_NMT_STOPPED);
```

后续 PDO、SDO 等服务是否允许工作，由各自服务模块根据状态判断。当前 NMT 函数只负责修改状态和发通知。



#### 7. Enter Pre-operational 命令

```c
case CO_NMT_ENTER_PREOP:
    change_state(nmt, CO_NMT_PRE_OPERATIONAL);
    break;
```

`CO_NMT_ENTER_PREOP` 的值是：

```
0x80
```

例如：

```
DATA = 80 01
```

执行：

```c
Operational 或 Stopped
        ↓
Pre-operational
```

如果原本已经是 `Pre-operational`，则不会重复通知。





#### 8. 两种复位命令共用一个分支

```c
case CO_NMT_RESET_NODE:
case CO_NMT_RESET_COMMUNICATION:
```

这里没有给第一个 `case` 写 `break`，所以两个命令会进入同一段代码：

```
case CO_NMT_RESET_NODE:
case CO_NMT_RESET_COMMUNICATION:
    ...
```

这表示：

```
0x81 Reset Node
0x82 Reset Communication
```

都要执行共同的复位流程。



#### 9. 先切回 Initialization 并通知应用

```
change_state(nmt, CO_NMT_INITIALIZATION);
```

假设当前：

```
my_node.state = CO_NMT_OPERATIONAL;
```

收到复位后：

```
my_node.state
    Operational
        ↓
    Initialization
```

如果状态发生变化，`change_state()` 会调用通知回调：

```
notify(user, CO_NMT_INITIALIZATION);
```

这个通知的用途包括：

```
应用层设置安全输出
清理Heartbeat计时
清理SDO/PDO服务状态
清空待处理通信队列
准备重新启动
```

注释中的：

```
/* Apply safe output/service notification before restoring parameters. */
```

意思是：

> 在恢复对象参数之前，先通知外部应用进入初始化状态。

这样应用可以先处理安全动作，再进行参数恢复。



#### 10. 根据命令选择复位范围

```c
status = co_device_od_reset(
    nmt->device,
    (uint8_t)(frame->data[0] == CO_NMT_RESET_COMMUNICATION)
);
```

这一句比较集中，我们拆开看。

先看比较：

```
frame->data[0] == CO_NMT_RESET_COMMUNICATION
```

如果是通信复位：

```
0x82 == 0x82
结果：1
```

如果是节点复位：

```
0x81 == 0x82
结果：0
```

再强制转换为：

```
(uint8_t)
```

于是第二个参数只有两种：

```
Reset Communication → 1
Reset Node           → 0
```

整个调用可以还原为：

```c
co_device_od_reset(nmt->device, 1u);
```

或：

```c
co_device_od_reset(nmt->device, 0u);
```

这两个参数的意义是：

```
1：只恢复通信区可变对象
0：恢复全部可变对象
```

例如：

```c
复位前：
1017:00 = 500
1801:05 = 100
6423:00 = 1
6200:01 = 15
```

通信复位：

```c
1017:00  → 1000
1801:05  → 0
6423:00  → 保留1
6200:01  → 保留15
```

节点复位：

```c
1017:00  → 1000
1801:05  → 0
6423:00  → 0
6200:01  → 0
```

如果对象复位失败：

```c
if (status != CO_OK)
    return status;
```

函数就直接返回错误，不再发送 Boot-up。

不过按照当前阶段 2 对象表和实现，复位接口正常情况下应返回：

```
CO_OK
```



#### 11. 复位后重新发送 Boot-up

```
return co_nmt_bootup(nmt);
```

注意，这里是直接 `return`，所以它的返回值就是 `co_nmt_bootup()` 的返回值。

执行流程：

```
复位对象数据
        │
        ▼
节点当前状态仍为 Initialization
        │
        ▼
调用 co_nmt_bootup()
        │
        ├── 组装 0x700 + Node-ID
        ├── DATA[0] = 0x00
        ├── 调用发送回调
        └── 发送成功后进入 Pre-operational
```

例如 Node-ID=1：

```
CAN-ID = 0x701
DLC    = 1
DATA   = 00
```

如果发送成功：

```
Initialization
    ↓ Boot-up
Pre-operational
```

如果发送回调返回忙：

```
Reset流程已经把节点切到Initialization
但Boot-up没有发送成功
节点保持Initialization
函数返回CO_ERR_TX_BUSY
```

后续调用者可以再次调用：

```
co_nmt_bootup(&my_nmt);
```

进行重试。





#### 12. 未知命令

```c
default:
    return CO_IGNORED;
```

例如：

```c
DATA = 03 01
DATA = 7F 01
DATA = FF 01
```

这些不是当前支持的 NMT 命令。

函数会：

```c
不改变状态
不调用状态变化通知
不发送响应
返回 CO_IGNORED
```

因此未知命令不会被当成错误处理，也不会影响节点当前状态。



#### 13. 普通状态命令最后返回成功

```
return CO_OK;
```

这个 `return` 对应的是前三个普通命令：

```c
CO_NMT_START
CO_NMT_STOP
CO_NMT_ENTER_PREOP
```

它们调用 `change_state()` 后跳出 `switch`，最后返回 `CO_OK`。

而两种复位命令已经使用：

```
return co_nmt_bootup(nmt);
```

因此不会走到函数末尾。



#### 14. 完整命令流

###### Start

```c
收到 000 [01 01]
        │
        ▼
validate(nmt)
        │
        ▼
co_classify_rx()
        │
        ▼
kind = CO_RX_NMT
        │
        ▼
frame.data[0] = 0x01
        │
        ▼
change_state(..., OPERATIONAL)
        │
        ▼
返回 CO_OK
```

###### Stop

```c
收到 000 [02 01]
        │
        ▼
change_state(..., STOPPED)
        │
        ▼
返回 CO_OK
```

###### Reset Communication

```c
收到 000 [82 01]
        │
        ▼
切换到 Initialization
        │
        ▼
通知应用进入 Initialization
        │
        ▼
恢复通信区对象
        │
        ▼
发送 701 [00]
        │
        ▼
进入 Pre-operational
```

###### Reset Node

```c
收到 000 [81 01]
        │
        ▼
切换到 Initialization
        │
        ▼
通知应用进入 Initialization
        │
        ▼
恢复全部可变对象
        │
        ▼
发送 701 [00]
        │
        ▼
进入 Pre-operational
```



#### 15. 这个函数负责什么、不负责什么

`co_nmt_receive()` 负责：

```c
接收并过滤NMT帧
识别五种NMT命令
切换节点状态
处理两种复位范围
触发Boot-up
返回处理结果
```

它不负责：

```c
发送Heartbeat
处理SDO
处理PDO
操作GPIO
执行FreeRTOS任务
发送EMCY
```

这些属于后续模块或应用层。

可以把整个函数记成：

> **先验证上下文，再让阶段 1 分类器确认报文是否为本节点 NMT；确认后按命令切换状态，复位命令则恢复相应对象范围并重新发送 Boot-up。**



## 6：通信复位和节点复位

这两种复位的区别是：**通信复位只重新准备 CANopen 通信部分；节点复位还会把应用部分恢复到初始状态。**

它们都用于让节点重新开始启动流程，最后重新发送 Boot-up，进入 `Pre-operational`，等待主站配置和启动。

用我们项目中四个具体对象来看：

| 对象        | 保存什么         | 属于哪一部分 |
| ----------- | ---------------- | ------------ |
| `0x1017:00` | 心跳周期         | 通信参数     |
| `0x1801:05` | TPDO2 事件周期   | 通信参数     |
| `0x6423:00` | AI 变化触发开关  | 应用参数     |
| `0x6200:01` | 四路 DO 输出命令 | 应用数据     |

假设主站已经把它们设置成：

```
心跳周期        = 500 ms
TPDO2事件周期   = 100 ms
AI变化触发开关  = 1
DO命令          = 15（四路都要求开启）
```



#### **① Reset Communication：通信复位，命令 `0x82`**

主站对节点 1 发送：

```
CAN-ID = 0x000
DATA   = 82 01
```

意思是：

> 重新初始化你的通信部分，保留应用部分的数据。

在当前代码中，结果是：

```
通信部分恢复默认：
    心跳周期        500 → 1000 ms
    TPDO2事件周期   100 → 0

应用部分保留：
    AI变化触发开关    1 → 1
    DO命令          15 → 15
```

整个过程：

```
收到通信复位
    ↓
进入 Initialization，通知应用
    ↓
恢复通信区可变对象的默认值
    ↓
重新提交 Boot-up：701 [00]
    ↓ 提交成功
进入 Pre-operational
```

**它的用途是：重新建立通信配置，而不把应用配置一起清掉。** 例如主站希望重新配置心跳、PDO 通信周期，再让节点加入运行。

这里“重新初始化通信”不代表当前代码会重新初始化 STM32 CAN 外设；现在实现的是纯 C 协议层行为，硬件适配以后再接入。



#### **② Reset Node：节点复位，命令 `0x81`**

主站发送：

```
CAN-ID = 0x000
DATA   = 81 01
```

意思是：

> 把应用部分和通信部分都重新准备好，让节点从初始状态开始。

当前代码中的结果是：

```
通信部分恢复默认：
    心跳周期        500 → 1000 ms
    TPDO2事件周期   100 → 0

应用部分也恢复默认：
    AI变化触发开关    1 → 0
    DO命令          15 → 0
```

之后同样发送 Boot-up，进入 `Pre-operational`。

**它的用途是：需要连应用状态一起重新开始时，执行范围更大的恢复。** 例如一次实验结束后，想清除之前写入的输出命令和应用配置，再从默认配置重新测试。

但在我们当前实现中：

```
Reset Node
    = 恢复对象表中的全部可变数据
      + 重新执行协议启动流程
```

**它没有调用 MCU 的硬件复位，也没有执行 `NVIC_SystemReset()`。** 设备身份和固定配置会保留。

还有一个容易混淆的地方：**通信复位保留 `DO命令=15`，不代表四路物理输出在复位期间仍然保持开启。**

```
对象表里的DO命令
    保存“应用之前收到的输出要求”

真实GPIO输出
    还要服从当前节点状态和安全输出策略
```

所以通信复位时：

```
DO对象可以保留15
    +
节点进入Initialization
    ↓
应用应通过状态通知执行安全输出
```

目前 NMT 代码只发出通知，真正把 GPIO 置为安全电平，要由后续应用和硬件层完成。

对应到刚才的代码，就是：

```
co_device_od_reset(nmt->device, 1u);  // 通信复位：保留应用数据

co_device_od_reset(nmt->device, 0u);  // 节点复位：应用数据也恢复默认
```

两者都不会直接恢复到 `Operational`。**复位后先回到 `Pre-operational`，主站需要再发送 Start，节点才重新进入运行状态。**