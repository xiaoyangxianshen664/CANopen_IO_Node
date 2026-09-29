# 解析：

## 1：模拟发送环境结构体

```c
typedef struct
{
    unsigned sends;
    co_status_t result;
    can_frame_t frame;
} fake_t;
```

测试中会创建：

```
fake_t fake = {0};
```

然后把它交给节点的发送回调：

```
co_init(&node, 2, tx, &fake);
```

这样 `co_hb_process()` 最终调用：

```
co_send()
    ↓
node->tx()
    ↓
tx(&fake, &frame)
```

`fake_t` 的三个成员分别负责不同任务。





#### 1. `unsigned sends`

```
unsigned sends;
```

记录发送回调被调用了多少次。

模拟发送函数中：

```
++f->sends;
```

每当 Heartbeat 调用发送入口，就加 1。

例如：

```
co_hb_process(&hb, 999);
```

如果还没到发送周期：

```
fake.sends = 0
```

再调用：

```
co_hb_process(&hb, 1);
```

达到周期后：

```
fake.sends = 1
```

测试可以用它确认：

```
CHECK(fake.sends == 1u);
```

也可以确认关闭 Heartbeat 后没有发送：

```
CHECK(fake.sends == 1u);
```



#### 2. `co_status_t result`

```
co_status_t result;
```

这是测试预先设置的“模拟发送结果”。

模拟回调最后会返回：

```
return f->result;
```

因此测试可以指定发送成功或失败：

```
fake.result = CO_OK;
```

表示模拟发送成功。

```
fake.result = CO_ERR_TX_BUSY;
```

表示模拟传输层忙。

```
fake.result = CO_ERR_TX_FAILED;
```

表示模拟发送失败。

这样就能检查 Heartbeat 的错误处理：

```
fake.result = CO_ERR_TX_BUSY;
CHECK(co_hb_process(&hb, 500) == CO_ERR_TX_BUSY);
```

它测试的是：

```c
Heartbeat 到期
    ↓
调用发送
    ↓
模拟发送层返回 TX_BUSY
    ↓
Heartbeat 把错误返回给调用者
```





#### 3. `can_frame_t frame`

```
can_frame_t frame;
```

用于保存 Heartbeat 实际提交的报文副本。

模拟发送函数中通常会有：

```
f->frame = *frame;
```

这表示把发送函数收到的报文完整复制到：

```
fake.frame
```

之后测试就可以检查报文内容：

```c
CHECK(fake.frame.id == 0x702u);
CHECK(fake.frame.dlc == 1u);
CHECK(fake.frame.data[0] == CO_NMT_PRE_OPERATIONAL);
```

也可以检查帧类型：

```c
CHECK(!fake.frame.is_extended);
CHECK(!fake.frame.is_remote);
CHECK(!fake.frame.is_fd);
```



三个成员配合起来

```c
fake.sends
    判断发送回调被调用了几次

fake.result
    预先决定发送回调返回成功还是失败

fake.frame
    保存实际提交的最后一帧报文
```

测试链路是：

```c
co_hb_process(&hb, 500)
        ↓
判断 Heartbeat 到期
        ↓
组装 frame
        ↓
co_send(&node, &frame)
        ↓
tx(&fake, &frame)
        ├── sends 加 1
        ├── 保存 frame 副本
        └── 返回 fake.result
```

所以这个模拟结构体同时完成三件事：

```
记录是否发送
控制发送结果
检查发送内容
```

它不是真正的 CAN 驱动，只是测试用的“假发送环境”。





## 2：模拟发送函数

```c
static co_status_t tx(void *user, const can_frame_t *frame)
{
    fake_t *f = user;
    ++f->sends;
    f->frame = *frame;
    return f->result;
}
```

这个 `tx()` 是测试专用的模拟发送回调。它模拟真实 CAN 驱动的发送函数，但实际上不连接 CAN 总线，只记录数据并返回预设结果。

函数名对应阶段 1 定义的发送回调类型：

```c
typedef co_status_t (*co_tx_fn)(void *user, const can_frame_t *frame);
```

所以它可以传给：

```c
co_init(&node, 2, tx, &fake);
```

这里的绑定关系是：

```
tx       → 保存函数地址
&fake    → 保存测试结构体地址
```

