```c
validate()
    ↓
active()
    ↓
abort_for_status()
    ↓
find_entry()
    ↓
response_base()
    ↓
send_abort()
    ↓
upload_command()
    ↓
co_sdo_init()
    ↓
co_sdo_receive()
```





# 1：枚举

```c
enum
{
    ABORT_TOGGLE    = UINT32_C(0x05040001),	//命令无效或本版本不支持
    ABORT_READ_WRITE = UINT32_C(0x06010002),//写入只读对象
    ABORT_INDEX     = UINT32_C(0x06020000),//Index 不存在
    ABORT_SUBINDEX  = UINT32_C(0x06090011),//Index 存在，但 Sub-index 不存在
    ABORT_LENGTH    = UINT32_C(0x06070010),//通用的数据长度或类型不匹配
    ABORT_TOO_LONG  = UINT32_C(0x06070012),//提供的数据过长
    ABORT_TOO_SHORT = UINT32_C(0x06070013),//提供的数据过短
    ABORT_VALUE     = UINT32_C(0x06090030),//参数值超出允许范围
    ABORT_STATE     = UINT32_C(0x08000022)//当前 NMT 状态不允许此次访问
};
```

匿名枚举，它不是为了创建某种公开类型，而只是给当前 `.c` 文件定义一组有名字的常量。

这些名字只在 `co_sdo.c` 中使用，其他文件不能直接使用：给固定的 Abort 数字起容易读懂的名字

阶段 2 的对象访问函数返回的是项目内部状态：

```c
co_od_find()  → CO_ERR_OD_NOT_FOUND
co_od_write() → CO_ERR_OD_READ_ONLY
co_od_write() → CO_ERR_OD_LENGTH
co_od_write() → CO_ERR_OD_VALUE
co_od_write() → CO_ERR_OD_STATE
```

阶段 5 不能直接把这些内部枚举值发给主站，而要转换成 CANopen 规定的 Abort code：

```c
阶段2内部状态              阶段5 Abort码
------------------------------------------------
CO_ERR_OD_NOT_FOUND   →    ABORT_INDEX / ABORT_SUBINDEX
CO_ERR_OD_READ_ONLY   →    ABORT_READ_WRITE
CO_ERR_OD_LENGTH      →    ABORT_LENGTH / TOO_LONG / TOO_SHORT
CO_ERR_OD_VALUE       →    ABORT_VALUE
CO_ERR_OD_STATE       →    ABORT_STATE
```

也就是说：

```
阶段2负责判断具体哪里不合法
阶段5负责把结果包装成标准SDO错误响应
```

这些枚举值本身不发送报文。后面的 `send_abort()` 会把其中一个错误码放进：

```
0x80 Abort 响应帧
```

整体关系是：

```c
对象访问失败
    ↓
得到内部 co_status_t
    ↓
转换为 ABORT_xxx
    ↓
组装 0x80 响应帧
    ↓
发送给主站
```



# 2： validate(  )函数

```c
static co_status_t validate(const co_sdo_t *sdo)
{
    if (sdo == NULL || sdo->node == NULL || sdo->node->tx == NULL ||
        sdo->device == NULL || sdo->device->table.entries != sdo->device->entries ||
        sdo->device->table.count != CO_DEVICE_OD_COUNT)
        return CO_ERR_ARGUMENT;
    if (sdo->node->node_id == 0u || sdo->node->node_id > CO_NODE_ID_MAX)
        return CO_ERR_NODE_ID;
    switch (sdo->node->state)
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

这个 `validate()` 是 SDO 模块内部的统一基础检查函数，用来确认 `co_sdo_t` 连接的节点和对象字典是否有效。





### 1： 检查 SDO 对象和节点

```c
if (sdo == NULL ||
    sdo->node == NULL ||
    sdo->node->tx == NULL ||
    sdo->device == NULL ||
    sdo->device->table.entries != sdo->device->entries ||
    sdo->device->table.count != CO_DEVICE_OD_COUNT)
    return CO_ERR_ARGUMENT;
```

只要其中一个条件成立，就返回：CO_ERR_ARGUMENT



###### `sdo == NULL`

例如：

```
co_sdo_receive(NULL, &request);
```

没有 SDO 对象，当然无法处理请求。



###### `sdo->node == NULL`

表示 SDO 结构体没有绑定节点：

```
co_sdo_t sdo = {0};
```

此时：

```
sdo.node == NULL
```

SDO 不知道 Node-ID、NMT 状态，也无法发送响应。



###### `sdo->node->tx == NULL`

节点存在，但没有发送回调。

SDO 收到请求后必须发送：

```
正常响应
或
Abort 错误响应
```

如果没有：

```
node->tx
```

就无法发送任何响应。



###### `sdo->device == NULL`

表示没有绑定设备对象字典。

SDO 的核心工作就是根据：

```
Index + Sub-index
```

查找并读写对象，所以设备对象字典不能为空。





###### 检查对象表地址

```
sdo->device->table.entries != sdo->device->entries
```

初始化成功后应该是：

```
device->table.entries ───► device->entries[0]
```

也就是对象表的入口指针，应该指向设备内部的实际条目数组。

如果两者不相等，说明对象表没有正确初始化，或者对象结构被错误复制、破坏过。



###### 检查对象数量

```
sdo->device->table.count != CO_DEVICE_OD_COUNT
```

当前项目规定：

```
#define CO_DEVICE_OD_COUNT 36u
```

所以对象表数量必须是 36。

第一组判断整体是在确认：

```c
SDO 对象有效吗？
    ↓
