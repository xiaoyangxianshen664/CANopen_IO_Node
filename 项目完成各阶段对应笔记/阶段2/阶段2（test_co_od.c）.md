# 解析



## 1：测试结构体

这个结构体是测试时使用的**模拟对象存储和记录本**，和阶段 1 的 `fake_bus_t` 思路一样。

```c
typedef struct {
    uint32_t value;  /* 模拟对象当前数值 */
    unsigned reads; /* 记录读取回调次数 */
    unsigned writes;/* 记录写入回调次数 */
} fake_value_t;
```



#### **① `uint32_t value`：保存模拟对象的数值**

```
uint32_t value;
```

例如模拟数字输出当前命令：

```
value = 5;
```

或者模拟心跳周期：

```
value = 1000;
```

这里用 `uint32_t`，可以容纳当前支持的 8、16、32 位无符号对象值。



#### **② `unsigned reads`：读取回调调用次数**

```
unsigned reads;
```

`unsigned` 等价于 `unsigned int`。

每执行一次模拟读取回调，就把它加一：

```
++value->reads;
```

测试随后查看这个次数，判断读取回调有没有执行。

注意，这里 `value` 是回调里的指针变量名，具体到下一个函数再看。



#### **③ `unsigned writes`：写入回调调用次数**

```
unsigned writes;
```

每执行一次模拟写入回调，就把它加一。

例如传入非法数据时，对象字典应该提前拒绝写入。如果 `writes` 没增加，就能检查出写回调没有被调用。



#### **④ 创建一个这样的变量**

```
fake_value_t object = {0};
```

此时：

```
object.value  == 0;
object.reads  == 0;
object.writes == 0;
```

提前设置模拟数值：

```
object.value = 1000;
```

这样，后面的模拟读取回调就有一个实际变量可以读取。

这三个成员分别回答：

```c
value  → 对象当前是多少？
reads  → 读取回调执行了几次？
writes → 写入回调执行了几次？
```

结构体本身不会自动计数或更新数值，这些操作由后面的 `fake_read()` 和 `fake_write()` 完成。





## 2：测试用的读取回调

```c
static co_status_t fake_read(void *user, uint8_t *data, uint8_t length)
{
    fake_value_t *value = user; /* 还原回调私有数据指针 */
    uint8_t i; /* 字节下标，从低字节开始处理 */

    ++value->reads; /* 记录一次读取 */
    for (i = 0u; i < length; ++i) { /* 遍历有效字节，最低地址存低字节 */
        data[i] = (uint8_t)(value->value >> (8u * i)); /* 按小端顺序输出 */
    }
    return CO_OK; /* 本次操作成功 */
}
```

这段代码本质上是在做一件事：

因为我们pc端用fake_value_t 创建了一个结构体变量value ，这个结构体value 里面又有三个值 

```c
fake_value_t value = {
    0x1234u,  /* 当前值 */
    0u,       /* 初始读取次数 */
    0u        /* 初始写入次数 */
};
```

然后fake_read（）这个函数就是把这里面的0x1234u，拆成0x34 和0x12，低位先存，因此是data[0]存0x34，data[1]存0x12





#### **① 先明确三个参数**

用一个直接调用的例子来理解：

```c
fake_value_t object = {0};
object.value = 0x0726;

uint8_t buffer[2] = {0};

co_status_t status = fake_read(&object，buffer, 2);	//从我们创建的结构体变量object，读两个字节内容，存到buffer中去
```

进入函数后：

| 形参     | 接收到什么                |
| -------- | ------------------------- |
| `user`   | `object` 的地址           |
| `data`   | `buffer` 数组首元素的地址 |
| `length` | `2`，需要输出两个字节     |

这里没有读取真实 ADC，只读取测试中提前设置的 `object.value`。



#### **② 用正确的指针类型访问模拟变量**

```c
static co_status_t fake_tx(void *user, const can_frame_t *frame)
//其中第一个形参是  fake_value_t *value = user;
然后我们填的是fake_read(&object，buffer, 2);
因此此时user的值就是object的地址，也即user=&object
所以结构体指针fake_value_t *value，指向的位置就是&object，
因此可以通过-> 来访问结构体object中的三个子变量 reads ，value，write
```

`user` 是通用指针 `void *`。这一行创建一个 `fake_value_t *` 类型的指针 `value`，指向同一个 `object`。

因此：

```
value->reads  /* 就是外面的 object.reads */
value->value  /* 就是外面的 object.value */
```

特别注意这个容易混淆的写法：

```
value->value
```

- 左边的 `value`：函数内部的**指针变量**。
- 右边的 `value`：结构体中保存整数的**成员名称**。

两个名字相同，但不是同一个东西。



#### **③ 记录一次读取**

```
++value->reads;
```

等价于让外面的：

```
object.reads
```

加一。原来是 `0`，现在变成 `1`。



#### **④ 逐个产生输出字节**

```
for (i = 0u; i < length; ++i)
```

本次 `length = 2`，因此分别处理：

```
data[0]
data[1]
```

核心语句是：

```
data[i] = (uint8_t)(value->value >> (8u * i));
```

拆成两步：

```
value->value >> (8u * i)
```

先把整数向右移动，使需要的字节到达最低 8 位。

```
(uint8_t)(...)
```

再转换成 `uint8_t`，只保留最低 8 位。

以 `object.value = 0x0726` 为例：

