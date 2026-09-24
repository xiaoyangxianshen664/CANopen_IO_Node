# 阶段 1：纯 C 基础类型与 CAN 帧抽象





# 代码解析



## 1：宏定义

```c
#define CO_CAN_ID_MAX UINT32_C(0x7FF) 		/* 11 位标准 CAN-ID 的最大值；32 位类型用于检测越界输入 */
#define CO_CAN_DATA_MAX 8u 				   /* 经典 CAN 每帧最多 8 个数据字节 */
#define CO_NODE_ID_MAX 127u 			   /* 节点号上限；0 仅用于 NMT 广播目标 */
#define CO_COB_NMT UINT32_C(0x000)          /* NMT 固定 CAN-ID，目标节点在数据第 2 字节中 */
#define CO_COB_RPDO1_BASE UINT32_C(0x200)   /* RPDO1 基值，加节点号后得到接收输出命令的 CAN-ID */
#define CO_COB_SDO_RX_BASE UINT32_C(0x600)  /* SDO 请求基值；节点 1 接收 0x601 */
```



#### 1：CAN-ID 的最大值

```c
#define       CO_CAN_ID_MAX        UINT32_C(0x7FF)
先拆成三部分：
#define         // 定义宏
CO_CAN_ID_MAX    // 宏名称：CAN-ID 的最大值
UINT32_C(0x7FF)  // 无符号整数常量
以后代码中写：
if (frame->id > CO_CAN_ID_MAX)
就是在判断：
这个报文的 CAN-ID 是否超过标准帧允许的范围？

为什么最大值是 0x7FF？因为标准 CAN-ID 有 11 位：
二进制：111 1111 1111
十六进制：0x7FF
十进制：2047
所以合法范围是：
0x000 ～ 0x7FF
```

UINT32_C() 的作用：告诉编译器“请把 0x7FF 这个数当成 32 位无符号整数来看待”，这样在后面做比较时，类型匹配，不会出错。



为什么要用 32 位变量来装 11 位的数据？

```c
uint32_t id; 		                  // 用一个 32 位的“大碗”来装 ID
id = 收到的CAN_ID;                     // 比如对方发来了一个非法的 0x800

if (id > CO_CAN_ID_MAX) { 			  // 拿 0x800 和 0x7FF 比
    								// 0x800 比 0x7FF 大，判定为非法，拒绝！
}
```

如果反过来，你用 11 位（或更小）的变量来装：

```c
uint16_t id; 							 // 只用 11 位位域
id = 0x800;  							 // 0x800 是 1000 0000 0000，11 位装不下
									     // 结果：数据被截断，id 变成了 0x000
if (id > 0x7FF) { 
   										 // 0x000 不大于 0x7FF，代码以为它是合法的！非法数据就被放过去了。
}
```

1：UINT32_C() 本身不校验数据，它只是个“类型声明工具”，保证 0x7FF 是个 32 位数。

2：真正干活的是 `uint32_t id` 这个 32 位变量。因为它够宽，所以即使有人传了非法的 `0x800`、`0x900`，它也能原封不动地存下来，不会丢失信息

3：最后通过和 `CO_CAN_ID_MAX`（0x7FF）比较，就能轻松地发现“哦，这个数超范围了”，从而把非法 ID 拒之门外。

一句话概括：用大号容器（32位）装小号数据（11位ID），是为了保留完整的原始值，方便跟上限值（0x7FF）做对比，防止非法值因为“装不下”而被截断、伪装成合法值混进来。



#### 2：CAN 数据长度上限

```c
#define CO_CAN_DATA_MAX     8u
```

表示：

> 经典 CAN 一帧最多携带 8 个数据字节。

`8u` 中的 `u` 表示 unsigned，无符号整数常量，数值仍然是 8。

它不是“8 位”的意思。

后面的：

```
uint8_t data[CO_CAN_DATA_MAX];
```

相当于：

```
uint8_t data[8];
```

这是一个包含 8 个元素的数组，每个元素 1 字节。

注意，最多能放 8 字节，不代表每帧都必须发送 8 字节：

```c
RPDO1：DLC = 1
TPDO1：DLC = 1
TPDO2：DLC = 4
SDO：  DLC = 8
```





#### 3：节点编号上限

```
#define CO_NODE_ID_MAX 127u
```