节点有效吗？
    ↓
节点可以发送吗？
    ↓
设备对象字典有效吗？
    ↓
对象表绑定正确吗？
    ↓
对象数量正确吗？
```





### 2：检查 Node-ID

```c
if (sdo->node->node_id == 0u ||
    sdo->node->node_id > CO_NODE_ID_MAX)
    return CO_ERR_NODE_ID;
```

有效的普通节点号是：

```
1 ~ 127
```

因此：

```
0       非法，0 用于 NMT 广播目标
1~127   合法
128以上 非法
```

SDO 响应 CAN-ID 是：

```
0x580 + Node-ID
```

例如：

```
Node-ID = 1 → 0x581
Node-ID = 2 → 0x582
```

如果 Node-ID 是 0，就会得到：

```
0x580
```

这不是普通节点的 SDO 响应地址，所以要拒绝。



### 3：检查 NMT 状态值是否合法

```c
switch (sdo->node->state)
{
case CO_NMT_INITIALIZATION:
case CO_NMT_PRE_OPERATIONAL:
case CO_NMT_OPERATIONAL:
case CO_NMT_STOPPED:
    return CO_OK;

default:
    return CO_ERR_ARGUMENT;
}
//检查状态值是否属于合法枚举
```

这里确认 `node->state` 是项目定义的四种合法状态之一：

```c
CO_NMT_INITIALIZATION = 0x00
CO_NMT_STOPPED        = 0x04
CO_NMT_OPERATIONAL    = 0x05
CO_NMT_PRE_OPERATIONAL= 0x7F
```

如果状态被破坏成：

```
node->state = 99;
```

就会走：

```
default
```

返回：

```
CO_ERR_ARGUMENT
```



整体流程可以记成：

```c
co_sdo_receive()
        │
        ▼
validate(sdo)
        │
        ├── 上下文错误 → 返回 CO_ERR_ARGUMENT / CO_ERR_NODE_ID
        │
        ▼
co_classify_rx()
        │
        ▼
active(node->state)
        │
        ├── Initialization/Stopped → CO_IGNORED
        │
        └── Pre-op/Operational → 继续解析 SDO
```





# 3：active（）函数

```c
static int active(const co_nmt_state_t state)
{
    return state == CO_NMT_PRE_OPERATIONAL || state == CO_NMT_OPERATIONAL;
}
```

这个 `active()` 是 SDO 模块内部的状态判断辅助函数，用来回答一个问题：当前 NMT 状态是否允许 SDO 服务处理请求？

```c
Initialization  → 忽略 SDO
Pre-operational → 处理 SDO
Operational     → 处理 SDO
Stopped         → 忽略 SDO
```

流程是：

```c
co_sdo_receive()
        ↓
active(node->state)
        ↓
判断 SDO 服务是否开放
```





# 4：abort_for_status（）转错误码函数

```c

static uint32_t abort_for_status(co_status_t status, uint8_t requested,
                                 uint8_t expected)
{
    switch (status)
    {
    case CO_ERR_OD_NOT_FOUND: return ABORT_INDEX;
    case CO_ERR_OD_READ_ONLY: return ABORT_READ_WRITE;
    case CO_ERR_OD_STATE: return ABORT_STATE;
    case CO_ERR_OD_VALUE: return ABORT_VALUE;
    case CO_ERR_OD_LENGTH:
        if (requested > expected) return ABORT_TOO_LONG;
        if (requested < expected) return ABORT_TOO_SHORT;
        return ABORT_LENGTH;
    default: return ABORT_LENGTH;
    }
}
```

这个 `abort_for_status()` 的作用是：

> 把阶段 2 对象访问层返回的内部错误状态，转换成 CANopen SDO 要发送给主站的 Abort 错误码。

`requested` 和 `expected` 不是在其他文件里定义的，它们是 `abort_for_status()` 自己的函数形参：

```c
static uint32_t abort_for_status(co_status_t status,
                                 uint8_t requested,
                                 uint8_t expected)
```

它们只在这个函数内部有效。

含义是：

```c
requested
    本次 SDO 请求声称要传输的字节数

expected
    对象条目实际要求的字节数
```

它们的来源在 `co_sdo_receive()` 中。



#### 1：`requested` 从哪里来？

在 `co_sdo_receive()` 中，根据 SDO 下载命令确定：

```c
#define CO_SDO_CMD_DOWNLOAD_1 0x2Fu  // 写 1 字节
#define CO_SDO_CMD_DOWNLOAD_2 0x2Bu  // 写 2 字节
#define CO_SDO_CMD_DOWNLOAD_3 0x27u  // 写 3 字节
#define CO_SDO_CMD_DOWNLOAD_4 0x23u  // 写 4 字节