之后 Heartbeat 发送时：

```c
co_hb_process()
    ↓
co_send()
    ↓
node->tx(node->tx_user, &frame)
    ↓
tx(&fake, &frame)
```







#### 1. `void *user`

```
void *user
```

这是通用的用户数据指针。

初始化节点时传入：

```
fake_t fake = {0};

co_init(&node, 2, tx, &fake);
```

`&fake` 的类型是：

```
fake_t *
```

但 `co_tx_fn` 规定参数类型是：

```
void *
```

C 允许把对象指针转换为 `void *`，所以可以传入。

到了 `tx()` 内部，再转换回原来的类型：

```
fake_t *f = user;
```

这句意思是：

> 把通用的 `void *` 还原成测试使用的 `fake_t *`。

于是后面可以访问：

```
f->sends
f->frame
f->result
```



#### 2. `const can_frame_t *frame`

```
const can_frame_t *frame
```

这是要发送的 CAN 帧地址。

Heartbeat 组装好帧后：

```
frame.id = 0x700 + node_id;
frame.dlc = 1;
frame.data[0] = node->state;
```

然后调用：

```
co_send(node, &frame);
```

最后传入：

```
tx(&fake, &frame);
```

`const` 表示 `tx()` 不应该修改发送帧：

```
frame->id = ...;       // 不允许
frame->dlc = ...;      // 不允许
frame->data[0] = ...;  // 不允许
```

模拟发送函数只读取这帧，然后保存一份副本。



#### 3. 发送次数加一

```
++f->sends;
```

这句把模拟发送次数加 1。

例如初始：

```
f->sends = 0
```

Heartbeat 到期并调用 `tx()` 后：

```
f->sends = 1
```

如果连续发送三次：

```
f->sends = 3
```

测试可以据此判断 Heartbeat 是否真的调用了发送回调：

```
CHECK(fake.sends == 1u);
```

注意它统计的是：

> 发送回调被调用的次数。

它不是：

> 报文已经成功到达 CAN 总线的次数。

因为回调可能返回：

```
CO_ERR_TX_BUSY
```

此时回调已经被调用，`sends` 仍然会加 1，但发送并没有成功。



#### 4. 保存报文副本

```
f->frame = *frame;
```

这里有两个不同的东西：

```
frame
    指向原始 CAN 帧的指针

*frame
    原始 CAN 帧结构体本身

f->frame
    fake_t 中保存的 CAN 帧成员
```

所以：

```
f->frame = *frame;
```

表示：

> 把发送帧中的所有成员复制到测试结构体里。

相当于复制：

```
id
dlc
data[0..7]
is_extended
is_remote
is_fd
```

这样 `tx()` 返回以后，测试仍然可以检查：

```
fake.frame.id
fake.frame.dlc
fake.frame.data[0]
```

如果只保存指针：

```
f->frame_pointer = frame;
```

原始局部变量生命周期结束后，指针可能失效。因此这里保存完整副本更安全。



#### 5. 返回预先设置的结果

```
return f->result;
```

测试代码可以在调用前设置：

```
fake.result = CO_OK;
```

模拟发送成功。

也可以设置：

```
fake.result = CO_ERR_TX_BUSY;
```

模拟发送层暂时繁忙。

或：

```
fake.result = CO_ERR_TX_FAILED;
```

模拟传输失败。

Heartbeat 的调用链会原样得到这个返回值：

```
tx() 返回 CO_ERR_TX_BUSY
        ↓
co_send() 返回 CO_ERR_TX_BUSY
        ↓
co_hb_process() 返回 CO_ERR_TX_BUSY
```

因此测试可以验证：

```
fake.result = CO_ERR_TX_BUSY;
CHECK(co_hb_process(&hb, 500) == CO_ERR_TX_BUSY);
```



#### 6. 这段函数整体做了什么？

```
static co_status_t tx(void *user, const can_frame_t *frame)
{
    fake_t *f = user;      // 找回测试环境
    ++f->sends;            // 记录调用次数
    f->frame = *frame;     // 保存发送帧副本
    return f->result;      // 返回预设发送结果
}
```

可以记成：

```
找回 fake_t
    ↓
发送次数加一
    ↓
保存报文副本
    ↓
返回预设结果
```