表示本节点编号的合法范围上限是 127：

```
本节点可配置：1 ～ 127
当前项目使用：1
```

这里没有把当前节点“固定成 127”，只是定义上限。

`0` 在 NMT 命令的目标节点字段中表示广播：

```
目标节点 = 1：发给节点 1
目标节点 = 0：发给所有节点
```

所以初始化时不能把我们自己的 Node-ID 设置为 0。

还要区分：

| 名称    | 本项目示例 | 含义                   |
| ------- | ---------- | ---------------------- |
| Node-ID | `1`        | 节点编号               |
| CAN-ID  | `0x201`    | 一种具体通信报文的标识 |

**一个节点会使用多个 CAN-ID。**





#### 4：NMT 的固定 CAN-ID

```c
#define CO_COB_NMT UINT32_C(0x000)
```

表示：

> NMT 命令报文固定使用 CAN-ID `0x000`。

它不需要加上节点编号。目标节点在数据中：

```
CAN-ID = 0x000
DLC    = 2
DATA   = 01 01
         │  │
         │  └─ 目标节点：1
         └──── 命令：Start，进入 Operational
```

如果希望所有节点进入 Operational：

```c
CAN-ID = 0x000
DATA   = 01 00		//data[0]表命令，data[1]表示节点id
```

所以接收 NMT 时，需要先识别 `CAN-ID=0x000`，再检查 `data[1]` 是否为本节点或广播。



#### 5：RPDO1 的基础 CAN-ID

```
#define CO_COB_RPDO1_BASE UINT32_C(0x200)
```

`BASE` 表示**基值**，不是最终 CAN-ID。

在我们采用的预定义连接集中：

```
RPDO1 CAN-ID = 0x200 + Node-ID
```

当前节点编号为 1：

```
0x200 + 1 = 0x201
```

例如主站发送：

```
CAN-ID = 0x201
DLC    = 1
DATA   = 05
```

后续 PDO 模块会按映射将它解释为 DO 输出值。

`R` 是从**节点的视角**说的：这是节点接收的 PDO。因此方向是：

```
主站 → STM32 节点
```

当前阶段只识别这类帧，还不会操作 LED。





#### 6： SDO 请求的基础 CAN-ID

```
#define CO_COB_SDO_RX_BASE UINT32_C(0x600)
```

这里：

```
SDO：服务数据对象
RX：节点接收
BASE：基础值
```

因此：

```
节点接收 SDO 请求的 CAN-ID = 0x600 + Node-ID
```

对于节点 1：

```
主站 → 节点：0x601，SDO 请求
节点 → 主站：0x581，SDO 响应
```

当前这个宏只定义了**请求方向的基值**，所以是 `0x600`，不是 `0x580`。

例如后面看到：

```
frame->id == CO_COB_SDO_RX_BASE + ctx->node_id
```

当 `ctx->node_id` 为 1 时，就是判断：

```
frame->id == 0x601
```

也就是：**“这是不是发给我的 SDO 请求？”**

这 6 行只是定义后续代码要使用的常量，尚未接收、发送或处理任何报文。下一段 `co_status_t` 则用来表达：**调用一个函数以后，它成功了、忽略了报文，还是遇到了错误？**



#### 7：阶段1只定义接收方向的宏

因为当前这几行宏主要服务于**阶段 1 的接收入口和节点过滤**，所以只定义了：

- NMT：节点接收
- RPDO1：节点接收
- SDO RX：节点接收

TPDO 是 **节点发送给主站** 的报文，当前阶段还没有实现 PDO 发送逻辑，所以暂时没有定义对应宏。

按当前 Node-ID=1，TPDO 的 CAN-ID 是：

```c
#define CO_COB_TPDO1_BASE UINT32_C(0x180)
#define CO_COB_TPDO2_BASE UINT32_C(0x280)
```

计算：

```
TPDO1 = 0x180 + 1 = 0x181
TPDO2 = 0x280 + 1 = 0x281
```

完整方向是：

```
RPDO1：0x200 + 1 = 0x201，主站 → 节点
TPDO1：0x180 + 1 = 0x181，节点 → 主站
TPDO2：0x280 + 1 = 0x281，节点 → 主站
```

