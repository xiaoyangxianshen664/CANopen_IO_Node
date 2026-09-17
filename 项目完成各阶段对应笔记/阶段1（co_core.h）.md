# 阶段 1：`co_core.h` 接口与发送调用链



## 一、总览

| 函数 | 传入什么 | 完成什么 |
|---|---|---|
| `co_init()` | 节点地址、节点号、发送函数地址、辅助数据地址 | 保存配置，设置初始状态，不发送报文 |
| `co_frame_validate()` | CAN 帧地址 | 检查基本格式，不发送 |
| `co_send()` | 节点上下文地址、待发送帧地址 | 校验后调用上下文中保存的发送函数 |
| `co_classify_rx()` | 节点地址、接收帧地址、分类结果地址 | 判断 NMT/SDO/RPDO1 或忽略，不执行具体业务 |

```text
co_types.h：定义类型
    ↓
co_core.h：声明接口
    ↓
co_core.c：实现接口
    ↓
test_co_core.c：PC 测试调用

发送链路：
co_init(&ctx, 1, fake_tx, &bus)
    ↓ 保存 ctx.tx = fake_tx，ctx.tx_user = &bus
co_send(&ctx, &frame)
    ↓ 检查上下文和报文
ctx->tx(ctx->tx_user, frame)
    ↓ 运行时等价于
fake_tx(&bus, &frame)
```

`co_send()` 不在源码中写死 `fake_tx()`；它通过函数指针成员 `ctx->tx` 间接调用。PC 测试绑定 `fake_tx`，以后 STM32 绑定 `stm32_can_send`，接口调用方式不变。

---
这一份是 **`co_core.h`：声明阶段 1 提供的四个函数接口**。

之前的 `co_types.h` 定义“数据怎么保存”，这里说明“可以调用哪些函数来处理这些数据”。`co_init()` 保存发送回调，`co_send()` 后续通过 `ctx->tx(ctx->tx_user, frame)` 间接调用已保存的回调。具体函数体在 `co_core.c` 中。







### **① 开头：头文件保护与类型引用**

```
#ifndef CO_CORE_H
#define CO_CORE_H

#include "co_types.h"
```

前两行与末尾的 `#endif` 配对，防止头文件被重复包含。

```
#include "co_types.h"
```

让当前文件能够使用之前定义的类型，例如：

```
co_status_t
co_context_t
can_frame_t
co_tx_fn
co_rx_kind_t
```

否则编译器看见这些名字，就不知道它们代表什么。





### **② `co_init()`：初始化一个节点**

```
co_status_t co_init(co_context_t *ctx,
                    uint8_t node_id,
                    co_tx_fn tx,
                    void *tx_user);
```

它负责把“节点编号、初始状态、发送函数及辅助数据”填入节点上下文。

| 部分                | 含义                                         |
| ------------------- | -------------------------------------------- |
| `co_status_t`       | 返回初始化成功或失败的结果                   |
| `co_init`           | 函数名                                       |
| `co_context_t *ctx` | 要初始化的节点变量地址，函数会修改其成员     |
| `uint8_t node_id`   | 本节点编号，要求为 1～127                    |
| `co_tx_fn tx`       | 封装好的发送函数地址                         |
| `void *tx_user`     | 以后调用发送函数时，需要传给它的辅助数据地址 |