| 循环               | 右移后   | 转成 `uint8_t` | 写入位置  |
| ------------------ | -------- | -------------- | --------- |
| `i = 0`，右移 0 位 | `0x0726` | `0x26`         | `data[0]` |
| `i = 1`，右移 8 位 | `0x0007` | `0x07`         | `data[1]` |

所以外面的数组最终是：

```
buffer[0] == 0x26; /* 低字节在前 */
buffer[1] == 0x07; /* 高字节在后 */
```

这里右移只是计算一个结果，**不会改变 `object.value`**，它仍然是 `0x0726`。



#### **⑤ 返回成功状态**

```
return CO_OK;
```

本次调用结束后：

```
status        == CO_OK;
object.reads  == 1;
object.value  == 0x0726;
buffer[0]     == 0x26;
buffer[1]     == 0x07;
```

之前的 `co_od_decode_le()` 是把字节**组合成整数**；这里则是把整数**拆成小端字节**。这个测试回调本身不检查参数，调用时要保证指针有效、输出空间足够，且长度不超过 4 字节。





## 3：测试用的写回调

```c
static co_status_t fake_write(void *user, const uint8_t *data, uint8_t length)
{
    fake_value_t *value = user; /* 还原回调私有数据指针 */
    uint8_t i; /* 字节下标，从低字节开始处理 */

    value->value = 0u; /* 写入前清除旧值 */
    for (i = 0u; i < length; ++i) { /* 遍历有效字节，最低地址存低字节 */
        value->value |= ((uint32_t)data[i]) << (8u * i); /* 按小端顺序接收 */
    }
    ++value->writes; /* 记录一次写入 */
    return CO_OK; /* 本次操作成功 */
}
```

这个和刚才的 `fake_read()` **正好是反过来的**。

刚才：

```
从我们手写的模拟总线值 ：value →  写入到定义在函数外的全局数组data[0] data[1] 中
```

现在：写的话

```
从定义在外部的数组中data[] → 写入到模拟总线值output
```

也就是：

> `fake_write()` 的作用是：把外部传进来的字节数组 `data[]`，按照小端序重新拼成一个 `uint32_t`，保存到 `fake_value_t.value` 里面。



#### **① 先明确结构体类型和外面的变量**

前面定义过：

```c
typedef struct {
    uint32_t value;  /* 保存对象当前数值 */
    unsigned reads; /* 读取次数 */
    unsigned writes;/* 写入次数 */
} fake_value_t;
```

现在创建一个模拟对象和待写入数组：

```c
fake_value_t output = {0};
object.value = 100; /* 假设对象原来保存 100 */

uint8_t buffer[2] = {0x26, 0x07}; /* 小端表示整数 0x0726 */
```

然后调用：

```
fake_write(&output, buffer, 2);
```

三个参数对应：

| 函数形参 | 接收到的内容            |
| -------- | ----------------------- |
| `user`   | `output` 的地址         |
| `data`   | `buffer` 数组首元素地址 |
| `length` | `2`，处理两个字节       |



#### **② 创建指向外面结构体的指针**

```
fake_value_t *value = user;
```

这行创建的是**指针变量 `value`**，它指向外面的 `output`，没有创建新的结构体对象。

所以：

```c
value->value   /* 对应 output.value */
value->reads   /* 对应 output.reads */
value->writes  /* 对应 output.writes */
```

`value->value` 左边是指针名，右边是结构体成员名。



#### **③ 清除对象原来保存的数值**

```c
value->value = 0u;
```

相当于：

```
object.value = 0;
```

为什么要先清零？因为后面使用按位或 `|=` 逐个合入字节。如果不清零，旧数值中的某些 `1` 位可能残留，影响新结果。

这里只清除了数值成员，**没有清除 `reads` 和 `writes` 计数**。



#### **④ 按小端顺序组合整数**

```c
for (i = 0u; i < length; ++i) {
    value->value |= ((uint32_t)data[i]) << (8u * i);
}
```

这和前面学过的 `co_od_decode_le()` 使用同一种组合方法，只是结果直接保存到结构体成员中。

本次输入：

```
data[0] == 0x26;
data[1] == 0x07;
```

第一次循环，`i = 0`：

```
value->value |= ((uint32_t)0x26) << 0;
```

结果：

```
output.value = 0x00000026
```

第二次循环，`i = 1`：

```
value->value |= ((uint32_t)0x07) << 8;
```

也就是：

```
原来的值：0x00000026
合入的值：0x00000700
最终结果：0x00000726
```

所以：

```c
object.value == 0x0726; /* 十进制 1830 */
```



#### **⑤ 增加写入次数**

```
++value->writes;
```

相当于：

```
++object.writes;
```

原来是 `0`，现在变成 `1`，表示写回调执行了一次。



#### **⑥ 返回成功**

```
return CO_OK;
```

这次调用结束后：

```
object.value  == 1830;
object.writes == 1;
object.reads  == 0;
```

输入数组保持不变：

```
buffer[0] == 0x26;
buffer[1] == 0x07;
```

这里的：

```
const uint8_t *data
```

限制的是不能通过 `data` 修改输入数组，并不妨碍通过 `value` 修改 `output.value`。

这个测试回调只更新模拟变量，不操作真实 GPIO；指针、长度和存储空间需要由调用方保证有效。





## 4：读和写用于实机测试时

放到实机上，**读、写是以上位机访问 STM32 的对象为视角**：

```
读：上位机获取 STM32 的数据
写：上位机修改 STM32 的数据或配置
```

我们分别用 ADC 和 LED 举例。