所以不是 TPDO 没有 CAN-ID，而是我们当前的 `co_types.h` 只先定义了**阶段 1 需要的接收 ID**。到了阶段 6 实现 PDO 时再增加 TPDO 宏也可以。

不过为了让阶段 0 的全部固定 PDO 关系更完整，现在就把这两个基础宏补进去也合理：

```
#define CO_COB_TPDO1_BASE UINT32_C(0x180)
#define CO_COB_TPDO2_BASE UINT32_C(0x280)
```

它们只定义常量，不会改变当前功能。





## 2：统一返回状态类型enum

```c
typedef enum
{
    CO_OK = 0,           /* 我处理了，而且成功 */
    CO_IGNORED,          /*不是错误，而是“这帧不是我的，我不处理 */
    CO_ERR_ARGUMENT,     /* 指针或必要回调无效，比如传进来的指针是 NULL */
    CO_ERR_NODE_ID,      /* 节点号不在 1~127 内 */
    CO_ERR_CAN_ID,       /* CAN-ID 超过 11 位范围 */
    CO_ERR_DLC,          /* CAN 帧数据长度，超出8字节 */
    CO_ERR_FRAME_TYPE,   /* 不支持扩展、远程或 CAN FD 帧 */
    CO_ERR_TX_BUSY,      /* 传输层暂忙，调用者决定后续处理 */
    CO_ERR_TX_FAILED,    /* 传输层发送失败 */
        
    /*对象字典错误*/
    CO_ERR_OD_NOT_FOUND, /* 对象字典中没有对应的 Index/Sub-index */
    CO_ERR_OD_READ_ONLY, /* 对象只读，拒绝写入 */
    CO_ERR_OD_LENGTH,    /* 对象数据长度不匹配，例如uint16_t，应该传2字节，实际填入形参4Byte */
    CO_ERR_OD_VALUE,     /* 对象值超出允许范围，比如数字输出窗口为4bit，输出数值范围为0~15 */
    CO_ERR_OD_CALLBACK,  /* 对象需要一个回调函数，但这个回调没有配置，也就是你没传递函数指针进去 */
    CO_ERR_OD_STATE      /* 当前 NMT 状态不允许修改对象 ，不同状态下，对象的访问权限可能不同 ，预操作可以改SDO*/
} co_status_t;
```











## 3：第二个枚举

```c
typedef enum {
    CO_NMT_INITIALIZATION = 0, 			/* 初始化状态，数值 0 也用于 Boot-up 通知 */
    CO_NMT_STOPPED = 4, 			    /* 停止状态，对应状态字节 0x04 */
    CO_NMT_OPERATIONAL = 5,			    /* 运行状态，对应状态字节 0x05 */
    CO_NMT_PRE_OPERATIONAL = 127 		/* 预运行状态，对应状态字节 0x7F */
} co_nmt_state_t;
```

这段定义的是 **NMT 状态类型**。NMT 是 CANopen 的网络管理，节点会处于不同工作状态。





## 4：CAN 报文结构体

```c
typedef struct {
    uint32_t id; 			       /* 实际 CAN-ID，不是带配置标志位的对象字典 COB-ID 参数 */
    uint8_t dlc; 				  /* 有效数据长度，允许 0~8 */
    uint8_t data[CO_CAN_DATA_MAX]; /* 数据缓冲区；只有前 dlc 字节属于有效载荷 */
    uint8_t is_extended; 		   /* 非零表示扩展帧，本项目拒绝 */
    uint8_t is_remote; 			  /* 非零表示远程帧，本项目拒绝 */
    uint8_t is_fd; 				 /* 非零表示 CAN FD 帧，本项目拒绝 */
} can_frame_t;

```

这段定义了项目中最核心的数据结构之一：**CAN 报文结构体**。

它把一帧 CAN 报文的关键信息集中放在一起。

### 1. `typedef struct`

```
typedef struct {
    ...
} can_frame_t;
```

`struct` 用来把多个不同类型的变量组合成一个整体。

如果没有 `typedef`，使用时要写：

```
struct can_frame frame;
```

现在通过 `typedef` 给它起了类型名：

```
can_frame_t frame;
```

例如：

```
can_frame_t tx_frame;
can_frame_t rx_frame;
```

分别表示发送帧和接收帧。



### 2. `uint32_t id`

```
uint32_t id;
```