```c
我们现在只看这四个参数：
co_status_t co_init(
    co_context_t *ctx,
    uint8_t node_id,
    co_tx_fn tx,
    void *tx_user
);
你对第一个参数的理解正确，但第二个判断需要纠正：tx 不是结构体，它是函数指针。
参数					传入什么												用来做什么
ctx				节点结构体变量的地址									 告诉函数“初始化哪一个节点变量”
node_id			一个整数，例如 1													设置节点编号
tx			  封装好的发送函数的地址									 设置“以后调用哪个函数发送”
tx_user			发送函数需要的辅助数据地址								给发送函数使用；不需要时可以传 NULL

 
typedef struct {
    uint8_t node_id; /* 本节点编号，当前项目使用 1 */
    co_nmt_state_t state; /* 节点当前 NMT 状态，阶段 1 只设置初值 */
    co_tx_fn tx; /* 保存发送函数地址，使协议层不依赖具体硬件 */
    void *tx_user; /* 保存传输层私有数据地址，发送时原样传给回调 */
} co_context_t;   
    
创建一个co_context_t 结构体变量 co_context_t node
    

先把第四个参数设为 NULL，就容易理解了。
假设已经实现了一个发送函数 my_send，而且它不需要辅助数据，那么调用可以写成：
co_context_t node = {0};

co_init(&node, 1, my_send, NULL);
逐个读就是：
&node    → 请初始化 node 这个结构体变量
1        → 节点编号设为 1
my_send  → 以后发送报文时，调用 my_send
NULL     → 这个发送函数不需要额外的辅助数据

初始化函数做的事情，相当于：
node.node_id = 1;
node.state = CO_NMT_INITIALIZATION;
node.tx = my_send;
node.tx_user = NULL;
这里只是把信息存好，还没有发送报文。
至于：
void *tx_user
它声明的是一个通用数据指针参数，* 在这里表示“指针”，不是要求你传入一个叫 *tx_user 的东西。现在先理解为：留给发送函数的辅助参数，不需要就传 NULL。
```

你先抓住这条链路：

```c
co_init 的第三个参数 my_send
    → 保存到 node.tx
    → 以后决定调用哪个函数

co_init 的第四个参数 &hcan1
    → 保存到 node.tx_user
    → 以后作为 my_send 的第一个参数 user

待发送报文的地址 &frame
    → 发送时才提供
    → 作为 my_send 的第二个参数 frame
    
    frame是can_frame_t frame;所创建的一个变量
 typedef struct {
    uint32_t id; /* 实际 CAN-ID，不是带配置标志位的对象字典 COB-ID 参数 */
    uint8_t dlc; /* 有效数据长度，允许 0~8 */
    uint8_t data[CO_CAN_DATA_MAX]; /* 数据缓冲区；只有前 dlc 字节属于有效载荷 */
    uint8_t is_extended; /* 非零表示扩展帧，本项目拒绝 */
    uint8_t is_remote; /* 非零表示远程帧，本项目拒绝 */
    uint8_t is_fd; /* 非零表示 CAN FD 帧，本项目拒绝 */
} can_frame_t;
这里创建了一个名叫 frame 的变量，它的类型是 can_frame_t。
发送时传入它的地址：
my_send(&hcan1, &frame);
于是：
&hcan1 → my_send 的 user
&frame → my_send 的 frame
至于 frame 内部各成员的值，必须由我们提前填写。比如发送 Node-ID=1 的 Operational Heartbeat：
can_frame_t frame = {
    .id = 0x701,
    .dlc = 1,
    .data = {0x05},
    .is_extended = 0,
    .is_remote = 0,
    .is_fd = 0
};
它对应：
frame.id           = 0x701
frame.dlc          = 1
frame.data[0]      = 0x05
frame.is_extended  = 0
frame.is_remote    = 0
frame.is_fd        = 0
也可以先创建，再逐个赋值：
can_frame_t frame = {0};

frame.id = 0x701;
frame.dlc = 1;
frame.data[0] = 0x05;
这里的：
can_frame_t frame = {0};
会先把所有成员清零，避免成员中存在随机值。
所以完整链路是：
can_frame_t frame = {0};  /* 创建并清零一帧报文 */
frame.id = 0x701;         /* 设置 CAN-ID */
frame.dlc = 1;            /* 设置有效数据长度 */
frame.data[0] = 0x05;     /* 设置数据 */

my_send(&hcan1, &frame);   /* 发送这帧报文 */
封装函数 my_send() 通过：
frame->id
frame->dlc
frame->data
读取这帧报文的具体内容。
所以之前示例中没有马上贴出 frame 的内部值，是因为报文内容取决于当前要发送的具体 CANopen 服务：Heartbeat、TPDO、SDO 响应的 id、dlc 和 data 都不同
```