#### **① 读取：上位机获取 STM32 的 ADC 值**

假设 STM32 已经采集 AI1，并保存：

```
analog_input_1 = 1830; /* 0x0726 */
```

过程是：

```c
上位机发送 SDO 请求：读取 0x6401:01（AI1）
    ↓
STM32 的 SDO 模块解析请求，找到对象条目
    ↓
调用 co_od_read()
    ↓
检查参数和长度，调用该条目的读取回调
    ↓
回调读取 analog_input_1
把 1830 拆成两个小端字节：26 07
写入 data[]
    ↓
SDO 模块把 data[] 装进响应报文
通过 CAN 发送给上位机
    ↓
上位机解析出 AI1 = 1830
```

你刚学的 `fake_read()` 模拟的就是其中这一段：

```
读取保存的整数 → 拆成小端字节 → 填入 data[]
```



#### **② 写入：上位机控制 STM32 的 LED**

上位机希望 DO1、DO3 打开，发送：

```
写入 0x6200:01（数字输出命令）
数值：0x05，即二进制 0000 0101
```

过程是：

```c
上位机发送 SDO 写请求
    ↓
STM32 的 SDO 模块解析请求，找到对象条目
    ↓
调用 co_od_write()
    ↓
检查权限、长度、状态和数值范围
    ↓
检查通过，调用该条目的写入回调
    ↓
回调从 data[] 取得 0x05
更新数字输出命令变量
    ↓
应用层根据命令设置 GPIO
DO1、DO3 为高电平，DO2、DO4 为低电平
    ↓
对应 LED 状态改变
```

你刚学的 `fake_write()` 模拟的是：

```
读取 data[] → 组合成整数 → 更新模拟变量
```

**变量改变不会自动改变引脚电平**，实机必须再由 GPIO 驱动执行输出。可以在回调中执行，也可以把命令交给 I/O 任务执行。如果采用任务处理，SDO 写成功响应也不一定表示引脚已经完成更新。

​                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         





## 5：测试：**`co_od_find()`**

```c
static int test_find(void)
{
    fake_value_t value = {0}; /* 模拟数值及读写次数均初始化为零 */
    const co_od_entry_t entries[] = { /* 建立只有一个数字输出条目的测试数组 */
        {0x6200u, 0x01u, CO_OD_UNSIGNED8, CO_OD_READ_WRITE,
         1u, 0u, 15u, 0u, fake_read, fake_write, &value}
    };
    const co_od_table_t table = {entries, 1u}; /* 表保存数组首地址和条目数量 */
    const co_od_entry_t *entry = NULL; /* 用于接收查找结果地址 */

    CHECK(co_od_find(NULL, 0x6200u, 1u, &entry) == CO_ERR_ARGUMENT); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(co_od_find(&table, 0x6200u, 1u, NULL) == CO_ERR_ARGUMENT); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(co_od_find(&table, 0x6200u, 2u, &entry) == CO_ERR_OD_NOT_FOUND); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(entry == NULL); /* 未知对象时输出保持空指针 */
    CHECK(co_od_find(&table, 0x6200u, 1u, &entry) == CO_OK); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(entry == &entries[0]); /* 结果指向原数组条目，不是新副本 */
    CHECK(entry->index == 0x6200u && entry->subindex == 1u); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    return 0; /* 本测试组全部检查通过 */
}
```

`test_find()` 专门测试：**`co_od_find()` 能否正确拒绝错误参数、报告对象不存在，以及返回正确的条目地址。**

我们先看它准备了哪些变量，再逐条看 `CHECK`。



#### **① 创建模拟数据变量 `value`**

```
fake_value_t value = {0};
```

这里没有 `*`，所以创建的是一个**结构体变量**，不是指针。

它里面的三个成员都是零：

```c
value.value  == 0;
value.reads  == 0;
value.writes == 0;
```

它是后面条目绑定的模拟存储。不过本函数只测试查找，**不会调用读写回调**。



#### **② 创建对象条目数组 `entries`**

```c
const co_od_entry_t entries[] = {
    {0x6200u, 0x01u, CO_OD_UNSIGNED8, CO_OD_READ_WRITE,
     1u, 0u, 15u, 0u, fake_read, fake_write, &value}
};
```

`entries[]` 是结构体数组，其中只有一个元素：

```
entries[0]
```

大括号中的值，按照 `co_od_entry_t` 成员的声明顺序初始化：

| 初始化值           | 对应成员           | 含义                       |
| ------------------ | ------------------ | -------------------------- |
| `0x6200u`          | `index`            | 数字输出对象索引           |
| `0x01u`            | `subindex`         | 子索引 1                   |
| `CO_OD_UNSIGNED8`  | `type`             | 8 位无符号整数             |
| `CO_OD_READ_WRITE` | `access`           | 可读写                     |
| `1u`               | `length`           | 1 字节                     |
| `0u`               | `min_value`        | 最小值 0                   |
| `15u`              | `max_value`        | 最大值 15                  |
| `0u`               | `write_preop_only` | 不施加“仅预运行可写”的限制 |
| `fake_read`        | `read`             | 读取回调地址               |
| `fake_write`       | `write`            | 写入回调地址               |
| `&value`           | `user`             | 模拟数据变量的地址         |

所以这段代码是在建立一条记录：

```
0x6200:01 → 数字输出命令，1 字节，可读写，范围 0～15
```

`const` 表示这些条目描述不能直接修改，但不妨碍回调通过 `user` 更新外面的模拟数据。



