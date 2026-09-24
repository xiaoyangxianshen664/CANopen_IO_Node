# 解析



## 1：断言

和阶段 1、2 的 `CHECK` 一样，是测试用的检查宏。正确写法是 `__LINE__`：

```c
#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "%d: %s\n", __LINE__, #x); \
        return 1; \
    } \
} while (0)
```

- `x` 为真：检查通过，继续执行。
- `x` 为假：向 `stderr` 输出**代码行号和表达式文本**，然后从当前测试函数返回 `1`。
- `__LINE__`：宏调用所在的行号。
- `#x`：把表达式转换成字符串。
- `do { ... } while (0)`：让整个宏作为一条语句使用。

例如：

```c
CHECK(node.state == CO_NMT_PRE_OPERATIONAL);
```

状态不符合预期时，就打印这一行的行号和 `node.state == CO_NMT_PRE_OPERATIONAL`，结束当前测试。





## 2：模拟测试状态结构体

```c#
typedef struct {
    unsigned sends, notifications;
    co_status_t result;
    can_frame_t frame;
    co_nmt_state_t state;
} fake_t;
```

可以创建一个实际变量：

```
fake_t my_bus = {0};
```

`my_bus` 的五个成员是：

```c
my_bus
├── sends			//记录发送回调被调用了多少次，它记录的是回调调用次数，不是一定成功的发送次数
├── notifications	//记录状态通知回调notify被调用了多少次，重复切换到同一个状态时，change_state() 会直接返回，不调用通知，所以计数不会增加。
├── result		   //这是测试预先设置的“发送回调返回值”。my_bus.result = CO_OK; my_bus.result = CO_ERR_TX_BUSY; 			 
├── frame           //my_bus.resultCO_ERR_TX_FAILED;  测试可以控制Boot-up发送成功  Boot-up发送忙  Boot-up发送失败
└── state		   //frame保存最近一次发送回调收到的 CAN 帧副本。
    			  //state记录状态通知回调收到的最近一次 NMT 状态。
my_bus
├── sends
│   └── tx() 每调用一次就加1
├── notifications
│   └── notify() 每调用一次就加1
├── result
│   └── tx() 返回这个预设结果
├── frame
│   └── 保存 tx() 收到的最近一帧
└── state
    └── 保存 notify() 收到的最近状态
```













## 3：发送回调函数

```c
static co_status_t tx(void *user, const can_frame_t *frame)
{
    fake_t *f = user;
    ++f->sends; f->frame = *frame;
    return f->result;
}
```

这个 `tx()` 是**测试用的模拟发送回调**：记录提交次数、保存报文副本，并返回预先设定的发送结果，不会真正发送 CAN 报文

用具体变量看，假设准备：

```c
fake_t my_bus = {0};
can_frame_t my_frame = {0};

my_frame.id = 0x701;
my_frame.dlc = 1;
my_frame.data[0] = 0x00;

my_bus.result = CO_OK;
```

调用：

```c
co_status_t status = tx(&my_bus, &my_frame);
```

传入后：

```
user  = &my_bus
frame = &my_frame
```

第一句：

```
fake_t *f = user;
```

用明确的 `fake_t *` 接住通用指针，所以：

```
f ────> my_bus
```

接着：

```
++f->sends;
```

相当于：

```
++my_bus.sends;
```

记录发送回调被调用了一次。**即使后面返回忙或失败，这次调用也会计数，所以它不是成功发送次数。**



然后：

```
f->frame = *frame;
```

相当于：

```
my_bus.frame = my_frame;
```

这里容易混淆的两边分别是：

```
f->frame   ：my_bus里面的CAN帧结构体成员
*frame     ：传入指针指向的实际CAN帧
```

这是**结构体整体复制**，保存的是报文内容，不是 `&my_frame` 地址。即使原来的局部帧之后不再存在，测试仍能检查保存的副本：

```
my_bus.frame.id       // 0x701
my_bus.frame.dlc      // 1
my_bus.frame.data[0]  // 0x00
```



最后：

```
return f->result;
```

返回测试预先设置的结果：

(**是测试代码自己提前设定一个结果，让模拟发送函数返回它。** `tx()` 不连接真实 CAN 硬件，因此不会真的检测“发送忙不忙”。)

目的是：人为制造不同的发送结果，检查 NMT 代码面对这些结果时是否处理正确。

比如我们想测试“发送忙时，节点能不能保持初始化状态”，就分三步。

**第一步：人为设置发送忙。**

```c
my_bus.result = CO_ERR_TX_BUSY;
```

这只是给结构体成员赋值，还没执行发送。



**第二步：调用真正要测试的 Boot-up 函数。**

```c
co_status_t actual = co_nmt_bootup(&my_nmt);
```

它内部调用：

```
co_nmt_bootup()
    ↓
co_send()
    ↓
tx()
```

`tx()` 执行到：

```
return f->result;
```

因为 `f` 指向 `my_bus`，所以等价于：

```
return my_bus.result;
```

也就是返回我们提前设置的：

```
CO_ERR_TX_BUSY
```

这个结果会逐层返回给 `co_nmt_bootup()`。它里面的代码是：

```
status = co_send(nmt->node, &frame);

if (status == CO_OK)
    change_state(nmt, CO_NMT_PRE_OPERATIONAL);

return status;
```

现在 `status` 是 `CO_ERR_TX_BUSY`，所以：

```
status == CO_OK  // 不成立
```

就不会切换到预运行状态。



**第三步：检查实际结果。**

```c
CHECK(actual == CO_ERR_TX_BUSY);
CHECK(my_node.state == CO_NMT_INITIALIZATION);
```

这里的分工是：

```c
我们人为控制：
    发送回调返回“忙”

让真实的NMT代码自行执行：
    是否切换状态？
    向调用者返回什么？

测试最后检查：
    返回值是不是“忙”？
    节点是不是仍在Initialization？
```

然后，还可以模拟“下一次发送恢复正常”：

```c
my_bus.result = CO_OK;

actual = co_nmt_bootup(&my_nmt);

CHECK(actual == CO_OK);
CHECK(my_node.state == CO_NMT_PRE_OPERATIONAL);
```

完整过程就是：

```
第一次：设置发送忙
    → 调用Boot-up
    → 检查节点没有提前进入Pre-op

第二次：设置接受提交
    → 再次调用Boot-up
    → 检查节点正确进入Pre-op
```

在实际测试调用链中，它是这样被用到的：

```
co_nmt_bootup(&my_nmt)
    ↓
co_send(&my_node, &frame)
    ↓
tx(&my_bus, &frame)
    ├── sends加1
    ├── 保存报文副本
    └── 返回my_bus.result
```