因此`co_init()` 是提前把“发送函数”和“它要使用的句柄”配好；真正发送时调用函数my_send（），再给它一帧报文，通过创建can_frame_t frame变量，

随后赋值，然后传入地址即可。







### **③ `co_frame_validate()`：检查一帧报文的基本格式**

```
co_status_t co_frame_validate(const can_frame_t *frame);
```

它只接收一个参数：**待检查报文的地址**。

```
const can_frame_t *frame
```

表示通过这个指针读取报文，不修改报文。

例如：

```c
can_frame_t frame = {
    .id = 0x701,
    .dlc = 1,
    .data = {0x05}
};

co_status_t result = co_frame_validate(&frame);
```

没有显式填写的其他成员会被零初始化，所以这是一帧标准、经典 CAN 数据帧。

这个函数主要检查：

- 报文指针是否为 `NULL`。
- 是否为不支持的扩展帧、远程帧或 CAN FD 帧。
- CAN-ID 是否超过 `0x7FF`。
- DLC 是否超过 `8`。

这帧基本格式正确，就返回：

```
CO_OK
```

如果改成：

```
frame.dlc = 9;
```

就会返回：

```
CO_ERR_DLC
```

注意，这里只检查**基础格式**。例如 SDO 必须 DLC=8 的服务要求，由后续 SDO 模块检查。





### **④ `co_send()`：检查之后，调用发送回调**

`co_send()` 的核心作用是：

> **使用节点上下文中已经配置好的发送方式，把指定的 CAN 报文提交出去。**

它不是为了读取所有成员，而是主要读取其中两个成员：

```
ctx->tx
ctx->tx_user
```

------



#### 1：先看调用方准备的数据

```
co_context_t ctx = {0};
can_frame_t frame = {0};
```

调用初始化：

```
co_init(&ctx, 1, my_send, &hcan1);
```

初始化后，`ctx` 中大致是：

```
ctx.node_id = 1
ctx.state = CO_NMT_INITIALIZATION
ctx.tx = my_send
ctx.tx_user = &hcan1
```

然后准备待发送报文：

```
frame.id = 0x701;
frame.dlc = 1;
frame.data[0] = 0x05;
```

最后调用：

```
co_send(&ctx, &frame);
```

------



#### 2：`co_send()` 内部真正要做什么

可以用简化代码表示：

```
co_status_t co_send(const co_context_t *ctx,
                    const can_frame_t *frame)
{
    /* 先检查 ctx 是否有效 */
    /* 再检查 frame 格式是否正确 */

    return ctx->tx(ctx->tx_user, frame);
}
```

把 `ctx` 中的内容代入：

```
return my_send(&hcan1, frame); /* co_send 内的 frame 已经是指针 */
```

所以它的真实作用就是：

```
通过 ctx 找到：
    tx       → 具体调用哪个发送函数
    tx_user  → 发送函数需要使用哪个 CAN 资源

再把 frame 交给这个发送函数
```

------





#### 3：`co_context_t` 四个成员在 `co_send()` 中的作用

| 成员      | `co_send()` 是否直接使用 | 作用                                                |
| --------- | ------------------------ | --------------------------------------------------- |
| `node_id` | 当前阶段不直接用于发送   | 保存本节点编号，后续生成 SDO、PDO、Heartbeat CAN-ID |
| `state`   | 当前阶段不直接用于发送   | 保存 NMT 工作状态，后续限制哪些报文可以发送         |
| `tx`      | **直接使用**             | 找到具体发送函数，例如 `my_send`                    |
| `tx_user` | **直接使用**             | 把 CAN1 句柄、发送队列等辅助资源传给发送函数        |

所以你看到 `co_send()` 访问结构体，不是为了访问所有成员，而是为了取出：

```
ctx->tx
ctx->tx_user
```

------



#### 4：为什么不直接调用 `my_send()`？

当然可以直接写：