#### **③ 创建对象表变量 `table`**

```c
const co_od_table_t table = {entries, 1u};		//传入我们创建的对象条目结构体数组的首地址，count=1，对象条目就1条
```

回忆表结构：

```c
typedef struct {
    const co_od_entry_t *entries;
    size_t count;
} co_od_table_t;
```

所以这次初始化相当于：

```
table.entries → 保存 entries 数组首元素地址
table.count   → 1，表示有一条记录
```

注意：

```
entries        /* 外面的数组名 */
table.entries  /* 表结构体中的指针成员 */
```

名字相同，但一个是数组，一个是保存数组地址的成员。





#### **④ 创建接收查找结果的指针**

```c
const co_od_entry_t *entry = NULL;
```

这里有 `*`，创建的是**指针变量**，用于保存找到的条目地址。

查找前：

```
entry → NULL
```

查找成功后，我们期望：

```
entry → entries[0]
```

下面就开始测试。



#### **⑤ 不提供表地址，应返回参数错误**

```c
CHECK(co_od_find(NULL, 0x6200u, 1u, &entry)//第一个参数应该填table表的地址
      == CO_ERR_ARGUMENT);
```

第一个参数故意传 `NULL`，没有表可以查。

预期：

```
CO_ERR_ARGUMENT
```

返回这个错误码，说明函数正确拒绝了错误输入，**本项测试通过**。



#### **⑥ 不提供结果输出地址，应返回参数错误**

```c
CHECK(co_od_find(&table, 0x6200u, 1u, NULL)//NULL本来应该填&entry的，entry就是我们创建的**found
      == CO_ERR_ARGUMENT);
```

这次有表，但第四个参数是 `NULL`。

函数无法把查找结果写回外面的found中，因此应返回参数错误。

这里要区分：

```
entry = NULL; /* 合法：结果指针暂时不指向任何条目 */
```

和：

```
co_od_find(..., NULL); /* 非法：没有提供结果指针变量的地址 */
```

正常调用应传 `&entry`。



#### **⑦ 查找不存在的子索引**

```c
CHECK(co_od_find(&table, 0x6200u, 2u, &entry)
      == CO_ERR_OD_NOT_FOUND);
```

表里只有：

```
0x6200:01
```

现在查找：

```
0x6200:02
```

所以应该返回：

```
CO_ERR_OD_NOT_FOUND
```

随后：

```
CHECK(entry == NULL);
```

确认没有找到时，结果指针为空。



#### **⑧ 查找存在的对象**

```
CHECK(co_od_find(&table, 0x6200u, 1u, &entry)
      == CO_OK);
```

这次查找的 `0x6200:01` 确实存在。

函数内部找到第零个条目后，会把它的地址写入外面的 `entry`，然后返回 `CO_OK`。



#### **⑨ 检查返回的地址是否正确**

```
CHECK(entry == &entries[0]);
```

两边分别是：

```c
entry       → 查找函数写回的地址
&entries[0] → 数组第一个条目的实际地址
```

相等就说明：**结果确实指向原数组中的第一个条目。**



#### **⑩ 检查这个条目的内容**

```
CHECK(entry->index == 0x6200u &&
      entry->subindex == 1u);
```

由于 `entry` 已经指向 `entries[0]`：

```c
entry->index     /* 对应 entries[0].index */
entry->subindex  /* 对应 entries[0].subindex */
```

两个成员都符合预期，本项检查通过。

最后：

```
return 0;
```

表示本组测试全部通过。整个函数完成的是**找条目、检查地址和条目描述**，并没有读取或写入条目背后的模拟数值。



## 6：测试：test_read( )

```c
static int test_read(void)
{
    fake_value_t value = {0x1234u, 0u, 0u}; /* 使用高低字节不同的值验证小端顺序 */
    const co_od_entry_t entry = { /* 只读 16 位对象，绑定 fake_read 和模拟存储 */
        0x6401u, 0x01u, CO_OD_UNSIGNED16, CO_OD_READ_ONLY,
        2u, 0u, 4095u, 0u, fake_read, NULL, &value
    };
    uint8_t data[2] = {0u, 0u}; /* 读取结果缓冲区，先清零 */

    CHECK(co_od_read(NULL, data, 2u) == CO_ERR_ARGUMENT); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(co_od_read(&entry, NULL, 2u) == CO_ERR_ARGUMENT); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(co_od_read(&entry, data, 1u) == CO_ERR_OD_LENGTH); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(co_od_read(&entry, data, 2u) == CO_OK); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(data[0] == 0x34u && data[1] == 0x12u); /* 低字节在前，高字节在后 */
    CHECK(value.reads == 1u); /* 只有合法读取调用过一次回调 */
    return 0; /* 本测试组全部检查通过 */
}
```

用来测试：**错误参数会不会被拒绝，合法读取能不能把模拟对象的值按小端顺序放入数组。**

先看三个变量，再看每一条检查。



#### **① 创建模拟数据变量 `value`**

```c
fake_value_t value = {0x1234u, 0u, 0u};
```

它是一个结构体变量，按照成员顺序初始化：

```c
value.value  = 0x1234; /* 模拟对象当前数值 */
value.reads  = 0;      /* 读取次数 */
value.writes = 0;      /* 写入次数 */
```

这里特意使用两个不同的字节：

```c
高字节：0x12
低字节：0x34
```

这样容易检查后面输出的顺序是否正确。