#### 示例：

先创建一个模拟发送环境：

```
fake_t fake = {0};
```

初始化后，它的成员是：

```
fake.sends = 0
fake.result = 0，也就是 CO_OK
fake.frame = 全部清零
```

然后把 `tx()` 和 `fake` 绑定到节点：

```
co_context_t node = {0};

co_init(&node, 2, tx, &fake);
```

这句调用完成后，节点内部大致保存：

```
node.node_id = 2
node.tx      = tx
node.tx_user = &fake
```

也就是说：

```c
node.tx      保存 tx 函数地址
node.tx_user 保存 fake 结构体地址
```



现在假设 Heartbeat 已经组装好了这帧：

```c
can_frame_t frame = {0};

frame.id = 0x702;
frame.dlc = 1;
frame.data[0] = CO_NMT_PRE_OPERATIONAL;
```

此时帧内容是：

```c
CAN-ID = 0x702
DLC    = 1
Data   = 0x7F
```

Heartbeat 通过统一发送入口提交：

```
co_send(&node, &frame);
```

`co_send()` 内部最后会调用：

```
node->tx(node->tx_user, &frame);
```

把节点成员替换进去，就是：

```
tx(&fake, &frame);
```

进入 `tx()` 函数：

```c
static co_status_t tx(void *user, const can_frame_t *frame)
{
    fake_t *f = user;
    ++f->sends;
    f->frame = *frame;
    return f->result;
}
```





##### fake_t *f = user;

调用时：

```c
user 实际上是 &fake
```

所以转换后：

```c
fake_t 结构体指针f 指向 fake
```

也就是：结构体可以有另外一种访问方式，用指针加->进行访问

```c
f->sends  等价于 fake.sends
f->result 等价于 fake.result
f->frame  等价于 fake.frame
```





##### ++f->sends;

原来：

```
fake.sends = 0
```

执行后：

```
fake.sends = 1
```

表示发送回调被调用了一次。





##### f->frame = *frame;

把传进来的发送帧复制到 `fake.frame`：

```c
fake.frame.id      = 0x702
fake.frame.dlc     = 1
fake.frame.data[0] = 0x7F
这句可以拆成三部分看：
f->frame = *frame;
1. frame 是什么？
在函数参数中：
static co_status_t tx(void *user, const can_frame_t *frame)
这里的 frame 是一个指针变量，类型是：
const can_frame_t *
它保存的是某个 can_frame_t 结构体变量的地址。
例如调用者先创建：
can_frame_t heartbeat_frame = {0};

heartbeat_frame.id = 0x702;
heartbeat_frame.dlc = 1;
heartbeat_frame.data[0] = 0x7F;
然后调用：
tx(&fake, &heartbeat_frame);
传入：
&heartbeat_frame
所以在 tx() 内部：
frame 保存 heartbeat_frame 的地址
可以画成：
frame ───────────────► heartbeat_frame
                       ├── id = 0x702
                       ├── dlc = 1
                       └── data[0] = 0x7F
2. *frame 是什么？
如果 frame 是地址：
frame
那么：
*frame
就是“沿着这个地址，找到它指向的那个结构体”。
也就是：
frame       = heartbeat_frame 的地址
*frame      = heartbeat_frame 这个结构体本身
因此：
frame->id
表示通过指针访问结构体成员。
而：
*frame
表示取得完整的 can_frame_t 结构体值，包括：
id
dlc
data[]
is_extended
is_remote
is_fd
```





##### return f->result;

如果之前设置的是：

```
fake.result = CO_OK;
```

那么：

```c
tx() 返回 CO_OK
co_send() 返回 CO_OK
Heartbeat 发送成功
```







## 3:设置周期函数

```c
static co_status_t set_period(co_device_od_t *device, uint16_t period)
{
    const co_od_entry_t *entry;
    uint8_t data[2] = {(uint8_t)period, (uint8_t)(period >> 8)};
    co_status_t status = co_od_find(&device->table, 0x1017, 0, &entry);
    return status == CO_OK ? co_od_write(entry, CO_NMT_PRE_OPERATIONAL,
                                         data, 2) : status;
}
```

这个 `set_period()` 是测试辅助函数，用来直接修改对象字典中的：