```
my_send(&hcan1, &frame);
```

但这样协议层就必须知道：

```
发送函数叫什么
使用 CAN1 还是 CAN2
传输资源在哪里
```

使用 `co_send()` 后，协议层只需要知道：

```
co_send(&ctx, &frame);
```

不同环境可以配置不同发送方式：

```
/* PC 测试 */
co_init(&ctx, 1, fake_tx, &fake_bus);

/* STM32 硬件 */
co_init(&ctx, 1, stm32_can_send, &hcan1);
```

上层协议代码完全不用改变。

------

#### 5：最后总结

```
co_context_t
    保存节点配置和发送配置

co_send()
    读取 ctx 中的 tx 和 tx_user
    检查 frame
    调用实际发送函数

frame
    保存真正要发送的 CAN-ID、DLC 和数据
```

因此：

> `co_send()` 是一个统一的、与硬件无关的发送入口；它通过 `co_context_t` 找到实际发送函数和发送资源，再把 `can_frame_t` 报文交出去。



#### 6：对应的.c函数逻辑

```c
实际代码在 canopen/src/co_core.c 中：
co_status_t co_send(const co_context_t *ctx,
                    const can_frame_t *frame)
{
    co_status_t status = co_context_validate(ctx);

    if (status != CO_OK) {
        return status;
    }

    status = co_frame_validate(frame);

    if (status != CO_OK) {
        return status;
    }

    return ctx->tx(ctx->tx_user, frame);
}
关键就是最后一行：
return ctx->tx(ctx->tx_user, frame);
它等价于：
return 保存的发送函数(保存的辅助地址, frame);
但是，ctx->tx 和 ctx->tx_user 是在哪里保存的？在 co_init()：
co_status_t co_init(co_context_t *ctx,
                    uint8_t node_id,
                    co_tx_fn tx,
                    void *tx_user)
{
    if (ctx == NULL || tx == NULL) {
        return CO_ERR_ARGUMENT;
    }

    if (node_id == 0u || node_id > CO_NODE_ID_MAX) {
        return CO_ERR_NODE_ID;
    }

    ctx->node_id = node_id;
    ctx->state = CO_NMT_INITIALIZATION;
    ctx->tx = tx;
    ctx->tx_user = tx_user;

    return CO_OK;
}
测试代码中这样调用：
fake_bus_t bus = {0};
co_context_t ctx = {0};

co_init(&ctx, 1, fake_tx, &bus);
执行完后，结构体中的内容相当于：
ctx.node_id  = 1
ctx.state    = CO_NMT_INITIALIZATION
ctx.tx       = fake_tx
ctx.tx_user  = &bus
之后准备报文：
can_frame_t frame = {0};

frame.id = 0x701;
frame.dlc = 1;
frame.data[0] = 0x05;
调用：
co_send(&ctx, &frame);
进入 co_send() 后：
ctx->tx(ctx->tx_user, frame)
根据 ctx 中保存的内容，实际展开为：
fake_tx(&bus, &frame);
fake_tx() 的实际代码是：
static co_status_t fake_tx(void *user,
                           const can_frame_t *frame)
{
    fake_bus_t *bus = user;

    ++bus->calls;
    bus->frame = *frame;

    return bus->result;
}
完整调用链就是：
co_init(&ctx, 1, fake_tx, &bus)
    ↓
ctx->tx = fake_tx
ctx->tx_user = &bus
    ↓
co_send(&ctx, &frame)
    ↓
ctx->tx(ctx->tx_user, frame)
    ↓
fake_tx(&bus, frame)
要注意：
ctx->tx = tx;
是保存函数地址。
ctx->tx(ctx->tx_user, frame);
才是通过保存的函数地址调用函数。
所以 co_send() 的源代码里没有直接写：
fake_tx(...);
它通过 ctx->tx 间接连接到 fake_tx()。如果初始化时换成：
co_init(&ctx, 1, stm32_can_send, &hcan1);
那么同一个 co_send() 就会实际调用：
stm32_can_send(&hcan1, &frame);
```