这样测试代码既能检查 **Boot-up 组装得对不对**，也能模拟发送失败，检查节点是否仍保持 `Initialization`。





## 4：状态通知回调

```c
static void notify(void *user, co_nmt_state_t state)
{
    fake_t *f = user;
    ++f->notifications; f->state = state;
}
```

这个 `notify()` 是**测试用的状态通知回调**，负责记录：

```c
收到了几次通知？
最近一次通知的状态是什么？
```

第一步是：创建模拟测试状态结构体

```c
fake_t my_bus = {0};
```

假设 NMT 模块调用：

```c
notify(&my_bus, CO_NMT_PRE_OPERATIONAL);
```

进入函数后：

```
user  = &my_bus
state = CO_NMT_PRE_OPERATIONAL
```



```c
第一句：
fake_t *f = user;
让 `f` 指向实际的测试变量：f ────> my_bus

第二句：++f->notifications;等价于：++my_bus.notifications;记录“状态通知回调被调用了一次”。

第三句：f->state = state;等价于：
my_bus.state = CO_NMT_PRE_OPERATIONAL; //左边是**测试结构体中保存的状态记录**，右边是**本次回调收到的状态参数**。

执行后：
my_bus
├── notifications = 1
└── state = CO_NMT_PRE_OPERATIONAL
它与刚才的 tx() 有一个区别：
tx()：
    返回测试预先设置的发送结果

notify()：
    记录NMT模块实际传过来的状态，没有返回值
```







## 5：写函数

```c
static co_status_t write_value(co_device_od_t *d, uint16_t index, uint8_t sub,
                               uint16_t value, uint8_t length)
{
    const co_od_entry_t *e;
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    co_status_t s = co_od_find(&d->table,index,sub,&e);
    return s == CO_OK ? co_od_write(e,CO_NMT_PRE_OPERATIONAL,bytes,length) : s;
    //co_od_write四个参数：要写入的对象条目，调用者告诉它“当前节点处于什么NMT状态”， 要写入的字节数组，本次提供了几个有效字节
}
```

这个 `write_value()` 是**测试辅助函数：按对象地址查找条目，再把指定数值写进去**。

用一个具体例子贯穿：**把心跳周期 `0x1017:00` 写成 500 ms。**

假设对象表已经初始化：

```c
co_device_od_t my_device = {0};

co_device_od_init(&my_device, 1, NULL);
```

调用：

```c
co_status_t result = write_value(&my_device, 0x1017, 0, 500, 2);
```

| 参数     | 此次传入值   | 含义                       |
| -------- | ------------ | -------------------------- |
| `d`      | `&my_device` | 操作哪份设备对象表         |
| `index`  | `0x1017`     | 对象索引                   |
| `sub`    | `0`          | 子索引                     |
| `value`  | `500`        | 要写入的整数               |
| `length` | `2`          | 传给对象写接口的数据字节数 |



#### **① 准备一个条目指针**

```
const co_od_entry_t *e;
```

`e` 用来保存查找结果：

```
e ────> my_device.entries[] 中找到的某个条目
```

这句只声明指针，没有创建一个新的对象条目。此时还不能直接读取 `e`，要先让 `co_od_find()` 填好它。



#### **② 把整数拆成小端字节**

```c
uint8_t bytes[2] = {
    (uint8_t)value,
    (uint8_t)(value >> 8)
};
```

此次：

```
value = 500 = 0x01F4
```

第一个元素：

```
(uint8_t)value
```

转换成 `uint8_t`，保留低 8 位：

```
0x01F4 → 0xF4
```

第二个元素：

```
(uint8_t)(value >> 8)
```

先右移 8 位，再取低 8 位：

```
0x01F4 >> 8 → 0x0001 → 0x01
```

得到：

```
bytes[0] = 0xF4;  // 低字节
bytes[1] = 0x01;  // 高字节
```

也就是：

```
整数500 → 小端字节 F4 01
```

这里要拆字节，是因为阶段 2 的 `co_od_write()` 接收的是**字节数组和长度**，不是直接接收整数 `500`。



#### **③ 查找对象条目**

```
co_status_t s = co_od_find(&d->table, index, sub, &e);
```

代入具体变量，相当于：

```
co_status_t s = co_od_find(
    &my_device.table,
    0x1017,
    0,
    &e
);
```

注意，这次调用有两个不同的结果：

```
函数返回值 → 保存到s
             表示查找成功还是失败

通过&e写回 → 改变e保存的地址
             告诉我们找到的是哪个条目
```

成功后：

```
s = CO_OK

e ────> my_device中0x1017:00的条目
```



#### **④ 查找成功才写入，否则返回查找错误**

最后这句用了三目运算符：

```
return s == CO_OK
    ? co_od_write(e, CO_NMT_PRE_OPERATIONAL, bytes, length)
    : s;
```

可以完整展开成：

```c
if (s == CO_OK)
{
    return co_od_write(
        e,
        CO_NMT_PRE_OPERATIONAL,
        bytes,
        length
    );
}
else
{
    return s;
}
```

所以本次成功查找到心跳对象后，会调用：

```c
co_od_write(e, CO_NMT_PRE_OPERATIONAL, bytes, 2);
```

由阶段 2 的写接口检查权限、长度、状态和值，检查通过后调用对象的写回调，最终把心跳周期改成 `500`。

如果对象不存在：

```c
write_value(&my_device, 0x9999, 0, 500, 2);
```

查找失败就直接返回错误，**不会执行 `co_od_write()`，也不会使用未获得有效查找结果的 `e`。**



#### ⑤这次写入要按“节点当前处于 Pre-operational”来做状态权限检查

```c
co_od_write(e,CO_NMT_PRE_OPERATIONAL,bytes,length)
//co_od_write四个参数：要写入的对象条目，调用者告诉它“当前节点处于什么NMT状态”， 要写入的字节数组，本次提供了几个有效字节
```

这里填：

```
CO_NMT_PRE_OPERATIONAL
```



我们正在测试的是 **NMT 复位命令**，测试前需要先把几个对象改成“非默认值”，这样复位后才看得出是否恢复正确。

但这些对象的写入要经过阶段 2 的状态检查：

```c
if (entry->write_preop_only &&
    state != CO_NMT_PRE_OPERATIONAL)
{
    return CO_ERR_OD_STATE;
}
```

当前测试没有真实 CAN 节点状态，也没有通过真实 SDO 写入，所以辅助函数直接传入：

```c
CO_NMT_PRE_OPERATIONAL
```

意思是：

> 假设这次对象写入发生在 Pre-operational 状态，允许它通过状态限制，继续执行后面的写入测试。