注意：`0x1234` 等于十进制 `4660`，超过真实 12 位 ADC 的最大值 `4095`。这里是用于测试字节序的模拟值，不是合法 ADC 采集示例。 当前 test_read() 不检查读取值的范围。



#### **② 创建对象条目 `entry`**

```c
const co_od_entry_t entry = {
    0x6401u, 0x01u, CO_OD_UNSIGNED16, CO_OD_READ_ONLY,
    2u, 0u, 4095u, 0u, fake_read, NULL, &value
};
```

这里的 `entry` 是**结构体变量本身**，不是指针，也不是数组。

它描述：

| 成员                    | 本次设置                          |
| ----------------------- | --------------------------------- |
| `index`                 | `0x6401`                          |
| `subindex`              | `0x01`                            |
| `type`                  | `CO_OD_UNSIGNED16`，16 位无符号数 |
| `access`                | `CO_OD_READ_ONLY`，只读           |
| `length`                | `2` 字节                          |
| `min_value / max_value` | `0 / 4095`，当前用于写入范围检查  |
| `write_preop_only`      | `0`                               |
| `read`                  | `fake_read` 的函数地址            |
| `write`                 | `NULL`，没有写回调                |
| `user`                  | `&value`，模拟数据变量的地址      |

最重要的绑定是：

```c
entry.read = fake_read;
entry.user = &value;
```

这是在说明：读取这个条目时，调用 `fake_read(void *user, uint8_t *data, uint8_t length)`，

传入的第一个实参，就是让它从 `fake_value_t value = {0x1234u, 0u, 0u};` 中取得数据。





#### **③ 创建接收结果的数组**

```c
uint8_t data[2] = {0u, 0u};
```

这个数组用于接收读取结果，初始为：

```
data[0] = 0
data[1] = 0
```

三个变量的职责就清楚了：

```
value → 保存模拟对象的实际数值
entry → 描述这个对象，并绑定读回调和 &value
data  → 接收读取出来的字节
```



#### **④ 测试条目地址为空**

```c
CHECK(co_od_read(NULL, data, 2u) == CO_ERR_ARGUMENT);
```

第一个参数故意传 `NULL`，没有提供对象条目地址。

`co_od_read()` 应该立即返回参数错误，不能调用 `fake_read()`。



#### **⑤ 测试输出数组地址为空**

```c
CHECK(co_od_read(&entry, NULL, 2u) == CO_ERR_ARGUMENT);
```

这次条目地址有效，但没有提供输出缓冲区。

没有地方存放结果，因此也应返回参数错误，不调用回调。



#### **⑥ 测试长度不匹配**

```
CHECK(co_od_read(&entry, data, 1u) == CO_ERR_OD_LENGTH);
```

条目是 `UNSIGNED16`：

```
标准长度：2 字节
条目长度：2 字节
本次传入长度：1 字节
```

长度不一致，因此返回 `CO_ERR_OD_LENGTH`。

注意，虽然实际数组 `data` 有两个元素，函数接收到的 `data_length` 却是 `1`。**它按传入的长度检查，不会自动获知数组大小。**



#### **⑦ 测试正常读取**

```
CHECK(co_od_read(&entry, data, 2u) == CO_OK);
```

这次地址和长度都正确，于是执行 `co_od_read()` 最后的一句：

```c
return entry->read(entry->user, data, data_length);
```

结合当前条目的绑定，实际相当于：

```
fake_read(&value, data, 2);
```

进入 `fake_read()` 后：

```
读取 value.value，也就是 0x1234
    ↓
拆成小端字节
    ↓
data[0] = 0x34
data[1] = 0x12
    ↓
value.reads 加一
    ↓
返回 CO_OK
```



#### **⑧ 检查输出的字节**

```
CHECK(data[0] == 0x34u && data[1] == 0x12u);
```

确认读取结果为：

```
低字节在前：0x34
高字节在后：0x12
```

如果顺序反了，或者内容不对，这一项就失败。



#### **⑨ 检查读取回调只执行一次**

```
CHECK(value.reads == 1u);
```

前面一共调用了四次 `co_od_read()`：

| 调用情况     | 是否进入 `fake_read()` |
| ------------ | ---------------------- |
| 条目地址为空 | 否                     |
| 输出地址为空 | 否                     |
| 长度错误     | 否                     |
| 参数全部正确 | 是                     |

因此读取次数应该是 `1`。

最后：

```
return 0;
```

表示这一组全部检查通过。



## 7：测试 ：test_write()