#### 7：PC端和STM硬件实机端的区分

```c
是我之前突然换成了 fake_tx、bus，让你以为又出现了一套新东西。它们其实就是 PC 测试中的“发送函数”和“发送函数需要的数据”。
你已经理解的示例是：
my_send(&hcan1, &frame);
笔记中换成了：
fake_tx(&bus, &frame);
两者对应关系是：
STM32 				发送示例							PC 测试代码	作用
my_send				 fake_tx						    被调用的发送函数
&hcan1			     	&bus						这个函数需要使用的数据地址
&frame				 &frame								待提交报文的地址


fake_tx(&bus, &frame) 的意思就是：
调用模拟发送函数 fake_tx，让它把 frame 这帧报文记录到 bus 中。

它不会真的发送，只是方便在 PC 上检查报文内容。
这里的 bus 是测试记录，保存：
calls   → 发送函数被调用了几次
frame   → 最近一次提交的报文
result  → 预先设置的返回结果

    执行：
fake_tx(&bus, &frame);
函数内部就做三件事：
bus 的调用次数加 1;
把传入的报文复制到 bus 的记录中;
返回 bus 中预先设置的结果;
它为什么会被 co_send() 调用？因为测试代码提前配置了：
co_init(&ctx, 1, fake_tx, &bus);
所以调用链是：
先配置：
co_init(&ctx, 1, fake_tx, &bus);
                 ↓
ctx 保存 fake_tx 的地址和 bus 的地址

再发送：
co_send(&ctx, &frame);
                 ↓
调用之前配置的函数：
fake_tx(&bus, &frame);
co_send() 并不固定调用 fake_tx()。初始化时配置哪个发送函数，它就调用哪个。 你现在可以继续用已经理解的 my_send + &hcan1 来理解这条链路，等学测试文件时再看 fake_tx + &bus。
```





### **⑤ `co_classify_rx()`：判断收到的帧属于哪一类**

```c
co_status_t co_classify_rx(const co_context_t *ctx,
                           const can_frame_t *frame,
                           co_rx_kind_t *kind);
```

三个参数分别是：

| 参数    | 用途                                   |
| ------- | -------------------------------------- |
| `ctx`   | 提供本节点编号，判断帧是不是发给自己的 |
| `frame` | 刚收到的报文                           |
| `kind`  | 分类结果的输出地址，函数会往这里写结果 |

这里要特别注意：**它通过两种方式交回信息。**

```
co_rx_kind_t kind = CO_RX_NONE;

co_status_t result = co_classify_rx(&ctx, &frame, &kind);
```

- `result`：这次分类操作成功、忽略，还是发生错误？
- `kind`：如果分类成功，它属于 NMT、SDO 还是 RPDO1？

例如本节点编号为 1，收到：

```c
can_frame_t frame = {
    .id = 0x601,
    .dlc = 8,
    .data = {0x40, 0x17, 0x10, 0x00, 0, 0, 0, 0}
};
```

调用分类函数后：

```
result = CO_OK
kind   = CO_RX_SDO
```

表示：**识别到本节点的 SDO 请求。** 当前还不会读取对象字典或发送响应。

如果把 CAN-ID 改为 `0x602`，它是发给节点 2 的：

```
result = CO_IGNORED
kind   = CO_RX_NONE
```

对于 NMT，则先检查 DLC=2，再看 `data[1]` 是本节点编号还是广播编号 `0`。

**这四个函数可以这样记：**

| 函数                  | 作用                                       |
| --------------------- | ------------------------------------------ |
| `co_init()`           | 准备好一个节点，绑定发送方式               |
| `co_frame_validate()` | 检查一帧 CAN 报文的基本格式                |
| `co_send()`           | 校验后提交一帧报文                         |
| `co_classify_rx()`    | 判断接收帧是否属于本节点，以及属于哪类服务 |

这些声明末尾都是分号，没有函数体。下一步看 `co_core.c` 时，就能逐行看到它们怎么完成这些工作。