case CO_SDO_CMD_DOWNLOAD_1:
    requested_length = 1u;
    break;

case CO_SDO_CMD_DOWNLOAD_2:
    requested_length = 2u;
    break;

case CO_SDO_CMD_DOWNLOAD_3:
    requested_length = 3u;
    break;

case CO_SDO_CMD_DOWNLOAD_4:
    requested_length = 4u;
    break;
```

所以：

```
requested = requested_length
```

例如请求命令是：

```
0x2B
```

代码就设置：

```
requested_length = 2u;
```

之后调用：

```
abort_for_status(status,requested_length,entry->length);
```

因此函数内部对应为：

```
requested = requested_length = 2
```





#### 2：`expected` 从哪里来？

```c
expected	//是对象条目规定的标准长度
```

例如：

```
0x1017:00 类型 UNSIGNED16
entry->length = 2
```

因此调用：

```
abort_for_status(status, requested_length, entry->length);
```

对应为：

```
requested = 主站本次请求长度
expected  = 对象实际需要长度
```





#### 3：函数体

```c
switch (status)
status 是阶段 2 对象访问层返回的结果，例如：
CO_ERR_OD_NOT_FOUND
CO_ERR_OD_READ_ONLY
CO_ERR_OD_STATE
CO_ERR_OD_VALUE
CO_ERR_OD_LENGTH
switch 会根据 status 的值，进入匹配的 case。
```

```c
    switch (status)
    {
    case CO_ERR_OD_NOT_FOUND: return ABORT_INDEX;			//对象不存在
    case CO_ERR_OD_READ_ONLY: return ABORT_READ_WRITE;		//对象只读
    case CO_ERR_OD_STATE: return ABORT_STATE;			   //当前 NMT 状态不允许写入
    case CO_ERR_OD_VALUE: return ABORT_VALUE;			   //如果数据长度正确、对象也允许写入，但数值超出范围
    case CO_ERR_OD_LENGTH:								 //主站本次提供的字节数≠对象实际需要的字节数
        if (requested > expected) return ABORT_TOO_LONG;	//表示主站提供了 4 字节，但对象只需要 2 字节。
        if (requested < expected) return ABORT_TOO_SHORT;	//表示对象需要 2 字节，但主站只提供 1 字节。
        return ABORT_LENGTH;							 //请求长度和对象条目声明长度表面上相等，但对象条目的类型与长度描述自身不一致，无法进
            											一步归类，只返回通用长度错误，这属于是对象条目定义的uint16_t，而entry->length 填的1
    default: return ABORT_LENGTH;						 //如果 status 不是前面列出的几种错误，就进入 default
    }
```







# 5：find_entry( )SDO 层自己的查找辅助函数

```c
static co_status_t find_entry(const co_device_od_t *device, uint16_t index,
                              uint8_t subindex, const co_od_entry_t **entry,
                              uint32_t *abort_code)
{
    size_t i;
    co_status_t status = co_od_find(&device->table, index, subindex, entry);
    if (status == CO_OK)
        return CO_OK;
    for (i = 0u; i < device->table.count; ++i)
        if (device->table.entries[i].index == index)
        {
            *abort_code = ABORT_SUBINDEX;
            return CO_ERR_OD_NOT_FOUND;
        }
    *abort_code = ABORT_INDEX;
    return CO_ERR_OD_NOT_FOUND;
}
```

这个 `find_entry()` 是 SDO 层自己的查找辅助函数。它在阶段 2 的 `co_od_find()` 基础上多做了一件事：

> 区分到底是 Index 不存在，还是 Index 存在但 Sub-index 不存在。



```c
调用 co_od_find()
        │
        ├── 找到完整 Index:Sub-index
        │       └── 返回 CO_OK
        │
        └── 没找到
                │
                ▼
        遍历所有条目检查 Index
                │
        ┌───────┴────────┐
        ▼                ▼
   Index 存在        Index 不存在
        │                │
        ▼                ▼