流程就是：

```c
测试准备阶段：
    假设节点处于 Pre-operational
        ↓
    写入非默认对象值
        ↓
    测试 NMT Reset Communication / Reset Node
        ↓
    检查对象恢复范围是否正确
```

例如：

```
write_value(&my_device, 0x1801, 5, 100, 2);
```

先把事件周期改成：

```
0x1801:05 = 100
```

然后测试复位：

```
Reset Communication → 恢复为 0
Reset Node          → 也恢复为 0
```

而：

```
write_value(&my_device, 0x6423, 0, 1, 1);
```

先把应用参数改成 `1`，再检查：

```
Reset Communication → 保留 1
Reset Node          → 恢复为 0
```

所以这里的 `CO_NMT_PRE_OPERATIONAL` 是**测试准备阶段传给对象写函数的状态假设**，不是在测试中真正执行 NMT 状态切换。真正的复位命令仍然由 `co_nmt_receive()` 测试。

当前项目中：

| 对象                              | 是否只能 Pre-op 写入          |
| --------------------------------- | ----------------------------- |
| `0x1800:05` TPDO1 事件周期        | 是                            |
| `0x1801:05` TPDO2 事件周期        | 是                            |
| `0x1017:00` 心跳周期              | 否，Pre-op/Operational 都可写 |
| `0x6423:00` AI 变化触发开关       | 否                            |
| `0x6200:01` DO 命令               | 否                            |
| 固定 PDO 映射、COB-ID、传输类型   | 不可写，属于 `const`          |
| `0x1800:03`、`0x1801:03` 抑制时间 | 固定 0，不可写                |

所以测试里传：

```
CO_NMT_PRE_OPERATIONAL
```

主要是为了允许修改：

```
0x1800:05
0x1801:05
```

例如配置 TPDO2 每 100 ms 发送：

```
write_value(&my_device, 0x1801, 5, 100, 2);
```

阶段 4/6 真正实现时，主站通常会先让节点处于 Pre-operational，配置这些事件周期，再发送 NMT Start 进入 Operational。





## 6：读函数

```c
static uint32_t value_at(co_device_od_t *d, uint16_t index, uint8_t sub)
{
    const co_od_entry_t *e;
    if (co_od_find(&d->table,index,sub,&e) != CO_OK) return UINT32_MAX;
    return ((co_device_value_t *)e->user)->value;
}
```

这个 `value_at()` 也是测试辅助函数，作用是：

> 按 `Index/Sub-index` 找到对象，然后直接读取它当前保存在 `fields[]` 里的数值。

假设：

```c
co_device_od_t my_device = {0};
co_device_od_init(&my_device, 1, NULL);//设备device地址，节点id，设备信息，可以填NULL
```

调用：

```c
uint32_t value = value_at(&my_device, 0x1017, 0);
```



#### 1. 声明条目指针

```
const co_od_entry_t *e;
```

`e` 用来接收 `co_od_find()` 找到的对象条目地址。

它不是新建对象，只是一个指针：

```
e ────> my_device.entries[] 中找到的条目
```



#### 2. 查找对象

```
co_od_find(&d->table, index, sub, &e)
```

代入本次参数，相当于：

```
co_od_find(&my_device.table, 0x1017, 0, &e);
```

如果找到 `0x1017:00`：

```
e ────> 0x1017:00 对应的 entry
```

如果找不到：

```
if (... != CO_OK)
    return UINT32_MAX;
```

这里用 `UINT32_MAX` 表示“查找失败”。因为函数返回类型是 `uint32_t`，不能直接返回 `CO_ERR_OD_NOT_FOUND` 作为同一种结果来使用。

所以调用者可以这样理解：

```
返回 UINT32_MAX → 对象不存在
返回其他数值   → 对象当前值
```



#### 3. 理解 `e->user`

阶段 2 初始化对象时，每个条目都绑定了对应的存储地址：

```
e->user = &device->fields[i];
```

所以：

```
e->user
```

本质上是一个 `void *`，指向这个对象对应的：

```
co_device_value_t
```

例如：

```
e                  → 0x1017:00 的条目
e->user            → my_device.fields[HEARTBEAT_SLOT]
```



#### 4. 强制转换后读取 value

```
((co_device_value_t *)e->user)->value
```

分成三步：

```
(co_device_value_t *)e->user
```

把通用的 `void *` 转回具体的 `co_device_value_t *`。

然后：

```
((co_device_value_t *)e->user)->value
```

通过这个指针访问结构体里的 `value` 成员。

最终返回：

```
my_device.fields[HEARTBEAT_SLOT].value
```

所以如果当前心跳周期是：

```
1000
```

则：

```
value_at(&my_device, 0x1017, 0) == 1000
```



#### 5. 它和 `co_od_read()` 的区别

`value_at()` 是测试代码里的直接检查工具：

```c
value_at()
    直接找到 entry
    直接访问 entry->user 指向的 RAM
    读取 fields[i].value
```

它没有经过：

```
co_od_read()
```

因此它不是在模拟主站通过 SDO 读取，而是在测试中直接检查内部结果。

复位测试就是这样使用它的：

```
write_value(&my_device, 0x1017, 0, 500, 2);

/* 执行 Reset Communication */

CHECK(value_at(&my_device, 0x1017, 0) == 1000);
```

流程是：

```
先写入500
    ↓
执行NMT复位
    ↓
直接读取对象内部当前值
    ↓
检查是否恢复为1000
```

所以可以记住：

> `value_at()` 不执行对象访问协议，只是测试代码用来查看对象当前 RAM 值的快捷函数。

```c
co_od_find() 找到条目
        ↓
把条目地址写入指针 e
        ↓
e->user 指向对应的 条目中的结构体co_device_value_t cell
       cell
        ├── value   ← 读取并返回这个成员
        └── writes
最后这一句：
return ((co_device_value_t *)e->user)->value;
拆开就是：
co_device_value_t *cell = (co_device_value_t *)e->user;
return cell->value;
```





## 7：main函数

代码较长，我们按执行顺序分块看。

对，这是 `test_co_nmt.c` 最后一个函数。**`main()` 把前面学过的辅助函数串起来，依次验证启动、命令处理、复位和异常输入。** 没有新的协议功能，主要看每段“准备什么条件、检查什么结果”。

代码较长，我们按执行顺序分块看。



#### **① 创建本次测试需要的变量**

```c
co_context_t node = {0};		//node：实际节点上下文，保存节点号、当前状态、发送回调
co_device_od_t d = {0};			//d   ：实际设备对象表
co_nmt_t nmt = {0};				//nmt ：连接node、d和通知回调
fake_t f = {0};				   //nmt ：连接node、d和通知回调
can_frame_t cmd = {0};			//测试构造的“收到的CAN帧”
```

