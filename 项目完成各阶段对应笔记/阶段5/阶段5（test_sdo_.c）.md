## 1：断言宏

```c
#define CHECK(x) \
    do { \
        if (!(x)) { \
            fprintf(stderr, "%d: %s\n", __LINE__, #x); \
            return 1; \
        } \
    } while (0)
```



## 2：模拟发送环境结构体声明

```c
typedef struct
{
    unsigned sends;
    co_status_t result;
    can_frame_t frame;
} fake_t;
```

这是 SDO 测试使用的模拟发送环境。

测试中创建：

```
fake_t fake = {0};
```

三个成员分别表示：

```c
sends
    发送回调被调用了多少次

result
    模拟发送回调要返回什么结果

frame
    保存最近一次提交的 CAN 报文副本
```



#### `sends`

例如：

```
fake.sends = 0
```

表示还没有调用发送回调。

SDO 发送一帧响应后：

```
fake.sends = 1
```

它记录的是“发送函数被调用几次”，不代表每次都成功。



#### `result`

测试可以提前设置：

```
fake.result = CO_OK;
```

表示模拟发送成功。

也可以设置：

```
fake.result = CO_ERR_TX_BUSY;
```

模拟发送层繁忙。

SDO 调用：

```
co_send(sdo->node, &response);
```

最终会进入测试回调，回调再返回这个预先设置的结果。



#### `frame`

```
can_frame_t frame;
```

用于保存最后一次发送的 SDO 响应帧。

测试回调通常会执行：

```c
f->frame = *frame;
```

于是测试可以检查：

```c
fake.frame.id
fake.frame.dlc
fake.frame.data[0]
fake.frame.data[1]
```

例如验证读取 `0x1017:00` 的响应：

```c
CHECK(fake.frame.id == 0x581u);
CHECK(fake.frame.dlc == 8u);
CHECK(fake.frame.data[0] == CO_SDO_CMD_UPLOAD_2);
CHECK(fake.frame.data[1] == 0x17u);
CHECK(fake.frame.data[2] == 0x10u);
```

------

三个成员串起来就是：

```
SDO 组装响应帧
        ↓
co_send()
        ↓
测试发送回调
        ├── sends 加 1
        ├── frame 保存响应副本
        └── 返回 result
```

因此：

```
fake.sends  → 是否调用了发送回调
fake.result → 这次模拟发送成功还是失败
fake.frame  → 实际提交的响应内容是否正确
```

这个结构体不是真正的 CAN 驱动，只是为了让测试能够同时观察：

```
有没有发送
发送了什么
发送结果是什么
```



## 3：模拟发送tx回调函数

```c
static co_status_t tx(void *user, const can_frame_t *frame)
{
    fake_t *f = user;
    ++f->sends;
    f->frame = *frame;
    return f->result;
}
```

这个 `tx()` 和前面阶段的模拟发送回调作用相同，专门给 SDO 测试使用。



#### 1. `user` 转回 `fake_t *`

节点初始化时：

```
fake_t fake = {0};
co_init(&node, 1, tx, &fake);
```

`co_init()` 会保存：

```
node.tx      = tx
node.tx_user = &fake
```

以后 `co_send()` 内部调用：

```
node->tx(node->tx_user, frame);
```

实际就是：

```
tx(&fake, frame);
```

因此：

```
fake_t *f = user;
```

把通用的 `void *` 还原为：

```
fake_t *
```

之后：

```
f->sends
f->frame
f->result
```

分别就是：

```
fake.sends
fake.frame
fake.result
```



#### 2. 记录发送回调调用次数

```
++f->sends;
```

每当 SDO 响应调用一次发送回调，计数加 1。

例如：

```
第一次发送 → sends = 1
第二次发送 → sends = 2
```

如果 SDO 请求被忽略，或者 DLC 错误在发送前返回，那么 `tx()` 不会被调用，`sends` 也不会增加。





#### 3. 保存响应帧副本

```
f->frame = *frame;
```

这里：

```
frame  是当前收到的发送帧地址
*frame 是当前 CAN 帧结构体本身
f->frame 是 fake_t 中保存报文的成员
```

所以这句把 SDO 实际提交的响应帧完整复制到：

```
fake.frame
```

复制后测试可以检查：

```
fake.frame.id
fake.frame.dlc
fake.frame.data[0]
```