ABORT_SUBINDEX      ABORT_INDEX
0x06090011          0x06020000
```

因此，`find_entry()` 自己不发送报文，也不读写对象；它只负责查找对象并准备精确的 Abort 原因。





# 6：response_base（）DO 响应帧的“生成函数”

```c
static can_frame_t response_base(const co_sdo_t *sdo, uint16_t index,
                                 uint8_t subindex)
{
    can_frame_t response = {0};
    response.id = CO_COB_SDO_TX_BASE + sdo->node->node_id;
    response.dlc = 8u;
    response.data[1] = (uint8_t)index;
    response.data[2] = (uint8_t)(index >> 8);
    response.data[3] = subindex;
    return response;
}
```

这个 `response_base()` 是 SDO 响应帧的“基础模板生成函数”。

它先把每种响应都共同需要的内容填好：

```c
响应 CAN-ID
DLC
Index
Sub-index
```

后面再由上传成功、写入成功或 Abort 函数补充：

```c
Data[0] 命令字
Data[4..7] 数据或错误码
```



#### 1：返回值是个结构体

函数内部创建：

```
can_frame_t response = {0};
```

最后：

```
return response;
```

表示把这个结构体的内容复制一份返回给调用者。



#### 2：先清零整帧

```
can_frame_t response = {0};
```

这会把所有成员初始化为 0：

```c
response.id           = 0
response.dlc          = 0
response.data[0..7]   = 0
response.is_extended  = 0
response.is_remote    = 0
response.is_fd        = 0
```

这样后面只修改 SDO 必须的字段，剩下的字节自然保持 0。

这对于 SDO 很重要，因为项目规定：

```c
SDO 请求和响应 DLC 固定为 8
未使用的数据字节填 0
```



#### 3：设置响应 CAN-ID

```
response.id = CO_COB_SDO_TX_BASE + sdo->node->node_id;
```

`CO_COB_SDO_TX_BASE` 是：

```
0x580
```

所以实际公式是：

```
响应 CAN-ID = 0x580 + Node-ID
```

例如：

```
Node-ID = 1
response.id = 0x580 + 1 = 0x581
Node-ID = 2
response.id = 0x580 + 2 = 0x582
```

这里的：

```
sdo->node->node_id
```

可以拆成：

```
sdo
    └── node 指针
          └── node_id
```

它读取的是 SDO 初始化时绑定的节点号。



#### 4.设置固定 DLC

```c
response.dlc = 8u;
```

SDO 第一版固定使用 8 字节数据区，所以无论是：

```
读取成功
写入成功
Abort 错误
```

响应帧的 DLC 都是：

```
8
```

即使读取的对象只有 1 字节，帧仍然是：

```
DLC = 8
```

只是剩余数据字节填 0。





#### 5. 写入 Index 的低字节

```
response.data[1] = (uint8_t)index;
```

`index` 是：

```
uint16_t
```

占两个字节。

CANopen 使用小端顺序，所以低字节放在前面。

例如：

```
index = 0x1017;
```

二进制分成：

```
高字节 = 0x10
低字节 = 0x17
```

因此：

```
response.data[1] = 0x17;
```

强制转换：

```
(uint8_t)index
```

会保留低 8 位。

------



#### 6. 写入 Index 的高字节

```
response.data[2] = (uint8_t)(index >> 8);
```

先右移 8 位：

```
0x1017 >> 8 = 0x10
```

再转换成 8 位：

```
response.data[2] = 0x10;
```

最终：

```
data[1] = 0x17
data[2] = 0x10
```

组合起来就是：

```
Index = 0x1017
```

所以响应帧中的 Index 格式是：

```
Data[1] = Index 低字节
Data[2] = Index 高字节
```

------



#### 7. 写入 Sub-index

```
response.data[3] = subindex;
```

子索引本身就是一个字节，所以直接放到：

```
Data[3]
```

例如：

```
Index    = 0x6401
Sub-index = 0x02
```

则：

```
Data[1] = 0x01
Data[2] = 0x64
Data[3] = 0x02
```

------



#### 8. 返回基础帧

```
return response;
```

此时返回的响应帧大致是：

```
CAN-ID = 0x581
DLC    = 8
Data   = 00 17 10 00 00 00 00 00
```

假设：

```
Node-ID = 1
Index    = 0x1017
Sub-index = 0
```

注意：

```
Data[0] 还没有填写
Data[4..7] 还没有填写
```

它们仍然是 0，等调用者继续补充。



# 7: send_abort （）组装错误响应

```c
static co_status_t send_abort(const co_sdo_t *sdo, uint16_t index,
                              uint8_t subindex, uint32_t code)
{
    can_frame_t response = response_base(sdo, index, subindex);
    response.data[0] = CO_SDO_CMD_ABORT;
    response.data[4] = (uint8_t)code;
    response.data[5] = (uint8_t)(code >> 8);
    response.data[6] = (uint8_t)(code >> 16);
    response.data[7] = (uint8_t)(code >> 24);
    return co_send(sdo->node, &response);
}

参数：
sdo
    SDO 服务上下文，用于取得节点号和发送入口

index
    出错的对象 Index

subindex
    出错的对象 Sub-index

code
    具体的 4 字节 Abort 错误码
    