初始化后会连接成：

```c
nmt
├── node ─────> node
│              ├── tx = tx函数
│              └── tx_user ──> f
├── device ───> d
├── notify ───> notify函数
└── user ─────> f
```

这里发送回调和通知回调共享同一份测试记录 `f`。

```c
const co_device_identity_t identity = {12,34,56,78};
```

提供一组自定义测试身份：

```c
vendor_id    = 12
product_code = 34
revision     = 56
serial       = 78
```

后面用它检查：**节点复位后，设备身份有没有被错误地改回开发默认值。**

```c
unsigned id, target, command, before;
```

分别用于：

```
id      ：正在测试哪个本机节点号
target  ：报文发给哪个目标节点
command ：循环命令值或命令数组下标
before  ：保存操作前的计数，便于比较
```



随后是，两个数组一一对应：

```
const uint8_t commands[] = {1,2,128};

const co_nmt_state_t states[] = {
    CO_NMT_OPERATIONAL,
    CO_NMT_STOPPED,
    CO_NMT_PRE_OPERATIONAL
};
```

| 下标 | `commands[下标]`         | `states[下标]`  |
| ---- | ------------------------ | --------------- |
| 0    | `1`：Start               | Operational     |
| 1    | `2`：Stop                | Stopped         |
| 2    | `128`，即 `0x80`：Pre-op | Pre-operational |

这样循环时，可以同时取出“命令”和“期望状态”。

```c
这两个数组是配对使用的测试数据，用来表示：
每个普通 NMT 命令执行后，节点应该进入什么状态。

const uint8_t commands[] = {1, 2, 128};
保存三个命令字节：
commands[0] = 1    → Start
commands[1] = 2    → Stop
commands[2] = 128  → 0x80，Enter Pre-operational
const co_nmt_state_t states[] = {
    CO_NMT_OPERATIONAL,
    CO_NMT_STOPPED,
    CO_NMT_PRE_OPERATIONAL
};
保存对应的期望状态：
states[0] = CO_NMT_OPERATIONAL
states[1] = CO_NMT_STOPPED
states[2] = CO_NMT_PRE_OPERATIONAL
它们通过相同下标建立对应关系：
下标			commands[]		命令含义	      states[]				   期望结果
0			    1			  Start		  CO_NMT_OPERATIONAL	     进入 Operational
1	             2			   Stop	         CO_NMT_STOPPED		   	  进入 Stopped
2	            128			 Enter Pre-op	CO_NMT_PRE_OPERATIONAL	  进入 Pre-operational


后面的循环：
for (command = 0; command < 3; ++command)
{
    co_nmt_state_t old = node.state;
    cmd.data[0] = commands[command];

    CHECK(co_nmt_receive(&nmt, &cmd) ==
          ((target == 0 || target == id) ? CO_OK : CO_IGNORED));

    CHECK(node.state ==
          ((target == 0 || target == id)
               ? states[command]
               : old));
}
例如第一次循环：
command = 0;
执行：
cmd.data[0] = commands[0];
也就是：
cmd.data[0] = 1;
然后期望：
node.state == states[0];
也就是：
node.state == CO_NMT_OPERATIONAL;
第二次循环：
command = 1;
相当于：
cmd.data[0] = 2;
node.state == CO_NMT_STOPPED;
第三次循环：
command = 2;
相当于：
cmd.data[0] = 128;
node.state == CO_NMT_PRE_OPERATIONAL;
所以这两个数组避免了重复写三段几乎相同的测试代码。它们本质上是一个“命令—期望状态”测试表：
命令数组       发送什么
状态数组       应该变成什么
相同下标       表示一对
这里 commands[] 也可以写得更清楚：
const uint8_t commands[] = {
    CO_NMT_START,
    CO_NMT_STOP,
    CO_NMT_ENTER_PREOP
};
这样比直接写：
{1, 2, 128}
更容易看出协议含义，但当前测试代码中的数值与枚举值相同，功能没有区别。
```









#### **② 先检查未准备好的输入**

这三句是在测试**函数收到无效输入时，能不能马上返回参数错误**。它们还没有开始测试正常 NMT 流程。

```c
CHECK(co_nmt_bootup(NULL) == CO_ERR_ARGUMENT);		//co_status_t co_nmt_bootup(co_nmt_t *nmt)
CHECK(co_nmt_receive(&nmt,NULL) == CO_ERR_ARGUMENT);//co_status_t co_nmt_receive(co_nmt_t *nmt, const can_frame_t *frame)
CHECK(co_device_od_reset(&d,1) == CO_ERR_ARGUMENT);//
```

此时 `nmt`、`d` 都只是清零，还没初始化。

`{0}` 表示全部成员清零，所以此时：

```
nmt
├── node   = NULL
├── device = NULL
├── notify = NULL
└── user   = NULL

d
├── fields[]  全部为0
├── entries[] 全部为0
└── table
    ├── entries = NULL
    └── count   = 0
```



###### 第一条：传入空的 NMT 指针

```
CHECK(co_nmt_bootup(NULL) == CO_ERR_ARGUMENT);
```

调用的是：

```
co_nmt_bootup(NULL);
```

函数内部第一步：

```
co_status_t status = validate(nmt);
```

此时：

```
nmt == NULL
```

进入 `validate()` 后，第一组判断立即成立：

```
if (nmt == NULL || ...)
    return CO_ERR_ARGUMENT;
```

所以：

```
co_nmt_bootup(NULL)
        ↓
validate(NULL)
        ↓
CO_ERR_ARGUMENT
```

`co_nmt_bootup()` 不会继续组装 CAN 帧，也不会发送 Boot-up。

`CHECK` 再确认返回值确实是：

```
CO_ERR_ARGUMENT
```



###### 第二条：NMT 指针本身不为空，但接收帧指针为空

```
CHECK(co_nmt_receive(&nmt, NULL) == CO_ERR_ARGUMENT);
```

这里传入的是：

```
nmt  = &nmt
frame = NULL
```

注意，`&nmt` 虽然不是空指针，但 `nmt` 这个结构体还没有初始化。

因此 `co_nmt_receive()` 第一件事仍然是：

```
status = validate(nmt);
```

`validate(&nmt)` 检查到：

```
nmt->node == NULL
```

因为 `nmt` 是 `{0}` 创建的，里面的 `node` 仍然是空指针。

于是 `validate()` 返回：

```
CO_ERR_ARGUMENT
```

`co_nmt_receive()` 随后执行：

```
if (status != CO_OK)
    return status;
```

所以它会直接返回：