```
0x1017:00 = Producer Heartbeat Time
```

它模拟“主站把 Heartbeat 周期写入对象字典”的效果，但它本身不发送 SDO CAN 报文。



### 1. 函数参数

```
co_device_od_t *device
```

这是设备对象字典的地址。函数要在它里面查找：

```
Index    = 0x1017
Subindex = 0x00
uint16_t period
```

是测试想写入的新周期，例如：

```
set_period(&device, 500);
```

表示把心跳周期设置为：

```
500 ms
```



### 2. 找到对象条目的指针

```
const co_od_entry_t *entry;
```

这里定义一个指针变量，用来接收找到的对象条目地址。

调用：

```
co_od_find(&device->table, 0x1017, 0, &entry);
```

参数含义是：

```
&device->table   要查找的对象表
0x1017           Index
0                Sub-index
&entry           把找到的条目地址写到 entry
```

如果查找成功：

```
entry ─────► device->entries 中的 0x1017:00 条目
```

如果查找失败，`entry` 不会用于写入，函数会直接返回错误。





### 3. 把 `period` 拆成两个字节

```c
uint8_t data[2] = { (uint8_t)period, (uint8_t)(period >> 8)  };
```

`co_od_write()` 接收的是字节数组：

```
const uint8_t *data
```

而 `period` 是一个两字节的 `uint16_t`，所以要先拆成两个字节。

CANopen 项目使用小端顺序：

```
低字节在前，高字节在后
```

例如：

```
period = 500;
```

十六进制是：

```
500 = 0x01F4
```

拆分后：

```
period                 = 0x01F4
(uint8_t)period        = 0xF4
period >> 8            = 0x01
```

因此：

```
data[0] = 0xF4;
data[1] = 0x01;
```

传给对象写入层的就是：

```
F4 01
```

对象层按小端解码后重新得到：

```
0x01F4 = 500
```

再例如：

```
set_period(&device, 1000);
```

因为：

```
1000 = 0x03E8
```

所以：

```
data[0] = 0xE8
data[1] = 0x03
```



### 4. 查找对象

```c
co_status_t status =co_od_find(&device->table, 0x1017, 0, &entry);
```

查找结果保存到 `status`：

```c
CO_OK                 找到了 0x1017:00
CO_ERR_OD_NOT_FOUND   没找到这个对象
其他错误              对象表或参数有问题
```





### 5. 条件运算符

```c
return status == CO_OK
    ? co_od_write(entry, CO_NMT_PRE_OPERATIONAL, data, 2)
    : status;
```

这是三目运算符，等价于：

```c
if (status == CO_OK)
{
    return co_od_write(entry,
                       CO_NMT_PRE_OPERATIONAL,
                       data,
                       2);
}
else
{
    return status;
}
```

也就是：

```c
查找成功
    ↓
调用 co_od_write() 写入 0x1017:00

查找失败
    ↓
直接返回查找错误
```

------



### 6. 为什么传入 `CO_NMT_PRE_OPERATIONAL`？

```
co_od_write(entry, CO_NMT_PRE_OPERATIONAL, data, 2)
```

第二个参数是“执行这次对象写入时，调用者声明的当前 NMT 状态”。

当前 `0x1017:00` 的定义允许写入，而且不要求只能在 Pre-operational 状态，所以这里传入：

```
CO_NMT_PRE_OPERATIONAL
```

主要是为了按测试中的配置阶段状态执行对象访问检查。

它表示：

> 测试现在模拟节点处于 Pre-operational，正在配置 Heartbeat 周期。

这不是在这里修改：

```
device->state
```

也不是让真实节点切换状态，更不会发送 NMT 报文。

------



### 7. 最后一个参数 `2`

```
co_od_write(entry, CO_NMT_PRE_OPERATIONAL, data, 2)
```

最后的 `2` 是：

```
本次 data 缓冲区中有 2 个有效字节
```

因为：

```
0x1017:00 类型：UNSIGNED16
对象长度：2 字节
```

所以这里必须传：

```
data, 2
```

如果错误地传：

```
data, 1
```

对象访问层会返回长度错误：

```
CO_ERR_OD_LENGTH
```

------



### 8. 一次完整调用

```
set_period(&device, 500);
```