例如：
send_abort(sdo, 0x9999, 0, ABORT_INDEX);
表示：
访问 0x9999:00 失败，原因是 Index 不存在
```

这个 `send_abort()` 专门负责：

> 组装并发送一帧 SDO Abort 错误响应。



#### 1. 先创建响应帧基础模板

```c
can_frame_t response = response_base(sdo, index, subindex);
```

调用刚刚学习的 `response_base()`，先填好公共字段：

```c
response.id      = 0x580 + Node-ID
response.dlc     = 8
response.data[1] = Index 低字节
response.data[2] = Index 高字节
response.data[3] = Sub-index
```

假设：

```
Node-ID = 1
Index = 0x9999
Sub-index = 0
```

此时响应帧基础内容是：

```c
CAN-ID = 0x581
DLC    = 8
Data   = 00 99 99 00 00 00 00 00
```

`Data[0]` 和 `Data[4..7]` 还没有正式填写。



#### 2. 写入 Abort 命令字

```c
response.data[0] = CO_SDO_CMD_ABORT;
```

宏定义是：

```c
#define CO_SDO_CMD_ABORT 0x80u
```

所以：

```
Data[0] = 0x80
```

它表示：

> 这是一帧 SDO Abort 响应。

此时：

```
Data = 80 99 99 00 00 00 00 00
```





#### 3. 把 32 位错误码拆成四个字节

```c
response.data[4] = (uint8_t)code;
response.data[5] = (uint8_t)(code >> 8);
response.data[6] = (uint8_t)(code >> 16);
response.data[7] = (uint8_t)(code >> 24);
```

Abort 错误码是一个 `uint32_t`，例如：

```c
code = ABORT_INDEX = 0x06020000
```

SDO 使用小端顺序，所以要拆成：

```c
Data[4] = 0x00
Data[5] = 0x00
Data[6] = 0x02
Data[7] = 0x06
```

最终数据区：

```
80 99 99 00 00 00 02 06
```

逐字节对应：

```
Data[0]    = 0x80       Abort 命令字
Data[1..2] = 99 99      Index = 0x9999
Data[3]    = 00         Sub-index = 0
Data[4..7] = 00 00 02 06
                         Abort code = 0x06020000
```

这里每句的作用：

```
(uint8_t)code
```

取最低 8 位。

```
(uint8_t)(code >> 8)
```

先右移 8 位，再取下一字节。

```
(uint8_t)(code >> 16)
```

取第三个字节。

```
(uint8_t)(code >> 24)
```

取最高字节。





#### 4. 通过阶段 1 的发送入口发送

```c
return co_send(sdo->node, &response);
```

这里：

```c
sdo->node
    保存的节点地址

&response
    当前 Abort 响应帧地址
```

等价于我们之前见过的：

```c
co_send(&node, &frame);
```

只不过这里的节点地址保存在 SDO 上下文中，响应帧是在函数内部创建的。



#### 调用链：

```c
send_abort()
    ↓
response_base()
    ├── 设置 0x580 + Node-ID
    ├── 设置 DLC=8
    └── 设置 Index/Sub-index
    ↓
填入 0x80
    ↓
填入 4 字节 Abort code
    ↓
co_send()
    ↓
node->tx()
```

如果模拟发送回调返回：

```
CO_OK
```

那么 `send_abort()` 返回：

```
CO_OK
```

如果发送层模拟：

```
CO_ERR_TX_BUSY
```

那么这里也会原样返回：

```
CO_ERR_TX_BUSY
```



#### 5. 一个完整例子

调用：

```c
send_abort(sdo, 0x6401, 3, ABORT_SUBINDEX);
```

其中：

```c
ABORT_SUBINDEX = 0x06090011
```

最终响应可能是：

```
CAN-ID = 0x581
DLC    = 8
Data   = 80 01 64 03 11 00 09 06
```

解释：

```c
80        	  Abort
01 64      	  Index = 0x6401
03            Sub-index = 3
11 00 09 06    Abort code = 0x06090011
```

所以这个函数完成的是：

```c
内部 Abort code
    ↓
放入固定 8 字节 SDO 响应帧
    ↓
调用 co_send() 发给主站
```

它不负责判断错误原因；错误原因已经由前面的 `find_entry()` 或 `abort_for_status()` 决定。`send_abort()` 只负责把已经确定的错误码包装成报文并发送。





# 8：SDO Upload 返回成功响应命令字

在组装报文响应帧有用处！！！！！！！！！！！！

```c
static uint8_t upload_command(uint8_t length)
{
    static const uint8_t commands[5] = {0u, CO_SDO_CMD_UPLOAD_1,
                                        CO_SDO_CMD_UPLOAD_2,
                                        CO_SDO_CMD_UPLOAD_3,
                                        CO_SDO_CMD_UPLOAD_4};
    return length <= 4u ? commands[length] : 0u;
}
```

这个 `upload_command()` 的作用是：

> 根据对象数据长度，选择对应的 SDO Upload 成功响应命令字。



#### 1. 参数 `length`

```
uint8_t length
```

表示对象数据占多少个有效字节。

来自对象条目：

```
entry->length
```

例如：

```
0x6423:00 → 1 字节
0x1017:00 → 2 字节
0x6401:01 → 2 字节
0x1000:00 → 4 字节
```





#### 2. 建立命令表

```c
static const uint8_t commands[5] = {
    0u,
    CO_SDO_CMD_UPLOAD_1,
    CO_SDO_CMD_UPLOAD_2,
    CO_SDO_CMD_UPLOAD_3,
    CO_SDO_CMD_UPLOAD_4
};
```

对应的数组内容是：

```c
commands[0] = 0
commands[1] = 0x4F
commands[2] = 0x4B
commands[3] = 0x47
commands[4] = 0x43
```

因为宏定义是：

```c
#define CO_SDO_CMD_UPLOAD_1 0x4Fu
#define CO_SDO_CMD_UPLOAD_2 0x4Bu
#define CO_SDO_CMD_UPLOAD_3 0x47u
#define CO_SDO_CMD_UPLOAD_4 0x43u
```

所以可以画成：

```c
对象长度       Upload 响应命令
1 字节    →    0x4F
2 字节    →    0x4B
3 字节    →    0x47
4 字节    →    0x43
```

第 0 项：

```c
commands[0] = 0u;
```

只是占位，因为有效对象长度从 1 到 4，不存在 0 字节的正常 Upload 响应。







#### 3. `static const`

```
static const uint8_t commands[5]
```

`const`

表示数组内容不会被修改：

```
commands[1] = 0x55; // 不允许
```

它只是固定的命令映射表。

`static`

在这个函数里表示：

- 数组只在当前函数内部可见；
- 数组存储在静态存储区；
- 不会每次调用都重新创建和初始化。

因此多次调用：

```
upload_command(1);
upload_command(2);
```

使用的都是同一张固定命令表。





#### 4. 条件运算符

```c
return length <= 4u ? commands[length] : 0u;
```

等价于：

```c
if (length <= 4u)
    return commands[length];