```
CO_ERR_ARGUMENT
```

此时它**还没有执行到**：

```
co_classify_rx(nmt->node, frame, &kind);
```

因此，这条测试实际验证的是：

> 未初始化的 NMT 上下文会被拒绝。

它暂时没有真正验证：

> 一个已经初始化好的 NMT 上下文，收到 `frame == NULL` 时能否拒绝空帧。

后面必须先完成：

```
co_init(...)
co_device_od_init(...)
co_nmt_init(...)
```

然后再调用：

```
co_nmt_receive(&nmt, NULL);
```

这时 `validate(&nmt)` 会通过，函数才会继续调用：

```
co_classify_rx(nmt->node, NULL, &kind);
```

最终由阶段 1 的帧检查返回：

```
CO_ERR_ARGUMENT
```

两种情况的路径不同：

```
未初始化的nmt：
co_nmt_receive()
    → validate()
    → CO_ERR_ARGUMENT

已初始化的nmt，但frame为空：
co_nmt_receive()
    → validate()通过
    → co_classify_rx()
    → CO_ERR_ARGUMENT
```

返回结果相同，但测试的层次不同。



###### 第三条：未初始化的对象字典执行复位

```c
CHECK(co_device_od_reset(&d, 1) == CO_ERR_ARGUMENT);
```

这里：

```
d    = 一个实际存在的结构体变量
&d   = 有效地址
```

所以它不是在测试空指针，而是在测试：

> 对象字典结构体虽然存在，但还没有经过 `co_device_od_init()`，能否被拒绝。

函数收到：

```c
device = &d
communication_only = 1
```

它首先会检查设备是否准备好，类似：

```
if (!ready(device) || communication_only > 1u)
    return CO_ERR_ARGUMENT;
```

此时 `d` 是清零状态：

```
d.table.entries == NULL
d.table.count   == 0
```

但一个有效设备对象表应该满足：

```
d.table.entries == d.entries
d.table.count == CO_DEVICE_OD_COUNT
```

也就是：

```
d.table.count == 36
```

当前这些条件都不满足，因此：

```
co_device_od_reset(&d, 1)
        ↓
ready(&d) 失败
        ↓
CO_ERR_ARGUMENT
```

它不会恢复任何对象，也不会修改 `d`。



三条测试可以总结成：

```c
1. co_nmt_bootup(NULL)
   检查是否拒绝空的NMT指针

2. co_nmt_receive(&nmt, NULL)
   当前因为nmt尚未初始化，
   先检查出nmt->node为空

3. co_device_od_reset(&d, 1)
   检查是否拒绝尚未初始化的对象字典
```

它们的共同目的都是：

```
传入无效或未准备好的对象
        ↓
函数必须尽早返回 CO_ERR_ARGUMENT
        ↓
不能继续访问成员、发送报文或修改对象数据
```







#### **③ 对节点号 1～127，分别执行整套测试**

```c
for (id = 1; id <= 127; ++id)
{
    CHECK(co_init(&node, (uint8_t)id, tx, &f) == CO_OK);
    CHECK(co_device_od_init(&d, (uint8_t)id, &identity) == CO_OK);
    CHECK(co_nmt_init(&nmt, &node, &d, notify, &f) == CO_OK);
```

它们依次准备：**节点 → 对象字典 → NMT 模块**。外面的 `CHECK` 只是确认每次初始化返回 `CO_OK`。

###### **第一条：准备节点 `node`。**

```
co_init(&node, 1, tx, &f);
```

执行后：

```c
node
├── node_id = 1
├── state   = Initialization
├── tx      = tx函数地址
└── tx_user = &f
```

意思是：

> 这是节点 1。以后需要发帧，就调用 `tx()`，并把测试记录 `f` 的地址交给它。

这里还不发送任何报文。



###### **第二条：准备设备对象字典 `d`。**

```c
co_device_od_init(&d, 1, &identity);
```

执行后：

```c
d
├── fields[]  → 填入对象默认值
├── entries[] → 填入36个条目的类型、权限、回调等
└── table     → 连接到自己的entries数组，数量为36
```

其中：

- 节点号 `1` 用于计算对象表中的 PDO COB-ID 参数。
- `&identity` 提供之前准备的设备身份 `{12, 34, 56, 78}`



###### **第三条：把前面准备好的两部分接到 NMT 模块。**

```
co_nmt_init(&nmt, &node, &d, notify, &f);
```

执行后：

```
nmt
├── node   = &node
├── device = &d
├── notify = notify函数地址
└── user   = &f
```

意思是：

> NMT 模块使用这个节点、这份对象字典；需要通知状态时，调用 `notify()`，并把 `&f` 传给它。

初始化成功时，还会通知一次：

```
notify(&f, CO_NMT_INITIALIZATION);
```

所以 `f.notifications` 加 1，`f.state` 记录为初始化状态。

三句连起来就是：

```c
co_init()             准备node
                          │
co_device_od_init()    准备d
                          │
co_nmt_init()          让nmt连接node和d，并绑定通知回调
```

**执行到这里，节点仍是 `Initialization`，还没有发送 Boot-up。**

外层循环只是把这一套流程分别用节点号 `1、2……127` 测试一遍。不是同时创建 127 个节点，而是每轮重新使用同一组 `node`、`d`、`nmt` 变量。



###### 测试目的：

测试 `2~127` 是为了验证协议代码的**通用性和边界**，不是要让一块板子同时运行 127 个节点。

CANopen 规定合法 Node-ID 范围是：

```
1～127
```

所以循环：

```
for (id = 1; id <= 127; ++id)
```

是在逐个验证所有合法节点号都能正确初始化：

```c
co_init(&node, id, tx, &f);
co_device_od_init(&d, id, &identity);
co_nmt_init(&nmt, &node, &d, notify, &f);
```



当循环执行：id = 2;

前面执行了：

```c
co_init(&node, 2, tx, &f);		//因此我们pc端的ID，此时变成了Node-ID = 2
```

NMT 报文固定使用：CAN-ID = 0x000

具体目标节点写在：DATA[1]

此时，我们只能接受来自广播和自身节点（Node-ID = 2）的NMT信息了

构造报文：

```c
CAN-ID = 0x000
DLC    = 2
DATA   = 01 02
         │  │
         │  └── 目标 Node-ID = 2
         └───── Start 命令
```

当前模拟节点的编号也是 2：

```
本节点 Node-ID = 2
报文目标 Node-ID = 2
所以它判断：frame->data[1] == ctx->node_id
也就是：2 == 2
条件成立，节点接受这个 NMT 命令，并执行：Start → 进入 Operational
```

再构造报文（指向节点1的）：