例如：

```
SDO 响应：
0x581 | 8 | 4B 17 10 00 E8 03 00 00
```

保存后：

```
fake.frame.id      = 0x581
fake.frame.dlc     = 8
fake.frame.data[0] = 0x4B
fake.frame.data[1] = 0x17
fake.frame.data[2] = 0x10
fake.frame.data[3] = 0x00
fake.frame.data[4] = 0xE8
fake.frame.data[5] = 0x03
```



#### 4. 返回预设发送结果

```
return f->result;
```

测试可以提前设置：

```
fake.result = CO_OK;
```

模拟发送成功，或者：

```
fake.result = CO_ERR_TX_BUSY;
```

模拟传输层繁忙。

调用链是：

```
co_sdo_receive()
    ↓
co_send()
    ↓
tx(&fake, &response)
    ├── sends 加 1
    ├── frame 保存响应副本
    └── 返回 fake.result
```

如果：

```
fake.result = CO_ERR_TX_BUSY;
```

那么：

```
tx() 返回 CO_ERR_TX_BUSY
co_send() 返回 CO_ERR_TX_BUSY
co_sdo_receive() 也返回 CO_ERR_TX_BUSY
```

因此这个回调的功能就是：

```
记录调用次数
保存最后发送的 SDO 响应
模拟成功或失败的发送结果
```

它不连接真实 CAN 硬件，只是测试用的“假发送函数”。







## 4:构造一帧固定格式的 SDO 请求帧函数

```c
static void request(can_frame_t *f, uint8_t command, uint16_t index,
                    uint8_t subindex)
{
    *f = (can_frame_t){0};
    f->id = 0x601;
    f->dlc = 8;
    f->data[0] = command;
    f->data[1] = (uint8_t)index;
    f->data[2] = (uint8_t)(index >> 8);
    f->data[3] = subindex;
}
```

这个 `request()` 是测试辅助函数，用来快速构造一帧固定格式的 SDO 请求帧。它不发送报文，只负责把传入的 `can_frame_t` 变量填写好。



```c
can_frame_t req = {0};
request(&req, ...);
```

这里先创建一个报文帧结构体变量：

```
req
```

然后把它的地址传入：

```
&req
```

函数参数：

```
can_frame_t *f
```

接收到的就是 `req` 的地址。

执行：

```
*f = (can_frame_t){0};
```

可以理解为：

```
req = (can_frame_t){0};
```

也就是先把整个 `req` 结构体清零，包括：

```c
req.id
req.dlc
req.data[0] ~ req.data[7]
req.is_extended
req.is_remote
req.is_fd
```

随后再填写 SDO 请求帧的公共字段：

```c
f->id = 0x601;
f->dlc = 8;
f->data[0] = command;
f->data[1] = (uint8_t)index;
f->data[2] = (uint8_t)(index >> 8);
f->data[3] = subindex;
```

所以最后得到：

```c
Data[0] = 命令字
Data[1] = Index 低字节
Data[2] = Index 高字节
Data[3] = Sub-index
Data[4] ~ Data[7] = 暂时保持 0
```

例如：

```c
request(&req, CO_SDO_CMD_UPLOAD, 0x1017, 0);
```

得到：

```c
CAN-ID = 0x601
DLC    = 8
Data   = 40 17 10 00 00 00 00 00
```



#### 作用：

调用关系是：

```c
测试代码 request()
    ↓
假设构造主站can分析仪发给从机节点stm32的 SDO 请求帧
    ↓
co_sdo_receive(&sdo, &req)
    ↓
模拟 STM32 从机收到这帧
    ↓
STM32 SDO 服务解析并生成响应帧
```







## 5：从一帧 SDO Abort 响应中提取 4 字节错误码

```c
static uint32_t abort_code(const can_frame_t *f)
{
    return (uint32_t)f->data[4] |
           ((uint32_t)f->data[5] << 8) |
           ((uint32_t)f->data[6] << 16) |
           ((uint32_t)f->data[7] << 24);
}
```

这个 `abort_code()` 是测试辅助函数，用来从一帧 SDO Abort 响应中提取 4 字节错误码。



SDO Abort 错误响应帧格式：

```c
Data[0]    = 0x80，Abort 命令字
Data[1..2] = Index
Data[3]    = Sub-index
Data[4..7] = 4 字节 Abort code
```