内部流程：

```c
period = 500
    ↓
拆成 data = {0xF4, 0x01}
    ↓
查找 device.table 中的 0x1017:00
    ↓
找到 entry
    ↓
调用 co_od_write()
    ↓
对象层检查权限、长度、状态和值
    ↓
写入 0x1017:00 = 500
    ↓
对应 fields 的 writes 加 1
```

所以它和真实 SDO 写入的关系是：

```c
真实运行：
主站 SDO 请求
    ↓
SDO 服务解析
    ↓
co_od_find()
    ↓
co_od_write()

当前单元测试：
set_period()
    ↓
co_od_find()
    ↓
co_od_write()
```

测试跳过了 CAN 报文和 SDO 解析，直接调用对象访问层，专门准备 Heartbeat 测试所需的周期值。





## 4:主测试函数

```c
int main(void)
{
    co_context_t node = {0};
    co_device_od_t device = {0};
    co_hb_t hb = {0};
    fake_t fake = {0};

    CHECK(co_hb_init(NULL, &node, &device) == CO_ERR_ARGUMENT);
    CHECK(co_hb_process(NULL, 1) == CO_ERR_ARGUMENT);
    CHECK(co_init(&node, 2, tx, &fake) == CO_OK);
    CHECK(co_device_od_init(&device, 2, NULL) == CO_OK);
    CHECK(co_hb_init(&hb, &node, &device) == CO_OK);

    CHECK(co_hb_process(&hb, 5000) == CO_IGNORED);
    CHECK(fake.sends == 0u);
    node.state = CO_NMT_PRE_OPERATIONAL;
    CHECK(co_hb_process(&hb, 999) == CO_IGNORED);
    CHECK(fake.sends == 0u);
    CHECK(co_hb_process(&hb, 1) == CO_OK);
    CHECK(fake.sends == 1u && fake.frame.id == 0x702u && fake.frame.dlc == 1u);
    CHECK(fake.frame.data[0] == CO_NMT_PRE_OPERATIONAL);

    node.state = CO_NMT_OPERATIONAL;
    CHECK(co_hb_process(&hb, 1000) == CO_OK);
    CHECK(fake.frame.data[0] == CO_NMT_OPERATIONAL);
    CHECK(set_period(&device, 0) == CO_OK);
    CHECK(co_hb_process(&hb, 5000) == CO_IGNORED && fake.sends == 2u);
    CHECK(set_period(&device, 500) == CO_OK);
    CHECK(co_hb_process(&hb, 499) == CO_IGNORED);
    CHECK(co_hb_process(&hb, 1) == CO_OK && fake.sends == 3u);

    fake.result = CO_ERR_TX_BUSY;
    CHECK(co_hb_process(&hb, 500) == CO_ERR_TX_BUSY && fake.sends == 4u);
    fake.result = CO_OK;
    CHECK(co_hb_process(&hb, 0) == CO_OK && fake.sends == 5u);

    CHECK(set_period(&device, 500) == CO_OK); /* same value still restarts */
    CHECK(co_hb_process(&hb, 499) == CO_IGNORED);
    CHECK(co_hb_process(&hb, 1) == CO_OK && fake.sends == 6u);
    CHECK(set_period(&device, 0) == CO_OK);
    CHECK(co_hb_process(&hb, 500) == CO_IGNORED);
    puts("Heartbeat: timing, state byte, disable, rewrite and retry passed");
    return 0;
}
```

这个 `main()` 是按场景顺序验证 Heartbeat Producer 的完整行为。它没有使用真实 CAN，而是用 `fake` 记录发送结果和报文



##### 1. 创建测试对象

```c
co_context_t node = {0};
co_device_od_t device = {0};
co_hb_t hb = {0};
fake_t fake = {0};
```

分别是：

```c
node   阶段 1 的节点上下文
device 阶段 2 的设备对象字典
hb     阶段 4 的 Heartbeat 对象
fake   模拟发送环境
```

初始化时它们都先清零。

`fake` 清零后：

```c
fake.sends  = 0
fake.result = CO_OK
fake.frame  = 全部为 0
```

因为 `CO_OK` 的枚举值是 0。

##### 

##### 2. 测试无效参数