```c
CAN-ID = 0x000
DLC    = 2
DATA   = 01 01
         │  │
         │  └── 目标 Node-ID = 1
         └───── Start 命令
```

当前模拟节点仍然是：

```c
本节点 Node-ID = 2
报文目标 Node-ID = 1
判断变成：
frame->data[1] == ctx->node_id
也就是：
1 == 2
不成立。
同时它也不是广播：frame->data[1] != 0
所以这个报文不是发给当前节点的，返回：CO_IGNORED
并且：
节点状态不改变
状态通知不增加
不发送回复
如果报文是：
CAN-ID = 0x000
DLC    = 2
DATA   = 01 00
目标地址为 0，表示广播：给所有节点发送 Start
所以即使当前节点是 2，也会接受：frame->data[1] == 0
对于当前模拟节点 Node-ID=2：
目标 0 → 接受，广播
目标 1 → 忽略，发给节点1
目标 2 → 接受，发给本节点
目标 3 → 忽略，发给节点3
所以这句话的准确含义是：
当测试把当前节点模拟为 Node-ID=2 时，目标地址为 2 的 NMT 报文是发给它自己的，因此接受；目标地址为 1 的报文是发给另一个节点的，因此忽略。

实际项目只有 Node-ID=1 时，规则完全一样：
目标 0 → 节点1接受
目标 1 → 节点1接受
目标 2 → 节点1忽略
```



###### 完整链路：

```c
co_init()
    └── 设置 node.node_id = 2

co_nmt_init()
    └── 让 nmt->node 指向 node

cmd.data[1] = 2
    └── 设置报文目标为节点2

co_nmt_receive()
    └── co_classify_rx()
            └── 比较 frame->data[1] 和 ctx->node_id
                    2 == 2，接受

        └── case CO_NMT_START
                └── change_state()
                        └── 进入 Operational
```

因此，初始化三句只是**准备节点身份和模块连接**；真正执行“接受目标地址并切换状态”的代码在 `co_classify_rx()` 和 `co_nmt_receive()` 中。







#### **④ 初始化中收到 Start，应当忽略**

```c
can_frame_t cmd = {0};  //can报文帧
cmd.dlc=2;
cmd.data[0]=1;
cmd.data[1]=0;

CHECK(co_nmt_receive(&nmt,&cmd)==CO_IGNORED);
//测试这句话就是为了确认：节点不会因为过早收到 Start，就跳过 Boot-up 和 Pre-operational 阶段直接运行。
```

`cmd.id` 从最初清零后一直是 `0`，所以此时构造的是：

```
CAN-ID = 0x000
DLC    = 2
DATA   = 01 00
         │  └── 广播
         └───── Start
```

它是合法的广播 Start，但节点仍在 `Initialization`，所以忽略，因此：节点还处于 `Initialization` 时，即使主站发送了 `Start` 命令，节点也不能直接进入 `Operational`



正常启动顺序必须是：

```c
co_init()
    ↓
Initialization
    ↓
完成初始化并发送 Boot-up
    ↓
Pre-operational
    ↓
主站发送 Start
    ↓
Operational
```

测试中的这段代码：

```c
cmd.dlc = 2;
cmd.data[0] = 1;  // Start
cmd.data[1] = 0;  // 广播

CHECK(co_nmt_receive(&nmt, &cmd) == CO_IGNORED);
此时刚执行完：
co_init(...)
co_device_od_init(...)
co_nmt_init(...)
节点状态仍是：node.state == CO_NMT_INITIALIZATION
而co_nmt_receive() 中有判断：
if (kind != CO_RX_NMT ||nmt->node->state == CO_NMT_INITIALIZATION)  return CO_IGNORED;
虽然这帧确实是合法的广播 NMT Start，但第二个条件成立：
nmt->node->state == CO_NMT_INITIALIZATION 因此直接返回：CO_IGNORED
不会执行下面的代码：
case CO_NMT_START:change_state(nmt, CO_NMT_OPERATIONAL); break;
也就是：Initialization + Start→ 忽略→ 仍是 Initialization
必须先成功执行：co_nmt_bootup(&nmt);
  
它发送：
CAN-ID = 0x700 + Node-ID
DATA   = 00
发送成功后：
change_state(nmt, CO_NMT_PRE_OPERATIONAL);
此时节点才从：Initialization → Pre-operational
   
之后再发送：
000 [01 01]
或者广播：
000 [01 00]
Start 才会被处理：
Pre-operational → Operational
   
所以这句话的重点是：Start 只是状态切换命令，  不是“完成节点初始化”的命令。
co_nmt_bootup() 负责：
完成启动通知，并进入 Pre-operational
Start 负责：
让已经完成启动的节点进入 Operational
测试这句话就是为了确认：节点不会因为过早收到 Start，就跳过 Boot-up 和 Pre-operational 阶段直接运行。
```









#### **⑤ 依次模拟 Boot-up 发送忙、失败、成功**

```c
before=f.sends;
        f.result=CO_ERR_TX_BUSY;
        CHECK(co_nmt_bootup(&nmt)==CO_ERR_TX_BUSY);
        CHECK(node.state==CO_NMT_INITIALIZATION);
        f.result=CO_ERR_TX_FAILED;
        CHECK(co_nmt_bootup(&nmt)==CO_ERR_TX_FAILED);
        f.result=CO_OK;
        CHECK(co_nmt_bootup(&nmt)==CO_OK);
        CHECK(f.frame.id==0x700+id && f.frame.dlc==1 && f.frame.data[0]==0);
        CHECK(!f.frame.is_extended && !f.frame.is_remote && !f.frame.is_fd);
        CHECK(node.state==CO_NMT_PRE_OPERATIONAL && f.state==node.state);
        CHECK(co_nmt_bootup(&nmt)==CO_IGNORED && f.sends==before+3);
```

这一段专门测试 Boot-up 发送失败、重试成功，以及成功后不能重复发送。

前面定义的变量：

```c
typedef struct {
    unsigned sends, notifications;
    co_status_t result;
    can_frame_t frame;
    co_nmt_state_t state;
} fake_t;

unsigned id, target, command, before;
fake_t f = {0};
before = f.sends;
先保存当前发送回调已经被调用的次数。//后面用它确认 Boot-up 尝试了几次。
    
```



###### 第一次：模拟发送忙

```c
f.result = CO_ERR_TX_BUSY;
```

给测试发送回调预设结果：

```c
调用 tx() 时返回 CO_ERR_TX_BUSY
```

然后：

```
CHECK(co_nmt_bootup(&nmt) == CO_ERR_TX_BUSY);
```

调用链：