表示 CAN-ID。

例如：

```c
0x000：NMT
0x181：TPDO1
0x201：RPDO1
0x601：SDO 请求
0x701：Heartbeat
```

为什么 CAN-ID 只有 11 位，却使用 `uint32_t`？

因为这样可以完整保存非法输入，方便检查：

```
if (frame->id > CO_CAN_ID_MAX) {
    return CO_ERR_CAN_ID;
}
```

例如收到：

```
id = 0x800
```

如果使用更小的类型，可能发生截断；使用 `uint32_t` 可以准确判断它超过了 `0x7FF`。

注释中特别说：

```
实际 CAN-ID，不是带配置标志位的对象字典 COB-ID 参数
```

这里的 `id` 必须是总线上真正使用的 CAN-ID，例如：

```
frame.id = 0x201;
```

不能把对象字典中可能带有有效标志位的配置值直接塞进来。

------



### 3. `uint8_t dlc`

```
uint8_t dlc;
```

DLC 是 **Data Length Code**，表示当前报文有几个有效数据字节。

经典 CAN 的范围是：

```
DLC = 0 ～ 8
```

在本项目中：

```
RPDO1：DLC = 1
TPDO1：DLC = 1
TPDO2：DLC = 4
SDO：  DLC = 8
```

例如：

```
frame.id = 0x201;
frame.dlc = 1;
frame.data[0] = 0x05;
```

表示：

```
CAN-ID = 0x201
有效数据长度 = 1
有效数据 = 05
```

虽然 `dlc` 用 `uint8_t`，可以保存 0～255，但协议校验会限制它不能大于 8字节。

------



### 4. `uint8_t data[CO_CAN_DATA_MAX]`

```
uint8_t data[CO_CAN_DATA_MAX];
```

由于：

```
#define CO_CAN_DATA_MAX 8u
```

所以等价于：

```
uint8_t data[8];
```

它提供 8 个字节的 CAN 数据缓冲区：

```
data[0]
data[1]
data[2]
data[3]
data[4]
data[5]
data[6]
data[7]
```

但是，真正有效的数据只看前 `dlc` 个字节。

例如：

```
frame.dlc = 1;
frame.data[0] = 0x05;
```

此时：

```
data[0] 有效
data[1]～data[7] 不属于本帧有效载荷
```

再比如 TPDO2：

```
frame.dlc = 4;
frame.data[0] = AI1 低字节;
frame.data[1] = AI1 高字节;
frame.data[2] = AI2 低字节;
frame.data[3] = AI2 高字节;
```

------



### 5. `uint8_t is_extended`

```
uint8_t is_extended;
```

表示是否为扩展帧：

```
0：标准帧
非 0：扩展帧
```

本项目只使用 CANopen 标准帧，也就是 11 位 CAN-ID：

```
if (frame->is_extended) {
    return CO_ERR_FRAME_TYPE;
}
```

CAN 扩展帧使用 29 位 ID，本项目不支持。

------



### 6. `uint8_t is_remote`

```
uint8_t is_remote;
```

表示是否为远程帧：

```
0：数据帧
非 0：远程帧
```

CAN 远程帧主要用于请求某个 CAN-ID 的数据，但 CANopen 这一版协议不使用它。

因此项目只接受普通数据帧：

```
if (frame->is_remote) {
    return CO_ERR_FRAME_TYPE;
}
```

------



### 7. `uint8_t is_fd`

```
uint8_t is_fd;
```

表示是否为 CAN FD 帧：

```
0：经典 CAN
非 0：CAN FD
```

本项目明确使用：

```
CAN 2.0A
11 位标准帧
最多 8 字节数据
```

所以 CAN FD 帧必须拒绝：

```
if (frame->is_fd) {
    return CO_ERR_FRAME_TYPE;
}
```

CAN FD 可以携带超过 8 字节的数据，但第一版项目不实现。

------



### 8. 一个完整的示例

假设主站向 Node-ID=1 下发 DO 状态：

```c
can_frame_t frame = {
    .id = 0x201,
    .dlc = 1,
    .data = {0x05},
    .is_extended = 0,
    .is_remote = 0,
    .is_fd = 0
};
```

它代表：

```c
CAN-ID：0x201
DLC：1
DATA[0]：0x05
标准帧
数据帧
经典 CAN
```