```c
static int test_write(void)
{
    fake_value_t output = {0u, 0u, 0u}; /* 模拟数字输出存储 */
    fake_value_t heartbeat = {0u, 0u, 0u}; /* 名字沿用 heartbeat，实际在此绑定 PDO 抑制时间测试条目 */
    const co_od_entry_t read_only = { /* 只读条目不绑定写回调 */
        0x6000u, 0x01u, CO_OD_UNSIGNED8, CO_OD_READ_ONLY,
        1u, 0u, 15u, 0u, fake_read, NULL, &output
    };
    const co_od_entry_t writable = { /* 数字输出允许写入 0 至 15 */
        0x6200u, 0x01u, CO_OD_UNSIGNED8, CO_OD_READ_WRITE,
        1u, 0u, 15u, 0u, fake_read, fake_write, &output
    };
    const co_od_entry_t preop_only = { /* PDO 抑制时间条目，仅预运行状态可写 */
        0x1800u, 0x03u, CO_OD_UNSIGNED16, CO_OD_READ_WRITE,
        2u, 0u, 65535u, 1u, fake_read, fake_write, &heartbeat
    };
    uint8_t output_data[1] = {0x05u}; /* 合法数字输出命令，bit0 和 bit2 为 1 */
    uint8_t too_large[1] = {0x10u}; /* 16 超过四路输出允许的最大值 15 */
    uint8_t heartbeat_data[2] = {0x64u, 0x00u}; /* 小端数值 100，此处用于 PDO 抑制时间 */

    CHECK(co_od_write(&read_only, CO_NMT_OPERATIONAL,
                      output_data, 1u) == CO_ERR_OD_READ_ONLY); /* 检查多行写入调用的返回状态 */
    CHECK(output.writes == 0u); /* 确认错误输入没有调用写回调 */
    CHECK(co_od_write(&writable, CO_NMT_OPERATIONAL,
                      output_data, 0u) == CO_ERR_OD_LENGTH); /* 检查多行写入调用的返回状态 */
    CHECK(co_od_write(&writable, CO_NMT_OPERATIONAL,
                      too_large, 1u) == CO_ERR_OD_VALUE); /* 检查多行写入调用的返回状态 */
    CHECK(output.writes == 0u); /* 确认错误输入没有调用写回调 */
    CHECK(co_od_write(&writable, CO_NMT_OPERATIONAL,
                      output_data, 1u) == CO_OK); /* 检查多行写入调用的返回状态 */
    CHECK(output.value == 5u && output.writes == 1u); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(co_od_write(&preop_only, CO_NMT_OPERATIONAL,
                      heartbeat_data, 2u) == CO_ERR_OD_STATE); /* 检查多行写入调用的返回状态 */
    CHECK(heartbeat.writes == 0u); /* 确认错误输入没有调用写回调 */
    CHECK(co_od_write(&preop_only, CO_NMT_PRE_OPERATIONAL,
                      heartbeat_data, 2u) == CO_OK); /* 检查多行写入调用的返回状态 */
    CHECK(heartbeat.value == 100u && heartbeat.writes == 1u); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    return 0; /* 本测试组全部检查通过 */
}
```

`test_write()` 用来测试：**错误的写入请求能不能被拦住，合法请求能不能通过回调更新模拟变量。**



我们继续先看变量，再看检查。

#### **① 创建两个模拟数据变量**

```c
fake_value_t output = {0u, 0u, 0u};
fake_value_t heartbeat = {0u, 0u, 0u};
```

它们都是 `fake_value_t` 结构体变量，各自包含：

```c
.value
.reads
.writes
```

例如：

```
output.value  == 0;
output.reads  == 0;
output.writes == 0;
```

这里需要提醒一下：**`heartbeat` 这个变量名起得不准确。** 本测试实际把它绑定到了 PDO 的抑制时间对象，不是心跳周期。你先把它理解为“第二个模拟存储变量”。



#### **② 创建只读条目 `read_only`**

```c
const co_od_entry_t read_only = {
    0x6000u, 0x01u, CO_OD_UNSIGNED8, CO_OD_READ_ONLY,
    1u, 0u, 15u, 0u, fake_read, NULL, &output
};
```

关键配置是：

```
对象：0x6000:01，数字输入
类型：UNSIGNED8
权限：只读
长度：1 字节
读取回调：fake_read
写入回调：NULL
辅助数据地址：&output
```

这条记录专门用于测试：**对只读对象发起写入时，应当拒绝。**

虽然这里使用了名为 `output` 的模拟变量，但只是测试中复用了存储，不能据此把 `0x6000:01` 理解成数字输出对象。



#### **③ 创建可写条目 `writable`**

```c
const co_od_entry_t writable = {
    0x6200u, 0x01u, CO_OD_UNSIGNED8, CO_OD_READ_WRITE,
    1u, 0u, 15u, 0u, fake_read, fake_write, &output
};
```

这次描述的是数字输出命令：

```c
对象：0x6200:01
长度：1 字节
权限：可读写
允许值：0～15
写入回调：fake_write
辅助数据地址：&output
```

因此，合法写入最后会通过 `fake_write()` 修改：

```
output.value
```

并增加：

```
output.writes
```



#### **④ 创建只能在预运行状态写入的条目**

```c
const co_od_entry_t preop_only = {
    0x1800u, 0x03u, CO_OD_UNSIGNED16, CO_OD_READ_WRITE,
    2u, 0u, 65535u, 1u, fake_read, fake_write, &heartbeat
};
```

这里是 TPDO1 的抑制时间对象：

```
对象：0x1800:03
类型：UNSIGNED16
长度：2 字节
范围：0～65535
write_preop_only：1
辅助数据地址：&heartbeat
```

其中：

```
write_preop_only = 1
```

表示只有：

```
CO_NMT_PRE_OPERATIONAL
```

状态下允许写入。



#### **⑤ 准备三份输入数据**

```c
uint8_t output_data[1] = {0x05u};
uint8_t too_large[1] = {0x10u};
uint8_t heartbeat_data[2] = {0x64u, 0x00u};
```

分别用于：

| 数组             | 表示的整数 | 测试用途               |
| ---------------- | ---------- | ---------------------- |
| `output_data`    | 5          | 合法数字输出命令       |
| `too_large`      | 16         | 超过数字输出最大值 15  |
| `heartbeat_data` | 100        | 用于预运行状态限制测试 |

最后一个数组按小端组合为：

```
{0x64, 0x00} → 0x0064 → 100
```