```c
co_nmt_bootup(&nmt)
    ↓
co_send(...)
    ↓
tx(&f, &frame)
    ↓
return f.result
    ↓
CO_ERR_TX_BUSY
```

因为 `co_nmt_bootup()` 只有在：

```
status == CO_OK
```

时才调用：

```c
change_state(nmt, CO_NMT_PRE_OPERATIONAL);
```

所以这次发送忙时，状态不会改变：

```
CHECK(node.state == CO_NMT_INITIALIZATION);
```

也就是：

```c
发送忙
    ↓
Boot-up没有成功提交
    ↓
节点仍是Initialization
```

注意：`tx()` 已经被调用，所以：

```c
f.sends
```

会增加一次。它记录的是**尝试次数**，不是成功次数。





###### 第二次：模拟发送失败

```c
f.result = CO_ERR_TX_FAILED;
```

这次让发送回调返回：

```
CO_ERR_TX_FAILED
```

然后再次调用：

```
CHECK(co_nmt_bootup(&nmt) == CO_ERR_TX_FAILED);
```

预期结果就是：

```
Boot-up发送失败
    ↓
co_nmt_bootup()返回CO_ERR_TX_FAILED
    ↓
节点仍保持Initialization
```

这里没有再次检查状态，是因为上一句已经确认第一次失败后仍是 `Initialization`，而第二次失败也走同样的逻辑。

到这里，发送回调已经被调用两次：

```
第一次：TX_BUSY
第二次：TX_FAILED
```



###### 第三次：模拟发送成功

```
f.result = CO_OK;
```

把发送回调恢复为成功：

```
tx()返回CO_OK
```

调用：

```
CHECK(co_nmt_bootup(&nmt) == CO_OK);
```

这次完整流程是：

```c
co_nmt_bootup()
    ↓
组装Boot-up帧
    ↓
tx()返回CO_OK
    ↓
change_state(nmt, CO_NMT_PRE_OPERATIONAL)
    ↓
返回CO_OK
```

于是节点从：

```
Initialization
    ↓
Pre-operational
```



###### 检查 Boot-up 报文内容

```c
CHECK(f.frame.id==0x700+id && f.frame.dlc==1 && f.frame.data[0]==0); //f.frame 是 tx() 保存的最近一次发送帧副本
这句检查三项：
CAN-ID = 0x700 + 当前节点号
DLC    = 1
DATA[0] = 0x00
例如当前：for循环到了 id = 2;
就要求：
CAN-ID = 0x702
DLC    = 1
DATA   = 00
```



###### 检查帧类型

```c
 CHECK(!f.frame.is_extended && !f.frame.is_remote && !f.frame.is_fd);
因为 ! 表示逻辑非，所以这要求：
is_extended = 0
is_remote   = 0
is_fd       = 0
即：
标准帧
数据帧
经典 CAN
Boot-up 不能被组装成扩展帧、远程帧或 CAN FD 帧。
```



###### 检查节点状态和通知状态

```c
 CHECK(node.state==CO_NMT_PRE_OPERATIONAL && f.state==node.state);
```

```c
这同时检查：
node.state = CO_NMT_PRE_OPERATIONAL
f.state    = CO_NMT_PRE_OPERATIONAL
其中：
node.state
    真实节点上下文中的状态

f.state
    notify() 回调收到并记录的最近状态
说明两件事都正确：
节点实际进入了Pre-operational
状态通知回调也收到了Pre-operational
```



###### 第四次：Boot-up 已完成，不能重复发送

```c
CHECK(co_nmt_bootup(&nmt)==CO_IGNORED && f.sends==before+3);
```

```c
此时节点已经是：
CO_NMT_PRE_OPERATIONAL
而 co_nmt_bootup() 要求当前状态必须是：
CO_NMT_INITIALIZATION
所以这次直接返回：
CO_IGNORED
不会再次调用 tx()。
发送回调一共尝试了三次：
第一次：TX_BUSY
第二次：TX_FAILED
第三次：CO_OK
因此：
f.sends == before + 3
第四次调用被忽略，发送次数不会变成 before + 4。
```



###### 总览：

```c
整个测试流程可以记成：
保存原发送次数
    ↓
模拟忙 → Boot-up失败，仍是Initialization
    ↓
模拟失败 → Boot-up失败，仍未进入Pre-op
    ↓
模拟成功 → 发送701/702等Boot-up，进入Pre-op
    ↓
再次调用 → 忽略，不重复发送
这段验证了三个关键规则：
1. Boot-up发送失败，节点不能提前进入Pre-operational
2. 发送成功后，节点才进入Pre-operational
3. Boot-up完成后不能重复发送
```



#### **⑥ 遍历目标地址，测试三种普通命令**

```c
for (target=0; target<=255; ++target) {
    cmd.data[1]=(uint8_t)target;
```

这里遍历 NMT 目标字节的所有可能值：

```c
0          ：广播，应接受
等于本机id ：发给自己，应接受
其他值     ：应忽略，包括128～255
```

再对每个目标地址，依次发送 Start、Stop、Pre-op：

```c
for(command=0;command<3;++command) {
    co_nmt_state_t old=node.state;
    cmd.data[0]=commands[command];
```

`old` 保存处理前的状态，用来检查无关报文有没有误改状态。

下面两个检查：

```c
CHECK(co_nmt_receive(&nmt,&cmd)==
      ((target==0 || target==id)?CO_OK:CO_IGNORED));

CHECK(node.state==
      ((target==0 || target==id)?states[command]:old));
```

可以读成：

```c
如果目标是广播或本机：
    返回CO_OK
    节点变成该命令对应的状态

否则：
    返回CO_IGNORED
    节点保持old状态
```

例如本机 `id=1`，当前是 Pre-op：

```c
收到01 01 → 发给自己 → Operational
收到02 02 → 发给节点2 → 忽略，保持Operational
```

接着原样重复发送同一帧：

```c
before=f.notifications;

CHECK(co_nmt_receive(&nmt,&cmd)==
      ((target==0 || target==id)?CO_OK:CO_IGNORED));

CHECK(before==f.notifications);
```

验证：

```c
有效重复命令：状态已经相同，不重复通知
无关命令：继续忽略，不产生通知
```



#### **⑦ 遍历未知命令**

```c
cmd.data[1]=0;

for(command=0;command<=255;++command) {
    if(command==1 || command==2 || command==128 ||
       command==129 || command==130)
        continue;

    cmd.data[0]=(uint8_t)command;
    CHECK(co_nmt_receive(&nmt,&cmd)==CO_IGNORED);
}
```

先把目标设为广播，保证报文不会因为“目标不符”而被忽略。

再跳过五个合法命令：