`0x05` 的二进制是：

```
0000 0101
```

表示：

```
DO1 = 1
DO2 = 0
DO3 = 1
DO4 = 0
```

------

这整个结构体的核心思想是：

> 用一个 C 结构体，同时保存 CAN-ID、有效长度、数据内容和帧类型信息，让协议层可以在不依赖 STM32 HAL 的情况下处理 CAN 报文。



### 9.经典can标准数据帧

经典 CAN **标准数据帧不一定是 8 字节**。

它的有效数据长度可以是：

```
0、1、2、3、4、5、6、7、8 字节
```

所以：

```
标准帧 ≠ 一定 8 字节
```

这里要区分两个概念：

- **标准帧**：指 CAN-ID 是 11 位。
- **数据长度**：由 DLC 表示，可以是 0～8 字节。

我们的项目使用的是：

```
11 位标准帧
经典 CAN
最大数据长度 8 字节
```

`co_types.h` 中：

```
#define CO_CAN_DATA_MAX 8u
```

表示：

> 给 `data` 数组预留 8 字节空间，因为经典 CAN 的最大有效载荷就是 8 字节。

而真正这一帧用了几个字节，由：

```
frame.dlc
```

决定。

例如：

```
TPDO1：DLC = 1，使用 1 字节
RPDO1：DLC = 1，使用 1 字节
TPDO2：DLC = 4，使用 4 字节
SDO：DLC = 8，使用 8 字节
NMT：DLC = 2，使用 2 字节
```

因此这段结构：

```
uint8_t data[CO_CAN_DATA_MAX];
```

虽然始终分配 8 个字节，但不代表每个报文都发送 8 字节。有效范围是：

```
data[0] 到 data[dlc - 1]
```

例如：

```
frame.dlc = 1;
frame.data[0] = 0x05;
```

只有 `data[0]` 有效，后面的数组空间只是预留的。

在我们的项目里，不是规定所有 CAN 报文必须 8 字节，而是每种 CANopen 报文按照协议规定自己的 DLC。





## 5：函数指针

```c
/* 发送函数指针*/

typedef co_status_t (*co_tx_fn)(void *user, const can_frame_t *frame);
```

`co_tx_fn` 是一种“函数指针类型”，指向的函数必须满足：
 传入两个参数 `user` 和 `frame`，返回一个 `co_status_t`。

也就是要求被指向的函数长这样：

```c
co_status_t can_send( void *user，const can_frame_t *frame）       
{
    // 真正发送 CAN 帧
    return CO_OK;
}
```

```c
你之前如果看到：

int *p;

你知道：

p 是一个指向 int 的指针。

而：

int (*p)(int);

就变成：

p 是一个指向函数的指针。

这里也是完全一样的思想：

co_status_t (*co_tx_fn)(...)

说明：

co_tx_fn 是一个函数指针。
```



```c
为什么 * 要写在括号里面？

这是 C 语言声明里非常重要的一点。

比较：

co_status_t *co_tx_fn(...);

和：

co_status_t (*co_tx_fn)(...);

含义完全不同。

第一种
co_status_t *co_tx_fn(...);

意思是：

co_tx_fn 是一个函数，这个函数返回 co_status_t *。

也就是：

函数
 ↓
返回一个 co_status_t*
第二种
co_status_t (*co_tx_fn)(...);

意思是：

co_tx_fn 是一个指向函数的指针，这个函数返回 co_status_t。

也就是：

co_tx_fn
   ↓
函数
   ↓
返回 co_status_t

所以括号：

(*co_tx_fn)

是在告诉编译器：

co_tx_fn 本身是函数指针。
```



#### PC端发送函数实例