else
    return 0u;
```

如果长度在 0～4 之间，就查表返回。

如果长度大于 4，就返回：

```
0u
```

因为当前项目只支持最多 4 字节的 expedited SDO。



#### 示例：

长度为 1

```
upload_command(1);
```

查表：

```
commands[1]
```

返回：

```
0x4F
```

表示：

```
读取成功，返回 1 字节
```



长度为 2

```
upload_command(2);
```

返回：

```
0x4B
```

例如读取：

```
0x1017:00
```



长度为 4

```
upload_command(4);
```

返回：

```
0x43
```



长度超过 4

```
upload_command(5);
```

因为：

```
5 <= 4u
```

为假，所以返回：

```
0
```

表示没有对应的 Upload 成功命令。



#### 5. 在 `co_sdo_receive()` 中的使用

上传请求处理成功后：

```
response.data[0] = upload_command(entry->length);
```

例如：

```
entry->length = 2
```

则：

```
response.data[0] = 0x4B
```

后面的对象数据会放入：

```
response.data[4]
response.data[5]
```

最终形成：

```
4B 17 10 00 E8 03 00 00
```

所以这个函数的关系是：

```c
对象条目长度
    ↓
upload_command(length)
    ↓
选择对应 Upload 成功命令
    ↓
填写 response.data[0]
```

它把原本可能写成的多重 `if`：

```c
if (length == 1)
    command = 0x4F;
else if (length == 2)
    command = 0x4B;
else if (length == 3)
    command = 0x47;
else if (length == 4)
    command = 0x43;
```

简化成数组查表：

```
commands[length]
```

可以记成：

```
1 → 0x4F
2 → 0x4B
3 → 0x47
4 → 0x43
```

这个函数只负责选择命令字，不负责读取对象、不负责组帧，也不负责发送。



# 9：co_sdo_init( )

```c
co_status_t co_sdo_init(co_sdo_t *sdo, co_context_t *node,
                        co_device_od_t *device)
{
    co_sdo_t candidate = {node, device};
    co_status_t status;
    if (sdo == NULL)
        return CO_ERR_ARGUMENT;
    status = validate(&candidate);
    if (status != CO_OK)
        return status;
    *sdo = candidate;
    return CO_OK;
}
```

这个 `co_sdo_init()` 的作用是：

> 检查传入的节点和对象字典是否有效；检查通过后，把它们的地址保存到真正的 SDO 对象中。



先看调用方创建的变量：

```c
co_context_t node = {0};
co_device_od_t device = {0};
co_sdo_t sdo = {0};
```

调用：

```c
co_sdo_init(&sdo, &node, &device);
```

这里的传入的实参是：

```
&sdo     sdo 变量的地址
&node    node 变量的地址
&device  device 变量的地址
```

函数内部的形参含义是：

```c
sdo     指向外部 sdo 变量
node    指向外部 node 变量
device  指向外部 device 变量
    
//什么意思呢？
就是指的是这一句co_sdo_t candidate = {node, device};
这里面的node device 存的东西是&node 和 &device ，//sb codex 和我道歉说下次不要把形参和传入的实参名弄一样了
这里确实存在同名混淆：外部变量也叫 node，函数形参也叫 node。在函数内部，node 指的是形参指针，也就是保存着外部 node 地址的指针。
    
    
```



#### 1. 创建临时候选对象

```
co_sdo_t candidate = {node, device};
```

根据结构体定义：

```
typedef struct
{
    co_context_t *node;
    co_device_od_t *device;
} co_sdo_t;
```

这句等价于：

```
co_sdo_t candidate;

candidate.node = node;
candidate.device = device;
```

由于调用时：

```
node 形参 = &外部 node
device 形参 = &外部 device
```

所以实际形成：

```
candidate.node   ───► 外部 node
candidate.device ───► 外部 device
```

注意，这里没有复制整个节点或对象字典，只是复制它们的地址。

------



#### 2. 定义状态变量

```
co_status_t status;
```

这个变量保存检查结果，例如：

```
status = validate(&candidate);
```

------



#### 3. 检查输出目标 `sdo`

```
if (sdo == NULL)
    return CO_ERR_ARGUMENT;