例如：

```
Data = 80 99 99 00 00 00 02 06
```

其中：

```c
Data[4] = 0x00
Data[5] = 0x00
Data[6] = 0x02
Data[7] = 0x06
```

完整错误码应为：

```c
0x06020000
```

这个函数就是把错误码提取出来



#### 1. `f->data[4]`

```
(uint32_t)f->data[4]
```

取错误码最低字节。

例如：

```
f->data[4] = 0x00
```

转换成 32 位后：

```
0x00000000
```

它位于最终错误码的：

```
bit 0 ~ bit 7
```

------



#### 2. `f->data[5] << 8`

```
((uint32_t)f->data[5] << 8)
```

先把 `data[5]` 转成 `uint32_t`，再左移 8 位。

例如：

```
data[5] = 0x00
```

结果是：

```
0x00000000
```

如果：

```
data[5] = 0x12
```

结果是：

```
0x00001200
```

它位于最终错误码的：

```
bit 8 ~ bit 15
```

------



#### 3. `f->data[6] << 16`

```
((uint32_t)f->data[6] << 16)
```

例如：

```
data[6] = 0x02
```

结果：

```
0x00020000
```

它位于最终错误码的：

```
bit 16 ~ bit 23
```

------



#### 4. `f->data[7] << 24`

```
((uint32_t)f->data[7] << 24)
```

例如：

```
data[7] = 0x06
```

结果：

```
0x06000000
```

它位于最终错误码的：

```
bit 24 ~ bit 31
```

------



#### 5. 使用按位或组合

```
return value0 | value1 | value2 | value3;
```

这里的 `|` 是按位或，用来把四个字节放回各自位置。

以：

```
Data[4..7] = 00 00 02 06
```

为例：

```
(uint32_t)f->data[4]        = 0x00000000
(uint32_t)f->data[5] << 8   = 0x00000000
(uint32_t)f->data[6] << 16  = 0x00020000
(uint32_t)f->data[7] << 24  = 0x06000000
```

按位或：

```
0x00000000
| 0x00000000
| 0x00020000
| 0x06000000
= 0x06020000
```

所以函数返回：

```
0x06020000
```

------



#### 6. 为什么必须先转换成 `uint32_t`？

代码写成：

```
((uint32_t)f->data[7] << 24)
```

而不是：

```
f->data[7] << 24
```

是为了确保左移操作在 32 位无符号整数上进行。

因为：

```
f->data[7]
```

本身是 `uint8_t`，如果直接参与移位，可能先经过整型提升，容易让代码的位宽含义不够清楚。

强制转换后：

```
(uint32_t)f->data[7]
```

明确表示：

```
先作为 32 位无符号数，再向左移动
```

这样组合 4 字节错误码更安全、清晰。

------



#### 7. 在测试中的使用

测试收到模拟响应后会这样检查：

```
CHECK(co_sdo_receive(&sdo, &req) == CO_OK &&
      abort_code(&fake.frame) == 0x06020000u);
```

调用链：

```c
co_sdo_receive()
    ↓
发现对象不存在
    ↓
send_abort()
    ↓
fake.frame 保存 Abort 响应
    ↓
abort_code(&fake.frame)
    ↓
提取 Data[4..7]
    ↓
重新组合成 uint32_t
    ↓
与期望错误码比较
```

比如：

```
CHECK(abort_code(&fake.frame) == 0x06090011u);
```

就是验证：

```
SDO 服务返回的 Abort code 是否为“Sub-index 不存在”
```

所以这个函数只做一件事：

```
从响应帧的 Data[4]～Data[7]
按小端顺序恢复出完整的 32 位 Abort code
```

它不负责判断错误，也不发送报文，只是方便测试读取错误码。



## 6：main函数（）

这个 `main()` 按顺序测试了 SDO Server 的初始化、读取、写入、错误响应和状态限制。