```c
1   = 0x01 Start
2   = 0x02 Stop
128 = 0x80 Pre-op
129 = 0x81 Reset Node
130 = 0x82 Reset Communication
```

`continue` 表示跳过本次循环，测试下一个命令值。

其余命令全部要求返回 `CO_IGNORED`。这段直接检查的是返回值，没有逐次断言状态和计数。



#### **⑧ 准备复位前的数据**

```c
CHECK(write_value(&d,0x1017,0,500,2)==CO_OK);
CHECK(write_value(&d,0x1801,5,100,2)==CO_OK);
CHECK(write_value(&d,0x6423,0,1,1)==CO_OK);
CHECK(write_value(&d,0x6200,1,15,1)==CO_OK);
CHECK(co_device_od_update_inputs(&d,5,2048,4095)==CO_OK);
```

这就是刚才学过的辅助函数的用途：先把对象改成非默认值。

| 对象                      | 准备的值           |
| ------------------------- | ------------------ |
| 心跳周期 `1017:00`        | 500                |
| TPDO2 事件周期 `1801:05`  | 100                |
| AI 变化触发开关 `6423:00` | 1                  |
| DO 命令 `6200:01`         | 15                 |
| DI `6000:01`              | 5                  |
| AI1 `6401:01`             | `2048 × 8 = 16384` |
| AI2 `6401:02`             | `4095 × 8 = 32760` |



#### **⑨ 测试通信复位，并让这次 Boot-up 返回忙**

```c
cmd.data[0]=130;		//10进制130转为16进制，等价的cmd.data[0] = 0x82;
f.result=CO_ERR_TX_BUSY;
```

目标字节仍然为 `0`，所以报文是：

```
000 [82 00]：广播通信复位
```

调用：

```c
CHECK(co_nmt_receive(&nmt,&cmd)==CO_ERR_TX_BUSY);
CHECK(node.state==CO_NMT_INITIALIZATION);
```

执行过程是：

```c
接受通信复位
    ↓
进入Initialization
    ↓
恢复通信区对象
    ↓
尝试提交Boot-up
    ↓
模拟发送忙，保持Initialization
```

**返回“发送忙”并不表示前面的对象复位没有执行。** 接着就检查复位结果：

```c
CHECK(value_at(&d,0x1017,0)==1000 &&
      value_at(&d,0x1801,5)==0);
```

通信参数恢复默认。

```c
CHECK(value_at(&d,0x6423,0)==1 && value_at(&d,0x6200,1)==15);
CHECK(value_at(&d,0x6401,1)==16384);
```

应用开关、DO 命令和 AI1 保留原值。

然后允许发送成功，单独重试 Boot-up：

```c
f.result=CO_OK;
CHECK(co_nmt_bootup(&nmt)==CO_OK);
```

节点重新进入 Pre-op，不需要再次执行复位。



#### **⑩ 测试节点复位**

```c
cmd.data[0]=129;		//10进制129转为16进制，等价的cmd.data[0] = 0x81;
cmd.data[1]=(uint8_t)id;

CHECK(co_nmt_receive(&nmt,&cmd)==CO_OK);
```

第一轮对应：

```c
000 [81 01]：对节点1执行节点复位
```

检查应用数据也恢复默认：

```
CHECK(value_at(&d,0x6423,0)==0 &&value_at(&d,0x6200,1)==0);      
CHECK(value_at(&d,0x6401,1)==0);
```

再检查身份保留：

```
CHECK(value_at(&d,0x1018,1)==12 &&value_at(&d,0x1018,4)==78);  
```

即厂商编号仍为 `12`，序列号仍为 `78`。

检查 TPDO1 固定 COB-ID 参数没有被破坏：

```
CHECK(value_at(&d,0x1800,1)==0x40000180+id);
```

第一轮应是 `0x40000181`。它是对象中的完整参数，包含禁止 RTR 的标志位。

到这里，一轮节点测试结束，随后换下一个 `id`，直到 127。



#### **⑪ 循环结束后检查非法帧**

```c
    /*11：循环结束后检查非法帧*/
    before = f.sends;
    CHECK(co_nmt_receive(&nmt, NULL) == CO_ERR_ARGUMENT); /*没有传入can报文帧的结构体地址*/
    cmd.dlc = 1;
    CHECK(co_nmt_receive(&nmt, &cmd) == CO_ERR_DLC); /* NMT应为DLC=2，这里少1字节 */
    cmd.dlc = 3;
    CHECK(co_nmt_receive(&nmt, &cmd) == CO_ERR_DLC); /* NMT应为DLC=2，这里多1字节 */
    cmd.dlc = 2;
    cmd.is_remote = 1;
    CHECK(co_nmt_receive(&nmt, &cmd) == CO_ERR_FRAME_TYPE); /* 传入RTR远程帧，不接受 */
    cmd.is_remote = 0;
    cmd.is_extended = 1;
    CHECK(co_nmt_receive(&nmt, &cmd) == CO_ERR_FRAME_TYPE); /* 传入扩展帧，不接受 */
    cmd.is_extended = 0;
    cmd.is_fd = 1;
    CHECK(co_nmt_receive(&nmt, &cmd) == CO_ERR_FRAME_TYPE); /* 传入CAN FD帧，不接受 */
    cmd.is_fd = 0;
    cmd.id = 0x800;
    CHECK(co_nmt_receive(&nmt, &cmd) == CO_ERR_CAN_ID); /* CAN-ID超出11位标准帧范围 */
    cmd.id = 0x600 + 127;
    CHECK(co_nmt_receive(&nmt, &cmd) == CO_IGNORED);                  /* 这是SDO请求ID，不是NMT，交给其他服务 */
    CHECK(f.sends == before && node.state == CO_NMT_PRE_OPERATIONAL); /* 非法/无关帧不应发送，也不应改变状态 */
    CHECK(co_device_od_reset(&d, 2) == CO_ERR_ARGUMENT);              /* reset范围参数只能是0或1，传入2非法 */
```



#### **⑫ 所有检查通过，程序返回成功**

```c
puts("NMT: boot, routing, commands, reset scopes and failures passed");
return 0;
```

只有前面全部 `CHECK` 通过，才会执行到这里。任何一次检查失败，宏都会打印行号和表达式，提前从 `main()` 返回 `1`。

这份 `main()` 的主线是：

```c
创建测试变量
    ↓
检查无效上下文
    ↓
对每个节点号1～127：
    初始化
    → Boot-up忙、失败、成功、重复调用
    → 目标地址与普通命令
    → 未知命令
    → 通信复位及Boot-up重试
    → 节点复位及身份保留
    ↓
检查非法帧和非法复位参数
    ↓
返回0：测试通过
```