```c
co_status_t co_init(co_context_t *ctx, uint8_t node_id,
                    co_tx_fn tx, void *tx_user)
{
    if (ctx == NULL || tx == NULL)
    { /* 先检查指针，避免写入无效内存或保存空回调 */
        return CO_ERR_ARGUMENT;
    }
    if (node_id == 0u || node_id > CO_NODE_ID_MAX)
    { /* 所有检查通过后才写上下文，保证失败不改配置 */
        return CO_ERR_NODE_ID;
    }
    ctx->node_id = node_id;             /* 记录本节点编号 */
    ctx->state = CO_NMT_INITIALIZATION; /* 初始状态，不在这里进入运行状态 */
    ctx->tx = tx;                       /* 绑定外部提供的发送函数 */
    ctx->tx_user = tx_user;             /* 绑定传输私有数据，可为空 */
    return CO_OK;                       /* 本次操作成功 */
}

需要填入4个形参
//第一个参数
先创建一个 co_context_t 类型的结构体变量：co_context_t node = {0};然后把它的地址作为第一个参数：
   
//第二个参数
例如我们要把节点编号设为 1，直接传 1：
    
//第三个参数：需要先写好一个发送函数，再把这个函数的地址传进去
函数声明中的第三个参数是：
co_tx_fn tx
这里：
co_tx_fn → 函数指针类型
tx       → 函数指针变量，用于接收发送函数的地址
先准备一个符合要求的函数
co_tx_fn 规定函数必须有这样的返回类型和参数：
co_status_t my_send(void *user, const can_frame_t *frame)
{
    /* 具体发送操作写在这里 */
}
上面只是展示函数形式，还不是完整实现。user 和 frame 暂时先不展开。
 PC端我们的发送函数名叫做fake_tx，一会传的是fake_tx，而不是my_send
    
//第四个参数：是第三个发送函数需要使用的数据的地址。
stm32实机测试则传送can句柄：&hcan1
目前为pc端测试，则传&bus ，为一个模拟总线变量 bus
因此初始化是
co_init(&node, 1, fake_tx, &bus);

//纠错：一个小术语修正：调用时填入的是四个“实参”，函数定义中接收它们的变量叫“形参”。

```



#### 完整流程：

```c
//PC端书写的模拟发送的函数
static co_status_t fake_tx(void *user, const can_frame_t *frame)
{
    fake_bus_t *bus = user;
    /* 将通用辅助指针还原为模拟总线结构体指针 */

    ++bus->calls;
    /* 每进入一次发送回调，调用次数加 1 */

    bus->frame = *frame;
    /* 复制整帧报文，保存最近一次提交的报文 */

    return bus->result;
    /* 返回预先设置的模拟发送结果 */
}
它依赖的结构体是：
typedef struct {
    unsigned calls;          /* 发送回调调用次数 */
    can_frame_t frame;       /* 最近一次提交的报文副本 */
    co_status_t result;      /* 预设的返回状态 */
} fake_bus_t;

    
//完整流程
完整流程是：
co_context_t node
    ├── node_id = 1
    ├── state = INITIALIZATION
    ├── tx ─────────► fake_tx
    └── tx_user ────► bus
调用：
co_send(&node, &tx_frame);
co_send() 通过 &node 找到：
ctx->tx
ctx->tx_user
然后执行：
ctx->tx(ctx->tx_user, frame);	//在co_init（）初始化中传入了我们的模拟总线量fake_bus_t bus的地址 ctx->tx_user
实际等价于：
fake_tx(&bus, &tx_frame);
进入回调后：
user  ──► bus
frame ──► tx_frame
回调执行：
++bus->calls;       /* 记录调用次数 */
bus->frame = *frame;/* 保存报文副本 */
return bus->result; /* 返回预设结果 */
所以：
node 保存“调用谁”和“给它什么辅助数据”
co_send 负责检查并连接这些信息
fake_tx 负责模拟这次发送并记录结果
tx_frame 保存本次准备发送的报文
但当前 PC 测试中，fake_tx() 只是记录报文，并没有真正发送到 CAN 总线。
    
调用关系：
co_init(&node, 1, fake_tx, &bus);//co_init(co_context_t *ctx, uint8_t node_id, co_tx_fn tx, void *tx_user)         
	    │
        ▼
co_send(&node, &tx_frame)	// co_send(const co_context_t *ctx, const can_frame_t *frame)
        │
        ▼
fake_tx(&bus, &tx_frame)	//fake_tx(void *user, const can_frame_t *frame)非单独调用，在co_send中由本句调用
        ▼    													// return ctx->tx(ctx->tx_user, frame); 
        │
        ├── bus.calls 加 1
        ├── bus.frame 保存 tx_frame 副本
        └── 返回 bus.result
          
//补充
 调用者先执行：
co_init(&node, 1, fake_tx, &bus)
    └── 保存函数地址和辅助数据地址，随后返回

调用者以后需要发送时，再执行：
co_send(&node, &tx_frame)
    │
    ├── 检查节点与报文
    │
    └── 内部调用 fake_tx(&bus, &tx_frame)
                    ├── bus.calls 加 1
                    ├── bus.frame 保存报文副本
                    └── 返回 bus.result
                              │
                              ▼
                    co_send 原样返回该结果
```