它在这里写入 PDO 抑制时间对象，单位为 `100 μs`，所以数值 `100` 对应 `10 ms`。

接下来逐条检查。



#### **⑥ 只读对象拒绝写入**

```c#
CHECK(co_od_write(&read_only, CO_NMT_OPERATIONAL,
                  output_data, 1u) == CO_ERR_OD_READ_ONLY);

CHECK(output.writes == 0u);
```

传入：

```
条目地址：&read_only
当前状态：Operational
输入数据：0x05
输入长度：1
```

长度正确，但权限为只读，因此返回：

```
CO_ERR_OD_READ_ONLY
```

后面检查 `output.writes == 0`，确认没有执行写回调。



#### **⑦ 错误长度拒绝写入**

```
CHECK(co_od_write(&writable, CO_NMT_OPERATIONAL,
                  output_data, 0u) == CO_ERR_OD_LENGTH);
```

这个对象需要 1 字节，本次却声明输入长度为 `0`。

因此在长度检查处返回：

```
CO_ERR_OD_LENGTH
```

不会进入 `fake_write()`。



#### **⑧ 越界数值拒绝写入**

```c
CHECK(co_od_write(&writable, CO_NMT_OPERATIONAL,
                  too_large, 1u) == CO_ERR_OD_VALUE);

CHECK(output.writes == 0u);
```

长度为 1，符合要求，但：

```
输入值：0x10，即 16
最大允许值：15
```

因此返回数值错误。

到这里，前三次写入都被检查拦住，所以：

```
output.writes == 0
```



#### **⑨ 合法写入应该成功**

```c
CHECK(co_od_write(&writable, CO_NMT_OPERATIONAL,
                  output_data, 1u) == CO_OK);
```

本次条件全部满足：

```
权限：可写
长度：1 字节
数值：5，在 0～15 内
预运行限制：未启用
写回调：存在
```

最后调用：

```
fake_write(&output, output_data, 1);
```

回调执行后：

```c
output.value  == 5;
output.writes == 1;
```

所以接着检查：

```
CHECK(output.value == 5u && output.writes == 1u);
```

这里同时确认：**写入的值正确，回调执行了一次。**



#### **⑩ 运行状态下，拒绝修改受限条目**

```c
CHECK(co_od_write(&preop_only, CO_NMT_OPERATIONAL,
                  heartbeat_data, 2u) == CO_ERR_OD_STATE);

CHECK(heartbeat.writes == 0u);
```

这个条目要求：

```
只能在 Pre-operational 状态写入
```

本次却传入：

```
CO_NMT_OPERATIONAL
```

因此返回 `CO_ERR_OD_STATE`，第二个模拟变量的写入次数仍为 `0`。



#### **⑪ 预运行状态下，允许修改受限条目**

```c
CHECK(co_od_write(&preop_only, CO_NMT_PRE_OPERATIONAL,
                  heartbeat_data, 2u) == CO_OK);
```

同一份数据、同一个条目，这次传入正确状态，于是调用：

```
fake_write(&heartbeat, heartbeat_data, 2);
```

回调把：

```
64 00 → 100
```

保存到模拟变量中。

因此：

```
CHECK(heartbeat.value == 100u && heartbeat.writes == 1u);
```

确认数值和调用次数都正确。

最后：

```
return 0;
```

表示本组全部通过。这里的 NMT 状态是测试直接传给函数的枚举值，**没有执行真实的 NMT 状态切换，也没有控制实际引脚。**



函数开头创建了两个独立的局部结构体变量：

```c
fake_value_t output = {0u, 0u, 0u};
fake_value_t heartbeat = {0u, 0u, 0u};
```

它们各有自己的成员：

```c
output                    heartbeat
├── value = 0             ├── value = 0
├── reads = 0             ├── reads = 0
└── writes = 0            └── writes = 0
```

**第一次合法写入：**

```
co_od_write(&writable, CO_NMT_OPERATIONAL, output_data, 1u);
```

`writable.user` 保存的是 `&output`，所以最终调用：

```
fake_write(&output, output_data, 1);
```

改变的是：

```
output.writes = 1;
```

此时：

```
output.writes    = 1
heartbeat.writes = 0
```

**后面合法写入 `preop_only`：**

```c
co_od_write(&preop_only, CO_NMT_PRE_OPERATIONAL,
            heartbeat_data, 2u);
```

`preop_only.user` 保存的是 `&heartbeat`，所以最终调用：

```
fake_write(&heartbeat, heartbeat_data, 2);
```

改变的是：

```
heartbeat.writes = 1;
```

最终：

```c
output.writes    = 1
heartbeat.writes = 1
```

所以，**计数保存在 `test_write()` 的两个局部结构体变量中，不是回调共用的一个计数器。** `fake_write()` 接收到谁的地址，就增加谁的 `writes`。



## 8：测试test_entry_definition()