```

这里检查的是：

> 有没有有效的地方保存初始化结果？

错误调用：

```
co_sdo_init(NULL, &node, &device);
```

虽然节点和对象字典地址都传了，但没有 `sdo` 目标保存结果，因此返回：

```
CO_ERR_ARGUMENT
```

正常调用：

```
co_sdo_init(&sdo, &node, &device);
```

才可以继续。





#### 4. 验证候选对象

```
status = validate(&candidate);

if (status != CO_OK)
    return status;
```

`validate()` 会检查：

```
candidate.node 是否为空
node->tx 是否存在
candidate.device 是否为空
对象表是否正确初始化
对象条目数量是否正确
Node-ID 是否有效
NMT 状态是否有效
```

正确初始化顺序是：

```
co_init(&node, 1, tx, &fake);
co_device_od_init(&device, 1, NULL);
co_sdo_init(&sdo, &node, &device);
```

如果 `node` 或 `device` 尚未初始化，`validate()` 就会失败，`co_sdo_init()` 直接返回错误。

------



#### 5. 正式写入 SDO 对象

```
*sdo = candidate;
```

这里：

```
sdo
```

是指针，指向外部创建的 `sdo` 变量。

所以：

```
*sdo
```

就是外部那个实际的 `co_sdo_t sdo` 结构体。

这句等价于：

```
sdo.node = candidate.node;
sdo.device = candidate.device;
```

最终关系是：

```
sdo.node   ───► node
sdo.device ───► device
```

之后 `co_sdo_receive()` 中：

```
sdo->node->node_id
```

读取的就是外部变量：

```
node.node_id
```

而：

```
sdo->device->table
```

访问的就是外部变量：

```
device.table
```





#### 6. 返回成功

```
return CO_OK;
```

表示：

```
SDO 上下文绑定成功
节点地址已保存
对象字典地址已保存
```

这个函数不会：

```
发送 SDO 报文
读取对象
写入对象
改变 NMT 状态
```

它只负责初始化连接关系。

整体流程：

```c
co_sdo_init(&sdo, &node, &device)
                │
                ▼
创建 candidate，暂存两个地址
                │
                ▼
检查 sdo、node、device 是否有效
                │
                ├── 失败 → 返回错误，不提交
                │
                ▼
*sdo = candidate
                │
                ▼