```c
CHECK(co_hb_init(NULL, &node, &device) == CO_ERR_ARGUMENT);
CHECK(co_hb_process(NULL, 1) == CO_ERR_ARGUMENT);
```

第一句测试：

```
Heartbeat 初始化目标 hb 为空
```

第二句测试：

```
处理函数传入的 hb 为空
```

两种情况都应该返回：

```
CO_ERR_ARGUMENT
```

这两项测试验证的是接口对空指针的保护。







#####  3.初始化节点和对象字典

```
CHECK(co_init(&node, 2, tx, &fake) == CO_OK);
CHECK(co_device_od_init(&device, 2, NULL) == CO_OK);
CHECK(co_hb_init(&hb, &node, &device) == CO_OK);
```

第一句：

```
co_init(&node, 2, tx, &fake)
```

建立节点上下文：

```
Node-ID  = 2
发送回调 = tx
回调参数 = &fake
状态     = Initialization
```

因此以后发送 Heartbeat 时，CAN-ID 会是：

```
0x700 + 2 = 0x702
```

第二句初始化设备对象字典：

```
0x1017:00 = 默认 1000 ms
```

第三句初始化 Heartbeat 对象：

```
hb.node            = &node
hb.device          = &device
hb.elapsed_ms      = 0
hb.observed_writes = 当前 0x1017 的写入次数
```





##### 4. Initialization 状态下不发送 Heartbeat

```
CHECK(co_hb_process(&hb, 5000) == CO_IGNORED);
CHECK(fake.sends == 0u);
```

此时 `node.state` 仍然是：

```
CO_NMT_INITIALIZATION
```

即使传入：

```
5000 ms
```

也不会发送，因为 Initialization 阶段的：

```
Data = 0x00
```

是 Boot-up 报文专用值，不能当成周期 Heartbeat 重复发送。

所以结果是：

```
返回 CO_IGNORED
发送次数仍为 0
```

这次调用还会把：

```
hb.elapsed_ms
```

清零。



##### 5. 进入 Pre-operational，测试未到期和刚好到期

```
node.state = CO_NMT_PRE_OPERATIONAL;
```

这里直接修改状态，是因为本测试专注 Heartbeat，不测试 NMT 状态切换。真实流程中，这个状态通常由阶段 3 的 Boot-up 完成后产生。

默认周期是：

```
period = 1000 ms
```

先调用：

```
CHECK(co_hb_process(&hb, 999) == CO_IGNORED);
CHECK(fake.sends == 0u);
```

累计：

```
hb->elapsed_ms = 999
```

但：

```
999 < 1000
```

所以还不到发送时间。

再调用：

```
CHECK(co_hb_process(&hb, 1) == CO_OK);
```

这次累计后：

```
999 + 1 = 1000
```

达到周期，于是发送 Heartbeat。

发送次数变为：

```
fake.sends = 1
```

检查报文：

```
CHECK(fake.sends == 1u &&
      fake.frame.id == 0x702u &&
      fake.frame.dlc == 1u);
```

验证：

```
CAN-ID = 0x702
DLC    = 1
```

再检查数据：

```
CHECK(fake.frame.data[0] == CO_NMT_PRE_OPERATIONAL);
```

确认报文中的状态字节是：

```
0x7F
```

即 Pre-operational。





##### 6. 切换到 Operational，检查状态字节会变化

```
node.state = CO_NMT_OPERATIONAL;
CHECK(co_hb_process(&hb, 1000) == CO_OK);
CHECK(fake.frame.data[0] == CO_NMT_OPERATIONAL);
```

前一次发送成功后：

```
hb->elapsed_ms = 0
```

这次经过：

```
1000 ms
```

再次到期，于是发送第二帧。

CAN-ID 仍然是：

```
0x702
```

但数据字节变成：

```
0x05
```

因为当前节点状态已经改成：

```
CO_NMT_OPERATIONAL
```

这里测试的是：

> Heartbeat 的 CAN-ID 不变，但数据字节会随当前 NMT 状态变化。





##### 7. 周期设置为 0，关闭 Heartbeat

```
CHECK(set_period(&device, 0) == CO_OK);
CHECK(co_hb_process(&hb, 5000) == CO_IGNORED &&
      fake.sends == 2u);
```

第一句把：