#### 纠错：

```
co_send 原样返回该结果就是bus.calls 加 1 ，bus.frame 保存报文副本 是不是？
```

```c
不是，这里要分清“修改变量”和“返回结果”。
fake_tx() 做了三件事：
++bus->calls;        /* 修改：调用次数加 1 */
bus->frame = *frame; /* 修改：保存报文副本 */

return bus->result; /* 返回：预设的状态码 */
前两项是直接修改 bus 里的成员，并不通过 return 返回。
“co_send() 原样返回该结果”指的是 bus.result 的状态码。例如：
bus.result = CO_OK;

co_status_t status = co_send(&node, &tx_frame);
执行后的结果是：
bus.calls → 比调用前增加 1
bus.frame → 保存了 tx_frame 的副本

bus.result = CO_OK
      │
      │ fake_tx 返回
      ▼
   co_send
      │
      │ 原样返回
      ▼
status = CO_OK
因此，次数和报文留在 bus 变量里；状态码通过 return 交给调用者。
```







## 6：节点信息结构体

```c
typedef struct {
    uint8_t node_id;		 /* 本节点编号，当前项目使用 1 */
    co_nmt_state_t state;    /* 节点当前 NMT 状态，阶段 1 只设置初值 */
    co_tx_fn tx; 			/* 保存发送函数地址，使协议层不依赖具体硬件 */
    void *tx_user; 			/* 保存传输层私有数据地址，发送时原样传给回调 */
} co_context_t;
```

把一个节点需要保存的信息，集中放在一起。



#### **① `uint8_t node_id;`**

```
uint8_t node_id;
```

保存本节点的编号。我们的项目使用：

```
Node-ID = 1
```

`uint8_t` 能保存 0～255，但程序会检查，要求节点号在 1～127 内。





#### **② `co_nmt_state_t state;`**

```
co_nmt_state_t state;
```

这里用到了前面定义的 NMT 状态枚举：

- `co_nmt_state_t`：类型名。
- `state`：结构体成员名。

它保存节点当前处于哪个状态，例如：

```c
CO_NMT_INITIALIZATION
CO_NMT_PRE_OPERATIONAL
CO_NMT_OPERATIONAL
CO_NMT_STOPPED
```

注意区分我们刚学过的两种类型：

```
co_status_t      → 一次函数操作的结果，例如成功、长度错误
co_nmt_state_t   → 节点的工作状态，例如预运行、运行
```





#### **③ `co_tx_fn tx;`**

```
co_tx_fn tx;
```

这就是上一段函数指针类型的实际使用：

- `co_tx_fn`：函数指针类型。
- `tx`：保存发送函数地址的成员。





#### **④ `void \*tx_user;`**

```
void *tx_user;
```

保存发送函数需要使用的辅助数据地址。

还记得发送函数的第一个参数吗？

```
void *user
```

这个成员就是为那个参数准备的：

```
tx       保存：调用哪个发送函数
tx_user  保存：调用时交给它什么辅助数据
```

例如发送函数需要操作某个发送队列，`tx_user` 就可以保存那个队列对象的地址。如果不需要辅助数据，可以使用 `NULL`。

这里保存的是**地址**，不会自动复制地址指向的数据。





#### **⑤ ` co_context_t;`**

```
 co_context_t;
```

结束结构体定义，并把这个类型命名为 `co_context_t`。

到这里，我们定义好了类型，但还没有创建节点变量。

写下面这一行，才真正创建变量：

```
co_context_t ctx = {0};
```

- `co_context_t`：类型。
- `ctx`：我们起的变量名。
- `{0}`：对成员进行零初始化；正式使用前仍需调用后面的初始化函数。

现在，之前提到的 `ctx.state` 就有出处了：

```
ctx.node_id = 1;                        /* 设置节点编号 */
ctx.state = CO_NMT_INITIALIZATION;      /* 设置初始状态 */
```