sdo.node   → node
sdo.device → device
```

这个临时 `candidate` 的好处是：验证失败时，不会先把无效的节点或对象字典写入真正的 `sdo`。只有全部检查通过后，才一次性提交。





# 10：co_sdo_receive（）负责接收并处理一帧 SDO 请求



```c
co_status_t co_sdo_receive(co_sdo_t *sdo, const can_frame_t *request)
{
    /*1：创建临时变量*/
    co_rx_kind_t kind;                 // 阶段1分类器给出的报文类型
    const co_od_entry_t *entry = NULL; // 用来存找到的对象条目地址
    can_frame_t response;              // 准备发送给主站的响应帧结构体变量
    uint16_t index;                    // 请求中的16位对象索引
    uint8_t subindex;                  // 请求中的子索引
    uint8_t requested_length = 0u;     // Download请求声明的有效数据长度
    uint32_t abort_code = ABORT_INDEX; // 失败时要返回的Abort码
    uint8_t data[4] = {0};             // 暂存读取出的对象数据

    /*2：先验证 SDO 上下文*/
    co_status_t status = validate(sdo); // 当前操作返回状态
    if (status != CO_OK)
        return status;

    /*3：复用阶段 1 的接收分类器*/
    status = co_classify_rx(sdo->node, request, &kind);
    if (status != CO_OK)
        return status;
    /*4：状态门控，判断是否为SDO帧和当前状态NMT是否允许处理SDO*/
    if (kind != CO_RX_SDO || !active(sdo->node->state))
        return CO_IGNORED;
    /*5：检查 SDO 固定 DLC：当前项目的 SDO 只支持固定 8 字节帧，其余均不再往下处理*/
    if (request->dlc != 8u)
        return CO_ERR_DLC;
    /*6：将接收报文中的字节按小端组合为 Index 和 Sub-index。例如 data[1]=0x17、data[2]=0x10，组合得到 Index=0x1017。*/

    index = (uint16_t)request->data[1] | ((uint16_t)request->data[2] << 8);
    subindex = request->data[3];

    /*7：判断是否为读取命令还是写入命令*/
    switch (request->data[0])
    {
    case CO_SDO_CMD_UPLOAD:                                                     // #define CO_SDO_CMD_UPLOAD 0x40u ，此时表示主站请求读取对象。
        status = find_entry(sdo->device, index, subindex, &entry, &abort_code); /*8：查找对象，找到了返回CO_OK，进入下方的if，没找到就不进入下面这么大的if，函数调用 send_abort() 返回错误响应。*/
        if (status == CO_OK)
        {
            status = co_od_read(entry, data, entry->length); /*9：读取对象数据*/
            if (status != CO_OK)

                abort_code = abort_for_status(status, entry->length, entry->length); /*10：如果读取失败：bort_for_status（）会把阶段2的内部错误转换成SDO Abort码走出 if 后，函数调用 send_abort() 返回错误响应。*/
            else
            {
                response = response_base(sdo, index, subindex); /*11. 组装读取成功响应*/
                response.data[0] = upload_command(entry->length);
                for (requested_length = 0u; requested_length < entry->length; ++requested_length)
                    response.data[4u + requested_length] = data[requested_length]; // 然后复制读取出来的对象数据：
                return co_send(sdo->node, &response);                              /*12：发送读取成功响应*/
            }
        }
        return send_abort(sdo, index, subindex, abort_code); /*如果没找到条目对象，那么组装并发送一帧 SDO Abort 错误响应*/

    case CO_SDO_CMD_DOWNLOAD_1: // #define CO_SDO_CMD_DOWNLOAD_1 0x2F  ，此时表示主站写入1字节内容。
        requested_length = 1u;
        break;
    case CO_SDO_CMD_DOWNLOAD_2: // #define CO_SDO_CMD_DOWNLOAD_2 0x2B   ，此时表示主站写入2字节内容。
        requested_length = 2u;
        break;
    case CO_SDO_CMD_DOWNLOAD_3: // #define CO_SDO_CMD_DOWNLOAD_3 0x27   ，此时表示主站写入3字节内容。
        requested_length = 3u;
        break;
    case CO_SDO_CMD_DOWNLOAD_4: // #define CO_SDO_CMD_DOWNLOAD_4 0x23   ，此时表示主站写入4字节内容。
        requested_length = 4u;
        break;
    default:
        return send_abort(sdo, index, subindex, ABORT_TOGGLE); // 未知命令则返回，并且组装并发送一帧 SDO Abort 错误响应
    }

    /*13:查找要写入的对象,和读取一样，先确认对象存在。*/
    status = find_entry(sdo->device, index, subindex, &entry, &abort_code);
    if (status != CO_OK)
        return send_abort(sdo, index, subindex, abort_code);
    /*14：先检查对象是否可写*/
    if (entry->access != CO_OD_READ_WRITE)
        return send_abort(sdo, index, subindex, ABORT_READ_WRITE);
    /*15：检查请求长度和对象长度*/
    if (requested_length != entry->length)
        return send_abort(sdo, index, subindex, abort_for_status(CO_ERR_OD_LENGTH, requested_length, entry->length));
    /*16：调用阶段 2 的写入接口*/
    status = co_od_write(entry, sdo->node->state, request->data + 4u, requested_length);
    /*17：写入失败时，把阶段2返回的内部错误转换成Abort码，组装并发送SDO Abort响应。*/
    if (status != CO_OK)
        return send_abort(sdo, index, subindex, abort_for_status(status, requested_length, entry->length));
    /*18：组装并发送写入成功响应*/
    response = response_base(sdo, index, subindex);
    response.data[0] = CO_SDO_CMD_DOWNLOAD_OK;
    return co_send(sdo->node, &response);
}
```



```c
整个函数的主流程
co_sdo_receive()
        │
        ▼
validate(sdo)
        │
        ▼
co_classify_rx()
        │
        ▼
是本节点 SDO 且状态允许吗？
        │
        ├── 否 → CO_IGNORED
        │
        ▼
DLC 是否为 8？
        │
        ├── 否 → CO_ERR_DLC
        │
        ▼
解析命令、Index、Sub-index
        │
        ├── 0x40 Upload
        │      ├── 查找对象
        │      ├── co_od_read()
        │      ├── 选择 0x4F/4B/47/43
        │      └── 发送读取响应
        │
        └── 0x2F/2B/27/23 Download
               ├── 确定写入长度
               ├── 查找对象
               ├── 检查可写权限
               ├── 检查长度
               ├── co_od_write()
               ├── 失败 → Abort
               └── 成功 → 0x60 响应
这一个函数把前面阶段全部串起来：
阶段1：
co_classify_rx()
co_send()

阶段2：
co_od_find()
co_od_read()
co_od_write()
对象条目和实际数据

阶段3：
node->state
决定 SDO 是否允许服务，以及对象写入状态限制

阶段4：
0x1017 对象可通过 SDO 读取或修改
Heartbeat 下一次运行时读取新配置

阶段5：
co_sdo_receive()
把 CAN 报文正式连接到对象字典
因此，SDO 的核心就是：
收到一帧 CAN 请求
    ↓
判断读还是写
    ↓
访问指定的 Index:Sub-index
    ↓
返回成功数据、写入确认或 Abort 错误
```



# 总览：

```c
validate()
    检查 SDO 上下文

active()
    判断当前 NMT 状态是否允许 SDO

abort_for_status（）
       转错误码函数，阶段2内部的错误码，转为标准SDO错误码
find_entry()
    区分 Index 不存在和 Sub-index 不存在

response_base()
    创建 SDO 响应帧公共部分

send_abort()
    组装并发送 Abort 响应

upload_command()
    根据对象长度选择读取成功命令字

co_sdo_init()
    绑定 node 和 device

co_sdo_receive()
    完整处理 Upload、Download、正常响应和 Abort
```