```
0x1017:00 = 0
```

表示关闭 Heartbeat。

第二句即使传入：

```
5000 ms
```

也不会发送，因为周期为 0。

检查结果：

```
返回 CO_IGNORED
发送次数仍为 2
```





##### 8. 改为 500 ms，测试新周期

```
CHECK(set_period(&device, 500) == CO_OK);
```

现在：

```
0x1017:00 = 500 ms
```

由于对象被写入，`writes` 增加，Heartbeat 会发现写入次数变化，并清零计时。

然后：

```
CHECK(co_hb_process(&hb, 499) == CO_IGNORED);
```

累计 499 ms，还没到 500 ms。

再调用：

```
CHECK(co_hb_process(&hb, 1) == CO_OK &&
      fake.sends == 3u);
```

累计正好达到 500 ms，发送第三帧。



##### 9. 模拟发送忙，检查失败重试

```
fake.result = CO_ERR_TX_BUSY;
```

把模拟发送结果改成：

```
发送层忙，暂时不能提交报文
```

然后：

```
CHECK(co_hb_process(&hb, 500) == CO_ERR_TX_BUSY &&
      fake.sends == 4u);
```

这次已经达到 500 ms，所以会调用 `tx()`：

```
fake.sends 从 3 增加到 4
tx() 返回 CO_ERR_TX_BUSY
co_hb_process() 原样返回 CO_ERR_TX_BUSY
```

因为发送失败，代码不会执行：

```
hb->elapsed_ms %= period;
```

所以计时器仍然保持“已经到期”的状态，等待重试

```c
Heartbeat 已到期
    ↓
co_hb_process() 确实调用了发送函数
    ↓
tx() 记录 sends 加 1
    ↓
tx() 返回 CO_ERR_TX_BUSY
    ↓
本次发送提交失败
```

`tx()` 内部：

```
++f->sends;       // sends 从 3 变成 4
f->frame = *frame;
return f->result; // 返回 CO_ERR_TX_BUSY
```

因此：

```c
发送回调调用次数：3 → 4
本次发送结果：失败
hb->elapsed_ms：仍保持 500，没有清零
```







##### 10. 发送恢复成功，立即重试

```
fake.result = CO_OK;
CHECK(co_hb_process(&hb, 0) == CO_OK &&
      fake.sends == 5u);
```

这次传入新增时间为：

```
0 ms
```

但仍然会发送，因为上一次发送失败后，`hb->elapsed_ms` 仍处于到期状态。

于是：

```
再次调用发送回调
发送成功
fake.sends = 5
```

```c
接着恢复发送结果：
fake.result = CO_OK;
co_hb_process(&hb, 0);
虽然这次新增时间是：
0 ms
但之前失败时计时器仍然是：
hb->elapsed_ms = 500
仍然满足：
500 >= period(500)
所以函数立即再次调用：
tx(&fake, &frame);
这次：
sends：4 → 5
result：CO_OK
发送成功后才执行：
hb->elapsed_ms %= period;
得到：
500 % 500 = 0
```











##### 11. 同值重写也会重新计时

```
CHECK(set_period(&device, 500) == CO_OK); /* same value still restarts */
```

当前周期本来就是：

```
500 ms
```

这里又写入一次 500。

虽然数值没有改变，但对象的写入次数会增加：

```
writes 发生变化
```

Heartbeat 因此清零：

```
hb->elapsed_ms = 0
```

接着：

```
CHECK(co_hb_process(&hb, 499) == CO_IGNORED);
CHECK(co_hb_process(&hb, 1) == CO_OK &&
      fake.sends == 6u);
```

重新累计：

```
499 + 1 = 500
```

到期后发送第六帧。

这验证的是：

> 只要 Heartbeat 周期对象被重新写入，即使写入的还是相同数值，也重新开始计时。



##### 12. 再次关闭 Heartbeat

```
CHECK(set_period(&device, 0) == CO_OK);
CHECK(co_hb_process(&hb, 500) == CO_IGNORED);
```

再次将：

```
0x1017:00 = 0
```

然后即使经过 500 ms，也不会发送。

最终输出：

```
puts("Heartbeat: timing, state byte, disable, rewrite and retry passed");
```

表示所有测试场景通过。