这里的点号 `.` 表示：**访问这个结构体变量中的某个成员。**

因此：

```
ctx.state
```

读作：“变量 `ctx` 里面的 `state` 成员。”

这段先掌握四个成员分别存什么：

| 成员      | 保存的内容                 |
| --------- | -------------------------- |
| `node_id` | 我是几号节点               |
| `state`   | 我当前处于什么 NMT 状态    |
| `tx`      | 我通过哪个函数提交报文     |
| `tx_user` | 发送函数需要的辅助数据地址 |



#### 实例：

```c
用 co_context_t 创建一个结构体变量 node：
co_context_t node = {0};
这个变量里面有四个成员：
node
 ├── node.node_id
 ├── node.state
 ├── node.tx
 └── node.tx_user
假设已经定义好 fake_bus_t 和 fake_tx()，我们就可以初始化它：
fake_bus_t bus = {0};       /* 创建模拟总线变量 */
co_context_t node = {0};    /* 创建节点上下文变量 */

co_init(&node, 1, fake_tx, &bus);
初始化成功后，等效于完成这些赋值：
node.node_id = 1;
node.state = CO_NMT_INITIALIZATION;
node.tx = fake_tx;
node.tx_user = &bus;
此时：
node
 ├── node_id = 1
 ├── state = CO_NMT_INITIALIZATION
 ├── tx ─────────► fake_tx 函数
 └── tx_user ────► bus 变量
co_context_t 是类型名，node 是用这个类型创建的结构体变量，也就是一个实例。
```



## 7：最后一个枚举

接收帧的分类类型

```c
typedef enum {
    CO_RX_NONE = 0,
    CO_RX_NMT,
    CO_RX_SDO,
    CO_RX_RPDO1
} co_rx_kind_t;
```

它用来记录：

> **收到的 CAN 帧属于哪一种需要处理的服务？**

这里 `RX` 表示接收，`kind` 表示类别。



#### **① `CO_RX_NONE = 0`**

```
CO_RX_NONE = 0,
```

表示没有匹配到需要处理的服务。

例如我们是节点 1，收到发给节点 2 的 SDO 请求：

```
CAN-ID = 0x602
```

它不属于我们的接收范围，分类结果保持 `CO_RX_NONE`。

分类开始前也会先设置为这个值，避免保留上一次的结果。





#### **② `CO_RX_NMT`**

```
CO_RX_NMT,
```

自动取值为 `1`，表示识别到**发给本节点或广播的 NMT 报文**。

对于我们的节点 1：

```
CAN-ID = 0x000，DATA = 01 01 → 发给节点 1
CAN-ID = 0x000，DATA = 01 00 → 广播
```

这两种都可以分类为 `CO_RX_NMT`。

当前阶段只识别类别，还不会执行进入 Operational 的命令。





#### **③ `CO_RX_SDO`**

```
CO_RX_SDO,
```

自动取值为 `2`，表示本节点的 SDO 请求。

节点 1 对应：

```
CAN-ID = 0x600 + 1 = 0x601
```

当前只根据地址识别它，SDO 命令、长度和对象地址的解析留到后续阶段。



#### **④ `CO_RX_RPDO1`**

```
CO_RX_RPDO1
```

自动取值为 `3`，表示本节点的 RPDO1。

节点 1 对应：

```
CAN-ID = 0x200 + 1 = 0x201
```

以后它用于接收 DO 输出命令。当前分类成功，不代表已经修改输出。





#### **⑤ `} co_rx_kind_t;`**

```
} co_rx_kind_t;
```

给这个枚举类型起名。以后可以这样创建变量：

```
co_rx_kind_t kind = CO_RX_NONE;
```

意思是：

> 创建一个保存接收类别的变量 `kind`，初始值为“未匹配”。



## 8：对比

现在我们已经遇到了三个枚举，可以放在一起区分：

| 类型             | 回答的问题             | 示例                       |
| ---------------- | ---------------------- | -------------------------- |
| `co_status_t`    | 这次操作结果怎么样？   | `CO_OK`：成功              |
| `co_nmt_state_t` | 节点当前处于什么状态？ | `CO_NMT_OPERATIONAL`：运行 |
| `co_rx_kind_t`   | 收到的帧属于什么类别？ | `CO_RX_SDO`：SDO 请求      |