```c
int main(void)
{
    /*1:创建测试对象*/
    co_context_t node = {0};     // 阶段1节点上下文
    co_device_od_t device = {0}; // 阶段2设备对象字典
    co_sdo_t sdo = {0};          // 阶段5 SDO服务对象，存两指针，指向node 和 device
    fake_t fake = {0};           // PC断模拟发送环境
    can_frame_t req = {0};       // 模拟主站发来的SDO请求帧
    /*2：测试空指针错误*/
    CHECK(co_sdo_init(NULL, &node, &device) == CO_ERR_ARGUMENT); // 没有提供保存初始化结果的 SDO 对象，返回CO_ERR_ARGUMENT
    CHECK(co_sdo_receive(NULL, &req) == CO_ERR_ARGUMENT);        // 没有提供有效的 SDO 上下文，所以也返回CO_ERR_ARGUMENT

    /*3：3. 初始化阶段 1、阶段 2 和阶段 5*/
    CHECK(co_init(&node, 1, tx, &fake) == CO_OK);        // 初始化节点：Node-ID  = 1，状态 = Initialization，发送函数 = tx，发送辅助数据 = &fake
    CHECK(co_device_od_init(&device, 1, NULL) == CO_OK); // 初始化对象字典，36个对象条目，36个数据存储结构体
    CHECK(co_sdo_init(&sdo, &node, &device) == CO_OK);   // 初始化 SDO，完成sdo.node → node 和sdo.device → device
    /*4: 构造读取 0x1017:00 的请求：主站请求 Node-ID=1 的节点读取 0x1017:00*/
    request(&req, CO_SDO_CMD_UPLOAD, 0x1017, 0);
    /*5：Initialization 状态下忽略 SDO，返回错误状态CO_IGNORED，帧与本节点无关，正常忽略，连SDO的状态门控都没通过，如果过了则是返回另外一个错误状态CO_ERR_OD_STATE*/
    CHECK(co_sdo_receive(&sdo, &req) == CO_IGNORED && fake.sends == 0u);
    /*6： 切换到 Pre-operational 后读取对象 ：测试直接修改节点状态，模拟阶段 3 已经完成 Boot-up，节点进入 Pre-operational*/
    node.state = CO_NMT_PRE_OPERATIONAL;
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK);                                   // 然后再次处理同一帧：
    CHECK(fake.frame.id == 0x581u && fake.frame.data[0] == CO_SDO_CMD_UPLOAD_2 && // 检查响应帧，我们返回给上位机的：并验证响应 ID  = 0x581，响应命令字  = 0x4B，读取成功并返回 2 字节，返回数据 = E8 03，即 1000
          fake.frame.data[4] == 0xE8u && fake.frame.data[5] == 0x03u);
    /*7. 写入新的 Heartbeat 周期*/
    request(&req, CO_SDO_CMD_DOWNLOAD_2, 0x1017, 0);
    req.data[4] = 0xF4;
    req.data[5] = 0x01;
    /*8：SDO 内部执行：找到 0x1017:00→确认可写→确认请求长度为 2→ 调用co_od_write()→对象值改为 500→返回 0x60（成功写入2字节内容），fake.frame.data[0] == CO_SDO_CMD_DOWNLOAD_OK是在验证Data[0] = 0x60，表示写入成功*/
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && fake.frame.data[0] == CO_SDO_CMD_DOWNLOAD_OK);
    /*9：再次读取，确认写入真的生效：重新构造读取请求，避免保留上一次 Download 命令。*/
    request(&req, CO_SDO_CMD_UPLOAD, 0x1017, 0);
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && fake.frame.data[4] == 0xF4u && fake.frame.data[5] == 0x01u); // 读取后检查Data[4] = F4 Data[5] = 01，确定对象字典的心跳周期从1000ms改为500ms
    /*10： 测试 Index 不存在，重新发送一个请求帧*/
    request(&req, CO_SDO_CMD_UPLOAD, 0x9999, 0);
    // 读取后检查，这里 co_sdo_receive() 返回 CO_OK，是因为：SDO 请求已经被正确处理，并成功发送了一帧 Abort 响应。CO_OK 不表示请求的对象存在，而表示服务处理和响应发送成功。
    // 同时检查：Data[0] = 0x80 和返回错误码Abort code = 0x06020000
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && fake.frame.data[0] == CO_SDO_CMD_ABORT && abort_code(&fake.frame) == 0x06020000u);
    /*11：测试 Sub-index 不存在*/
    request(&req, CO_SDO_CMD_UPLOAD, 0x6401, 3);
    // Index 存在但是Sub-index 不存在，返回错误码0x06090011u
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && abort_code(&fake.frame) == 0x06090011u);
    /*12： 测试数据长度过短*/
    request(&req, CO_SDO_CMD_DOWNLOAD_1, 0x1017, 0);                                      // 主站声明写入 1 字节但：0x1017:00 → UNSIGNED16，需要 2 字节，因此requested = 1，expected  = 2得到：requested < expected
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && abort_code(&fake.frame) == 0x06070013u); // 返回：ABORT_TOO_SHORT = 0x06070013
    /*13：测试写入只读对象*/
    request(&req, CO_SDO_CMD_DOWNLOAD_2, 0x6000, 1); // 对象：0x6000:01是数字输入对象，权限为：READ_ONLY，主站却使用 Download 命令尝试写入，所以返回：0x06010002
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && abort_code(&fake.frame) == 0x06010002u);
    /*14：测试 BOOLEAN 非法值*/
    request(&req, CO_SDO_CMD_DOWNLOAD_1, 0x6423, 0); // 对象：0x6423:00 类型是： BOOLEAN布尔值，开关用的，有效值只有：0 或 1 请求写入：2
    req.data[4] = 2;
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && abort_code(&fake.frame) == 0x06090030u); // 对象层：返回错误码co_od_write(...) == CO_ERR_OD_VALUE，SDO 层转换为：ABORT_VALUE = 0x06090030
    /*15：测试未知命令*/
    request(&req, 0x41, 0x1017, 0); // 0x41 不是当前支持的命令：0x2F → Download 1 字节 0x2B → Download 2 字节 0x27 → Download 3 字节 0x23 → Download 4 字节 ，这里命令虽然非法，但帧结构本身完整，所以可以正常生成 Abort 响应。
    // 这里命令虽然非法，但帧结构本身完整，所以可以正常生成 Abort 响应。
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && abort_code(&fake.frame) == 0x05040001u); // 因此进入：default:return send_abort(..., ABORT_TOGGLE);返回：0x05040001 表示：命令无效或当前版本不支持
    /*16： 测试 DLC 错误*/
    req.dlc = 7;                                     // 当前 SDO 要求：DLC = 8，函数在解析命令前检查：if (request->dlc != 8u)  return CO_ERR_DLC;
    CHECK(co_sdo_receive(&sdo, &req) == CO_ERR_DLC); // 这和未知命令不同：未知命令：帧格式完整，可以返回 Abort，DLC 错误：请求格式不完整，不能可靠解析，所以直接返回内部错误
    /*17：Stopped 状态下忽略 SDO*/

    req.dlc = 8;                                     // 先把 DLC 恢复为合法的：
    node.state = CO_NMT_STOPPED;                     // 再把节点切换到：CO_NMT_STOPPED
    CHECK(co_sdo_receive(&sdo, &req) == CO_IGNORED); // Stopped 不在允许范围内，因此：请求被忽略 不查对象 不发送 Abort，直接返回CO_IGNORED
    /*18：测试结束*/
    puts("SDO: upload, download, abort mapping and state gating passed"); // 所有 CHECK() 都通过后打印成功信息，并返回：
    return 0;                                                             // 测试程序返回 0，表示整个 SDO 测试通过。
}
```

| 测试内容                  | 结果                |
| ------------------------- | ------------------- |
| 空 SDO 初始化             | `CO_ERR_ARGUMENT`   |
| 空 SDO 接收               | `CO_ERR_ARGUMENT`   |
| Initialization 处理请求   | `CO_IGNORED`        |
| Upload 读取 `0x1017:00`   | `0x4B`，返回 2 字节 |
| Download 写入 `0x1017:00` | `0x60`              |
| 再次读取确认写入          | 返回 `500`          |
| Index 不存在              | `0x06020000`        |
| Sub-index 不存在          | `0x06090011`        |
| 数据过短                  | `0x06070013`        |
| 写入只读对象              | `0x06010002`        |
| BOOLEAN 值非法            | `0x06090030`        |
| 未知命令                  | `0x05040001`        |
| DLC 不为 8                | `CO_ERR_DLC`        |
| Stopped 状态              | `CO_IGNORED`        |

```c
主站请求帧
    ↓
SDO 状态门控
    ↓
Upload / Download 分流
    ↓
对象字典访问
    ↓
正常响应或 Abort
    ↓
模拟发送回调
```