```c
static int test_entry_definition(void)
{
    fake_value_t value = {0u, 0u, 0u}; /* 为条目提供模拟存储和计数 */
    co_od_entry_t invalid_type = { /* 故意使用不支持的类型标记 3 */
        0x2000u, 0x00u, (co_od_type_t)3, CO_OD_READ_ONLY,
        3u, 0u, 0u, 0u, fake_read, NULL, &value
    };
    co_od_entry_t invalid_length = { /* 故意把 16 位类型描述成 1 字节 */
        0x2000u, 0x01u, CO_OD_UNSIGNED16, CO_OD_READ_ONLY,
        1u, 0u, 0u, 0u, fake_read, NULL, &value
    };
    uint8_t data[2] = {0u, 0u}; /* 读取结果缓冲区，先清零 */

    CHECK(co_od_read(&invalid_type, data, 3u) == CO_ERR_OD_LENGTH); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(co_od_read(&invalid_length, data, 1u) == CO_ERR_OD_LENGTH); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    invalid_type.type = CO_OD_UNSIGNED8; /* 先恢复合法类型 */
    invalid_type.length = 1u; /* 同步恢复合法长度 */
    invalid_type.read = NULL; /* 单独构造缺少读取回调的情况 */
    CHECK(co_od_read(&invalid_type, data, 1u) == CO_ERR_OD_CALLBACK); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    return 0; /* 本测试组全部检查通过 */
}
```

这个 `test_entry_definition()` 测试的是：**对象条目本身配置错误时，`co_od_read()` 能不能发现并拒绝读取。**



这里分别构造三种错误：

```c
类型不支持
类型与长度不匹配
缺少读取回调
```



#### **① 创建模拟存储变量**

```c
fake_value_t value = {0u, 0u, 0u};
```

它是之前熟悉的结构体变量：

```c
value.value  == 0;
value.reads  == 0;
value.writes == 0;
```

后面的条目通过 `&value` 绑定它。不过这组测试都应该在检查阶段失败，不会执行 `fake_read()`。



#### **② 创建类型错误的条目 `invalid_type`**

```c
co_od_entry_t invalid_type = {
    0x2000u, 0x00u, (co_od_type_t)3, CO_OD_READ_ONLY,
    3u, 0u, 0u, 0u, fake_read, NULL, &value
};
```

这里重点看：

```
(co_od_type_t)3
```

这是把整数 `3` 强制转换为 `co_od_type_t` 类型，故意构造一个不支持的类型标记。

回忆我们定义的枚举：

```
CO_OD_UNSIGNED8  = 1
CO_OD_UNSIGNED16 = 2
CO_OD_UNSIGNED32 = 4
```

**没有定义标记 `3`，也没有支持 3 字节对象。** 强制转换只是让这个值以枚举类型传入，并不会使它变成受支持的类型。

这里还设置：

```
invalid_type.length = 3;
```

用于构造这个错误条目。`0x2000:00` 只是测试地址，不表示已经实现了正式的厂商对象。



#### **③ 创建长度错误的条目 `invalid_length`**

```c
co_od_entry_t invalid_length = {
    0x2000u, 0x01u, CO_OD_UNSIGNED16, CO_OD_READ_ONLY,
    1u, 0u, 0u, 0u, fake_read, NULL, &value
};
```

它的类型合法：

```
invalid_length.type = CO_OD_UNSIGNED16;
```

应该占 **2 字节**，但条目却填写：

```
invalid_length.length = 1;
```

因此条目描述自相矛盾：

```
类型说需要 2 字节
length 却写了 1 字节
```



#### **④ 准备输出数组**

```c
uint8_t data[2] = {0u, 0u};
```

创建两个字节的数组，准备接收读取结果。



#### **⑤ 检查不支持的类型**

```c
CHECK(co_od_read(&invalid_type, data, 3u)
      == CO_ERR_OD_LENGTH);
```

进入 `co_od_read()` 后：

```
expected_length = co_od_type_length(entry->type);
```

传入的类型标记为 `3`，在 `co_od_type_length()` 中匹配不到已支持的类型，于是：

```
expected_length = 0;
```

随后命中：

```
if (expected_length == 0u || ...)
```

返回：

```
CO_ERR_OD_LENGTH
```

这里虽然实际数组只有 2 字节，却故意传了长度 `3`，但**当前函数会在类型检查时返回，不会调用回调写入数组**。正常使用时，不能把长度填写得比实际缓冲区还大。



#### **⑥ 检查类型和条目长度不一致**

```c
CHECK(co_od_read(&invalid_length, data, 1u)
      == CO_ERR_OD_LENGTH);
```

这次：

```c
expected_length = 2  ← 根据 UNSIGNED16 得到
entry->length   = 1  ← 条目中故意填错
data_length     = 1  ← 本次调用传入
```

虽然后两个长度相等，但：

```
entry->length != expected_length
```

成立，所以仍然返回 `CO_ERR_OD_LENGTH`。

这就证明：**调用长度与条目长度相等还不够，它们还必须符合数据类型规定的长度。**



#### **⑦ 修正类型和长度，再单独制造回调错误**

```c
invalid_type.type = CO_OD_UNSIGNED8;
invalid_type.length = 1u;
invalid_type.read = NULL;
```

这里继续使用前面的 `invalid_type` 变量：

```
原来：类型标记为 3，长度为 3
现在：类型为 UNSIGNED8，长度为 1
```

类型和长度已经合法，但故意把读取函数地址清空：

```
invalid_type.read = NULL;
```

注意，这些条目变量没有声明为 `const`，所以这里可以修改成员。



#### **⑧ 检查缺少读取回调**

```c
CHECK(co_od_read(&invalid_type, data, 1u)
      == CO_ERR_OD_CALLBACK);
```

这次指针和长度检查都通过，执行到：

```
if (entry->read == NULL) {
    return CO_ERR_OD_CALLBACK;
}
```

因为没有绑定读取函数，所以返回回调错误。

先修正类型和长度，是为了让程序能够执行到这一步；否则还会提前返回长度错误。

最后：

```
return 0;
```

表示这三种错误都被正确识别，本组测试通过。