# 函数解析



## 1：返回对象字节数

```c
static uint8_t co_od_type_length(co_od_type_t type)
{
    switch (type)
    { /* 按对象类型选择标准长度 */
    case CO_OD_UNSIGNED8:
        return 1u; /* 8 位无符号对象占 1 字节 */
    case CO_OD_UNSIGNED16:
        return 2u; /* 16 位无符号对象占 2 字节 */
    case CO_OD_UNSIGNED32:
        return 4u; /* 32 位无符号对象占 4 字节 */
    default:
        return 0u; /* 用 0 表示不支持的类型 */
    }
}
```

它的用途是：传入对象的数据类型，返回该类型占用的字节数。

```c
例如在 co_od.c 内部可以这样调用：
uint8_t length;

length = co_od_type_length(CO_OD_UNSIGNED8);
执行关系：
CO_OD_UNSIGNED8
        │
        ▼
co_od_type_length(type)
        │
        ▼
返回 1
        │
        ▼
length = 1
```



#### **① 函数头**

```
static uint8_t co_od_type_length(co_od_type_t type)
```

| 部分                | 含义                                    |
| ------------------- | --------------------------------------- |
| `static`            | 仅供当前 `.c` 文件内部调用              |
| `uint8_t`           | 返回一个无符号 8 位整数，这里表示字节数 |
| `co_od_type_length` | 函数名称                                |
| `co_od_type_t type` | 接收前面学过的对象类型枚举值            |

这里的 `type` 是普通枚举变量，**传入的是类型标记，不是地址**。

例如：

```
uint8_t length = co_od_type_length(CO_OD_UNSIGNED16);
```

执行后：

```
length == 2
```



#### **② 根据 `type` 选择分支**

```
switch (type) {
```

意思是：检查 `type` 的值，与下面的 `case` 比较。

```
case CO_OD_UNSIGNED8:
    return 1u;
```

如果传入 `CO_OD_UNSIGNED8`，返回 `1`，表示占用 1 字节。

另外两个同理：

```
CO_OD_UNSIGNED16 → 返回 2，表示 2 字节
CO_OD_UNSIGNED32 → 返回 4，表示 4 字节
```

`u` 表示无符号整数常量。



#### **③ 为什么没有 `break`？**

因为每个分支都执行了：

```
return ...;
```

`return` 会直接退出整个函数，不会继续执行后面的分支，所以不需要再写 `break`。



#### **④ `default` 处理不支持的类型**

```
default:
    return 0u;
```

当 `type` 不属于前面的三个类型时，返回 `0`。

这里的 `0` 表示不支持这个类型，不是说它是一个合法的零字节对象。后面的读写函数会据此拒绝操作。

另外，枚举中虽然已经把三个标记定义为 `1、2、4`，这里仍然使用 `switch`，是为了明确识别支持的类型，避免把其他数值直接当成合法长度。





## 2：将多个字节组合成一个整数

```c
static uint32_t co_od_decode_le(const uint8_t *data, uint8_t length)
{
    uint32_t value = 0u; /* 从全零开始组合整数 */
    uint8_t i;           /* 字节下标，从低字节开始处理 */

    for (i = 0u; i < length; ++i)
    {                                             /* 遍历有效字节，最低地址存低字节 */
        value |= ((uint32_t)data[i]) << (8u * i); /* 先转为 32 位，再左移 0/8/16/24 位并合入结果 */
    }
    return value; /* 返回解码后的整数 */
}
```

这个函数的作用是：把按小端顺序存放的几个字节，组合成一个整数。 把 `{0x34, 0x12}` 还原成整数 `0x1234`。

名字中的：

```
decode → 解码
le     → Little Endian，小端字节序
```

例如：

```
输入字节：0x34、0x12
输出整数：0x1234
```

我们逐行看。



#### **① 函数头和两个参数**

```c
static uint32_t co_od_decode_le(const uint8_t *data,
                                uint8_t length)
```

- `static`：只在当前 `.c` 文件中使用。
- `uint32_t`：返回组合后的整数，最多容纳 4 字节。
- `data`：输入字节数组的首元素地址。
- `length`：本次要处理多少个字节。

例如：

```c
uint8_t bytes[2] = {0x34, 0x12};

//调用实例
uint32_t result = co_od_decode_le(bytes, 2);
```

进入函数后：

```c
data[0] == 0x34
data[1] == 0x12
length  == 2
```

`const` 表示这个函数只读取这些字节，不通过 `data` 修改它们。



#### **② 准备存放结果的变量**

```c
uint32_t value = 0u;
uint8_t i;
```

`value` 用于保存组合结果，初始为：

```
0x00000000
```

`i` 是数组下标，用来依次访问：

```
data[0]
data[1]
```



#### **③ 遍历输入字节**

```c
for (i = 0u; i < length; ++i)
```

当 `length = 2` 时，循环执行两次：

```
第一次：i = 0
第二次：i = 1
```

接下来是核心语句：

```
value |= ((uint32_t)data[i]) << (8u * i);
```

我们把它拆开。



#### **④ `(uint32_t)data[i]`：先转换类型**

```
(uint32_t)data[i]
```

把当前的 8 位字节转换成 32 位无符号整数，方便后面移动到结果中的高位。

例如：

```
data[0]：         0x34
转换为 uint32_t：0x00000034
```

转换不会改变它的数值，也不会修改原数组。



#### **⑤ `<< (8u \* i)`：移动到对应的字节位置**

`<<` 是左移运算符。每个字节有 8 位，所以每增加一个数组下标，就多左移 8 位。

| `i`  | 读取的数据 | 左移位数 | 左移后       |
| ---- | ---------- | -------- | ------------ |
| 0    | `0x34`     | 0        | `0x00000034` |
| 1    | `0x12`     | 8        | `0x00001200` |

小端顺序要求：**数组前面的字节放在整数低位，后面的字节放在高位。**

因此 `data[0]` 不移动，`data[1]` 左移 8 位。



#### **⑥ `|=`：把这个字节合入结果**

```
value |= 某个数;
```

在这里可以理解为：

```
value = value | 某个数;
```

`|` 是**按位或**：对应位置只要有一个是 `1`，结果就是 `1`。

第一次循环：

```
value 原来：  0x00000000
合入：        0x00000034
结果：        0x00000034
```

第二次循环：

```
value 原来：  0x00000034
合入：        0x00001200
结果：        0x00001234
```

两个字节放在不同位置，因此组合成了完整整数。



#### **⑦ 返回结果**

```
return value;
```

所以前面的调用：

```
uint8_t bytes[2] = {0x34, 0x12};
uint32_t result = co_od_decode_le(bytes, 2);
```

执行后：

```
result == 0x1234
```

这个函数只负责组合整数。**它本身不检查空指针或长度是否合法**；当前代码由调用它的 `co_od_write()` 先检查，确保输入有效，长度为 `1、2、4`，再交给它解码。



#### ⑧小端输出

**低字节放在前面，高字节放在后面。**

例如整数：

```
0x1234
```

拆成两个字节：

```
高字节：0x12
低字节：0x34
```

小端存放就是：

```
data[0] = 0x34; /* 低字节 */
data[1] = 0x12; /* 高字节 */
```

注意：**调整的是字节顺序，不会把字节内部的位反过来**，所以 `0x34` 仍然是 `0x34`。





## 3：寻找字典条目地址

```c
co_status_t co_od_find(const co_od_table_t *table, uint16_t index,
                       uint8_t subindex, const co_od_entry_t **entry)
{
    size_t i; /* 对象数组下标 */

    if (table == NULL || entry == NULL)
    {                           /* 检查表地址和输出地址；此分支不会清空输出 */
        return CO_ERR_ARGUMENT; /* 参数无效，立即结束 */
    }
    *entry = NULL; /* 解引用输出地址，进入时先第一次清空调用者的 found 指针 ，防止有初始值*/
    if (table->entries == NULL && table->count != 0u)
    {                           /* 非空表必须有数组；空表允许 entries 为 NULL */
        return CO_ERR_ARGUMENT; /* 参数无效，立即结束 */
    }
    for (i = 0u; i < table->count; ++i)
    { /* 只扫描 count 个有效条目 */
        if (table->entries[i].index == index &&
            table->entries[i].subindex == subindex)
        {                                /* 索引和子索引必须同时匹配 */
            *entry = &table->entries[i]; /* 把条目地址写入调用者的指针变量，不复制条目 */
            return CO_OK;                /* 本次操作成功 */
        }
    }
    return CO_ERR_OD_NOT_FOUND; /* 遍历结束仍无匹配项 */
}
```

函数作用：

`co_od_find()`：根据 `Index + Sub-index`，在 CANopen 的对象字典表里找到对应的对象条目，并把这个条目的地址交给调用者。

```c
它解决什么问题？

假设你的对象字典里有：

co_od_entry_t od_table[] =
{
    {0x1000, 0, ...},   // Device Type
    {0x1017, 0, ...},   // Heartbeat
    {0x6000, 0, ...},   // DI
    {0x6200, 0, ...},   // DO
    {0x6401, 0, ...},   // AI
};

现在主站发过来一个请求：

Index    = 0x6200
Sub-index = 0

程序就需要在这个数组里面找：

0x6200 : 00

对应的对象。

这就是：

co_od_find(...)干的事情。
```



#### **① 先明确四个形参**

假设我们已经建立了一张对象表，变量名叫 `dictionary`，调用：

```c
const co_od_entry_t *found = NULL;

co_status_t status =
    co_od_find(&dictionary, 0x6200, 0x01, &found);
```

对应关系：

| 函数形参   | 接收到什么                  |
| ---------- | --------------------------- |
| `table`    | `dictionary` 表变量的地址   |
| `index`    | `0x6200`                    |
| `subindex` | `0x01`                      |
| `entry`    | 指针变量 `found` 本身的地址 |

这里沿用你之前已经理解的关系：

```
entry 指向 found
*entry 就是外面的 found 变量
```



#### **② 定义数组下标**

```c
size_t i;				//`size_t` 是无符号整数类型，适合表示数组数量和下标。
```

`i` 用于依次访问对象条目：

```c
typedef struct {
    const co_od_entry_t *entries;
    size_t count;
} co_od_table_t ;					//对象表结构体声明
co_od_table_t   table				//创建表变量table
```

对象表 table ： 里面有两个成员： entries 和 count

① table->entries ：table 是指向对象表结构体的指针

② count ：数组中有效条目数

```c
//看一个具体例子：
先看熟悉的整数：
int number;       /* 一个整数变量 */
int numbers[3];  /* 一个数组，里面有三个整数 */
换成结构体类型，道理相同：
co_od_entry_t object;      /* 一个对象条目变量 */
co_od_entry_t objects[3];  /* 一个数组，里面有三个对象条目 */

objects 数组
    │
    ├── objects[0]：一个完整的 co_od_entry_t 结构体
    │       ├── index
    │       ├── subindex
    │       ├── type
    │       └── 其他成员……
    │
    ├── objects[1]：一个完整的 co_od_entry_t 结构体
    │       ├── index
    │       ├── subindex
    │       ├── type
    │       └── 其他成员……
    │
    └── objects[2]：一个完整的 co_od_entry_t 结构体
            ├── index
            ├── subindex
            ├── type
            └── 其他成员……





co_od_entry_t objects[3] = {0}; /* 创建三个对象条目 */

co_od_table_t dictionary = {	/*创建一个对象表*/
    .entries = objects, /* 保存 objects 数组首元素地址 */
    .count = 3		   /*对象表中有3个对象条目*/
};

const co_od_table_t *table = &dictionary;	//传入对象表的地址
此时对应关系是：
table->entries     /*就是访问对象条目数组*/
table->entries[0]  /* 就是访问对象条目数组中的第一个对象条目，也就是就是 objects[0] */
table->entries[1]  /* 就是访问对象条目数组中的第二个对象条目，也就是就是 objects[1] */
table->entries[2]  /* 就是访问对象条目数组中的第三个对象条目，也就是就是 objects[2] */

如果继续访问第一个条目的索引：
table->entries[0].index
table->entries[0].subindex     
table->entries[0].access
table->entries[0].length
table->entries[0].min_value
table->entries[0].max_value
table->entries[0].write_preop_only
table->entries[0].read
table->entries[0].write
```







#### **③ 检查两个必要地址**

```c
if (table == NULL || entry == NULL) {
    return CO_ERR_ARGUMENT;
}
```

`||` 表示“或者”。只要下面任意一个条件成立，就返回参数错误：

- `table == NULL`：没有提供对象表地址；
- `entry == NULL`：没有提供输出地址，用于接收查找结果的指针变量地址。

注意区分：

```
found = NULL;  /* 合法，表示当前还没找到条目 */
```

调用时传入：

```
&found        /* 有效地址，函数可以修改 found */
```

所以 **`found` 初始为 `NULL` 不影响查找；第四个参数本身不能是 `NULL`。**



#### **④ 清空上一次的查找结果**

```
*entry = NULL;
```

由于传入的是 `&found`，这句的效果就是：

```
found = NULL;
```

先清空，是为了避免本次查找失败后，`found` 还保留着上一次找到的条目地址。



#### **⑤ 检查对象表是否配置合理**

```c
if (table->entries == NULL && table->count != 0u) {
    return CO_ERR_ARGUMENT;
}
```

回忆对象表结构：

```c
typedef struct {
    const co_od_entry_t *entries; /* 数组首地址 */
    size_t count;                /* 条目数量 */
} co_od_table_t;
```

这里的 `&&` 表示“并且”。

如果表声称有条目：

```
table->count != 0
```

但却没提供数组地址：

```
table->entries == NULL
```

就无法访问条目，因此返回参数错误。

但下面这种**空表**是允许的：

```
entries = NULL
count   = 0
```

它只表示没有任何对象，后面会返回“未找到”。



#### **⑥ 逐个检查对象条目**

```
for (i = 0u; i < table->count; ++i) {
```

假设 `count = 3`，循环依次检查：

```
i = 0 → 第 1 条
i = 1 → 第 2 条
i = 2 → 第 3 条
```

`i` 增加到 `3` 时，`i < count` 不成立，循环结束。



#### **⑦ 索引和子索引必须同时匹配**

```c
if (table->entries[i].index == index &&
    table->entries[i].subindex == subindex)
```

拆开读：

```
table->entries[i]
```

取表中的第 `i` 个条目。

```
table->entries[i].index
```

取这个条目的索引。

```
table->entries[i].subindex
```

取这个条目的子索引。

假设要找 `0x6200:01`：

| 当前检查的条目 | 是否匹配       |
| -------------- | -------------- |
| `0x6000:01`    | 否，索引不同   |
| `0x6200:00`    | 否，子索引不同 |
| `0x6200:01`    | 是，两项都相同 |



#### **⑧ 找到后，把条目地址写回**

```
*entry = &table->entries[i];
return CO_OK;
```

右边：

```
&table->entries[i]
```

取得匹配条目的地址。

左边：

```
*entry
```

对应外面的 `found` 变量。

因此，效果就是让：

```
found → 指向找到的对象条目
```

随后 `return CO_OK;` 立即结束函数，不再扫描剩下的条目。

这里只保存地址，不复制整个条目，也不读取该对象的实际数值。



#### **⑨ 所有条目都检查完，仍未找到**

```c
return CO_ERR_OD_NOT_FOUND;
```

此时：

```
status = CO_ERR_OD_NOT_FOUND
found  = NULL
```

因此调用之后，要先判断状态，再使用 `found`：

```
if (status == CO_OK) {
    /* 此时可以通过 found->index 等访问找到的条目 */
}
```

这个函数交给调用者的两份信息是：返回码说明查找是否成功，`found` 保存找到的条目地址。





## 4：把对象当前值放进输出数组

```c
co_status_t co_od_read(const co_od_entry_t *entry, uint8_t *data,
                       uint8_t data_length)
{
    uint8_t expected_length; /* 根据类型得到的标准字节数 */

    if (entry == NULL || data == NULL)
    {                           /* 访问成员或缓冲区之前先检查空指针 */
        return CO_ERR_ARGUMENT; /* 参数无效，立即结束 */
    }
    expected_length = co_od_type_length(entry->type); /* 类型标记转换为标准长度 */
    if (expected_length == 0u || entry->length != expected_length ||
        data_length != entry->length)
    {                            /* 类型有效且三个长度必须一致；调用者须提供足够空间 */
        return CO_ERR_OD_LENGTH; /* 长度或类型描述错误，不执行回调 */
    }
    if (entry->read == NULL)
    {                              /* 检查是否绑定读取函数 */
        return CO_ERR_OD_CALLBACK; /* 缺少所需回调 */
    }
    return entry->read(entry->user, data, data_length); /* 传入辅助地址和输出缓冲区，原样返回回调状态 */
}
```

`co_od_read()` 的作用是：**先检查读取参数，再调用条目中保存的读回调，把对象当前值放进输出数组。**



#### **① 三个参数**

假设前面已经查找成功，`found` 指向一个 `UNSIGNED16` 对象条目：

```c
uint8_t buffer[2] = {0};

co_status_t status = co_od_read(found, buffer, 2);
```

对应关系：

| 形参          | 本次接收到的内容        | 用途                       |
| ------------- | ----------------------- | -------------------------- |
| `entry`       | `found` 保存的条目地址  | 取得对象类型、长度和读回调 |
| `data`        | `buffer` 数组首元素地址 | 用来存放读取结果           |
| `data_length` | `2`                     | 声明本次接收 2 字节        |

这里直接传 `found`，因为我们需要它保存的**条目地址**，不用修改 `found` 本身。



#### **② 定义标准长度变量**

```c
uint8_t expected_length;
```

它用来保存：根据对象类型判断，应该有多少字节。

例如：

```
CO_OD_UNSIGNED16 → 应该是 2 字节
```



#### **③ 检查空指针**

```c
if (entry == NULL || data == NULL) {
    return CO_ERR_ARGUMENT;
}
```

任意一个为空，就返回参数错误：

```
entry 为空 → 无法访问对象条目
data 为空  → 没地方存放读取结果
```

检查通过后，才能访问 `entry->type` 等成员。



#### **④ 根据类型取得标准长度**

```c
expected_length = co_od_type_length(entry->type);
```

这就用到了今天学的第一个函数。

假设：

```
entry->type == CO_OD_UNSIGNED16
```

那么调用相当于：

```
expected_length = co_od_type_length(CO_OD_UNSIGNED16);
```

结果：

```
expected_length == 2
```



#### **⑤ 检查类型和长度是否一致**

```
if (expected_length == 0u ||
    entry->length != expected_length ||
    data_length != entry->length) {
    return CO_ERR_OD_LENGTH;
}
```

这里有三个条件，用 `||` 连接：**任意一项不符合，就返回错误。**

| 条件                               | 检查什么                         |
| ---------------------------------- | -------------------------------- |
| `expected_length == 0u`            | 对象类型不受支持                 |
| `entry->length != expected_length` | 条目填写的长度和类型不匹配       |
| `data_length != entry->length`     | 调用时传入的长度和对象长度不匹配 |

对于正常的 `UNSIGNED16` 对象：

```
expected_length = 2  ← 根据类型计算
entry->length   = 2  ← 条目中填写
data_length     = 2  ← 本次调用传入
```

三者一致，才继续执行。

注意，C 函数不能仅凭 `data` 指针知道数组真实大小。**调用者必须实际提供足够的空间**，不能数组只有 1 字节，却把 `data_length` 写成 `2`。



#### **⑥ 检查是否绑定读回调**

```
if (entry->read == NULL) {
    return CO_ERR_OD_CALLBACK;
}
```

之前学过：

```
co_od_read_fn read;
```

这个成员保存读取函数的地址。

如果它是 `NULL`，说明没有绑定读取函数，所以返回：

```
CO_ERR_OD_CALLBACK
```



#### **⑦ 调用读回调**

```
return entry->read(entry->user, data, data_length);
```

拆开看：

```
entry->read → 要调用的读取函数
entry->user → 该函数使用的辅助数据地址
data        → 输出数组地址
data_length → 要读取的字节数
```

假设条目绑定的是测试中的：

```
entry->read = fake_read;
entry->user = &value;
```

那么本次调用的效果就是：

```
fake_read(&value, buffer, 2);
```

`fake_read()` 从模拟变量中取得数值，再按小端顺序填入 `buffer`。

例如数值为 `0x0726`，读取成功后：

```
buffer[0] == 0x26;
buffer[1] == 0x07;
```

最后，`co_od_read()` 把回调返回的状态原样返回给调用者：

```
status → 读取是否成功
buffer → 成功时读取到的数据
```

这里没有单独检查 `ro/rw`，因为当前支持的这两种权限**都允许读取**。



#### ⑧用途

**读取 STM32 的 ADC 采集值，就是它的一种用途。**

以读取 AI1 为例，将来的流程是：

```c
主机：CAN 上位机
    ↓
发出 SDO 读取请求：读取 0x6401:01（AI1）
    ↓
从机：STM32
    ↓
SDO 模块找到 AI1 的对象条目
    ↓
调用 co_od_read()
    ↓
读回调读取应用层保存的 ADC 采集值
    ↓
把数值按小端顺序放入 data
    ↓
SDO 模块组装响应报文，发送给上位机
```

**`co_od_read()` 只负责检查并调用读回调，不负责接收请求或发送 CAN 报文。**

另外，读回调通常读取的是**已经采集并保存的 ADC 值**，不一定每次请求都启动一次 ADC 采样。

它也可以读取 DI 状态、心跳周期等其他对象，具体读什么，由传入的 `entry` 决定。



#### ⑨调用PC实例

```c
我们要学习的函数是：
co_status_t co_od_read(
    const co_od_entry_t *entry,
    uint8_t *data,
    uint8_t data_length
);
它需要三个实参：
entry       → 要读取的对象条目地址
data        → 用来接收读取结果的字节数组地址
data_length → data 缓冲区允许接收的字节数
```

##### 1、先准备模拟对象数据

PC 测试中，我们使用这个结构体保存一个模拟对象的当前值：

```c
typedef struct {
    uint32_t value;  /* 模拟对象当前数值 */
    unsigned reads;  /* 记录读回调被调用的次数 */
    unsigned writes; /* 记录写回调被调用的次数 */
} fake_value_t;
```

创建一个模拟对象结构体变量 value：

```c
fake_value_t value = {
    0x1234u,  /* 当前值 */
    0u,       /* 初始读取次数 */
    0u        /* 初始写入次数 */
};
```

此时：

```
value
 ├── value  = 0x1234
 ├── reads  = 0
 └── writes = 0
```



##### 2、准备读取回调函数

`co_od_read()` 本身不直接知道对象数据存在哪里，所以对象条目中要绑定一个读取函数。

```c
static co_status_t fake_read(
    void *user,
    uint8_t *data,
    uint8_t length
)
{
    fake_value_t *value = user;
    uint8_t i;

    ++value->reads;

    for (i = 0u; i < length; ++i) {
        data[i] =
            (uint8_t)(value->value >> (8u * i));
    }

    return CO_OK;
}
```

这个函数符合读取回调类型：

```c
typedef co_status_t (*co_od_read_fn)(
    void *user,
    uint8_t *data,
    uint8_t length
);
```

它做三件事：

```c
user       → 找到要读取的模拟对象
data       → 把读取结果写入这个数组
length     → 告诉函数要写几个字节
```



##### 3、创建对象条目

```c
const co_od_entry_t entry = {
    0x6401u,              /* index：对象索引 */
    0x01u,                /* subindex：对象子索引 */
    CO_OD_UNSIGNED16,     /* 类型：无符号16位 */
    CO_OD_READ_ONLY,      /* 主站只读 */
    2u,                   /* 长度：2字节 */
    0u,                   /* 最小值 */
    4095u,                /* 最大值 */
    0u,                   /* 不限制Pre-operational */
    fake_read,            /* 读取回调函数地址 */
    NULL,                 /* 只读对象不需要写回调 */
    &value                /* 回调使用的辅助数据地址 */
};
```

这个条目表示：

```c
0x6401:01
    ├── 类型：UNSIGNED16
    ├── 长度：2字节
    ├── 权限：只读
    ├── 读取函数：fake_read
    └── 辅助数据：&value
```

关键连接是：

```
entry.read ──► fake_read
entry.user ──► value
```

这里：

```
entry.read = fake_read;
entry.user = &value;
```





##### 4、准备输出数组

```
uint8_t data[2] = {0u, 0u};
```

它是用来接收对象值的缓冲区：

```c
data
 ├── data[0]
 └── data[1]
```

开始读取前：

```c
data[0] = 0x00
data[1] = 0x00
```

调用：

```c
co_status_t status =
    co_od_read(&entry, data, 2u);
```

三个实参分别是：

```c
&entry → 要读取的对象条目地址
data   → 输出数组首元素地址
2u     → 输出数组长度
```





##### 5、`co_od_read()` 内部执行过程

函数收到：

```
entry       ──► entry
data        ──► data[0]
data_length = 2
```

###### 第一步：检查指针

```
if (entry == NULL || data == NULL) {
    return CO_ERR_ARGUMENT;
}
```

检查：

```
entry 是否为空
data 是否为空
```

当前两者都有效，所以继续。

------

###### 第二步：根据类型确定标准长度

```
expected_length =
    co_od_type_length(entry->type);
```

当前：

```
entry->type = CO_OD_UNSIGNED16
```

所以：

```
co_od_type_length(CO_OD_UNSIGNED16)
    ↓
expected_length = 2
```

随后检查：

```
if (expected_length == 0u ||
    entry->length != expected_length ||
    data_length != entry->length)
```

当前各项是：

```
expected_length = 2
entry->length   = 2
data_length     = 2
```

长度一致，继续。

------

###### 第三步：检查读取回调

```
if (entry->read == NULL) {
    return CO_ERR_OD_CALLBACK;
}
```

当前：

```
entry->read ──► fake_read
```

不是空指针，所以继续。

------

###### 第四步：调用条目保存的读取函数

```c
return entry->read(
    entry->user,
    data,
    data_length
);
```

代入当前条目的实际内容：

```c
return fake_read(
    &value,
    data,
    2u
);
```

所以这句：

```c
entry->read(entry->user, data, data_length)
```

实际等价于：

```
fake_read(&value, data, 2u);
```



##### 6、进入 `fake_read()` 之后

```c
static co_status_t fake_read(
    void *user,
    uint8_t *data,
    uint8_t length
)
{
    fake_value_t *value = user;
    uint8_t i;

    ++value->reads;

    for (i = 0u; i < length; ++i) {
        data[i] =
            (uint8_t)(value->value >> (8u * i));
    }

    return CO_OK;
}
```

进入时的地址关系：

```c
&value
   │
   ▼
user
   │
   ▼
value 指针
   │
   ▼
原来的 fake_value_t value 结构体
```

因此：

```
fake_value_t *value = user;
```

不是创建新的 `fake_value_t`，而是把通用指针恢复成正确类型，用来访问原来的对象。



##### 7、读取次数加一

```
++value->reads;
```

调用前：

```
value.reads = 0
```

执行后：

```
value.reads = 1
```

这只是测试用的记录，真实对象未必需要记录读取次数。

------



##### 8、按照小端顺序拆分数值

当前对象值是：

```
value.value = 0x1234;
```

它拆成两个字节：

```
低字节 = 0x34
高字节 = 0x12
```

循环过程：

```c
i = 0
data[0] = (uint8_t)(0x1234 >> 0)
        = 0x34
i = 1
data[1] = (uint8_t)(0x1234 >> 8)
        = 0x12
```

执行结束后：

```
data[0] = 0x34
data[1] = 0x12
```

这就是 CANopen 常用的小端顺序：

```
低字节先放
高字节后放
```

------



##### 9、回调返回，`co_od_read()` 原样返回

`fake_read()` 最后：

```
return CO_OK;
```

回到 `co_od_read()`：

```c
return entry->read(
    entry->user,
    data,
    data_length
);
```

所以：

```
fake_read() 返回 CO_OK
          ↓
co_od_read() 原样返回 CO_OK
          ↓
status = CO_OK
```

注意：

```
data 数组的内容
value.reads 的变化
```

是通过指针直接修改原变量产生的。

而：

```
CO_OK
```

是通过 `return` 返回给调用者的状态码。



##### 10.总览

```c
你已经把 PC 测试和未来实际用途对应起来了。
现在的测试过程：
自己创建模拟变量，保存 value = 0x1234
                 ↓
对象条目通过 user 指向这个变量
                 ↓
co_od_read() 调用 fake_read()
                 ↓
把数值拆成字节，写入 data[]
以后上位机读取 STM32 的 ADC 采样值：
STM32 ADC 采样
      ↓
应用层更新对象表背后的 AI 数值
      ↓
上位机发送 SDO 请求：“读取 AI1”
      ↓
STM32 的 SDO 服务找到 AI1 对象条目
      ↓
co_od_read() 调用该条目的读回调
      ↓
把当前 AI1 数值写入 data[]
      ↓
SDO 服务将 data[] 装入响应报文
      ↓
通过 CAN 发给上位机
co_od_read() 负责读取已经保存的对象值；ADC 采集和 CAN 响应发送，由其他模块完成。 当前测试用模拟变量代替真实采样值，验证这段“读取并填入数组”的过程。
```







## 5：把输入数组中的数据写入对象值中

```c
co_status_t co_od_write(const co_od_entry_t *entry, co_nmt_state_t state,
                        const uint8_t *data, uint8_t data_length)
{
    uint8_t expected_length; /* 根据类型得到的标准字节数 */
    uint32_t value;          /* 待写入数据解码后的整数 */

    if (entry == NULL || data == NULL)
    {                           /* 访问成员或缓冲区之前先检查空指针 */
        return CO_ERR_ARGUMENT; /* 参数无效，立即结束 */
    }
    expected_length = co_od_type_length(entry->type); /* 类型标记转换为标准长度 */
    if (expected_length == 0u || entry->length != expected_length ||
        data_length != entry->length)
    {                            /* 类型有效且三个长度必须一致；调用者须提供足够空间 */
        return CO_ERR_OD_LENGTH; /* 长度或类型描述错误，不执行回调 */
    }
    if (entry->access != CO_OD_READ_WRITE)
    {                               /* 只有可读写对象允许写入 */
        return CO_ERR_OD_READ_ONLY; /* 拒绝写入，应用值不由本次检查修改 */
    }
    if (entry->write_preop_only && state != CO_NMT_PRE_OPERATIONAL)
    {                           /* 仅在限制标志非零时要求预运行状态 */
        return CO_ERR_OD_STATE; /* 当前状态不满足该对象写入限制 */
    }
    if (entry->write == NULL)
    {                              /* 检查是否绑定写入函数 */
        return CO_ERR_OD_CALLBACK; /* 缺少所需回调 */
    }
    value = co_od_decode_le(data, data_length); /* 长度已验证，安全解码最多 4 个字节 */
    if (value < entry->min_value || value > entry->max_value)
    {                           /* 最小值和最大值本身都允许写入 */
        return CO_ERR_OD_VALUE; /* 数值越界，不调用写回调 */
    }
    return entry->write(entry->user, data, data_length); /* 检查通过后调用一次写回调，失败也不在此重试 */
}
```

`co_od_write()` 的作用是：**检查待写入的数据，全部检查通过后，调用对象的写回调，更新应用层数据**

例如，将来上位机要求：

```
向 0x6200:01 写入 0x05
```

SDO 模块解析并找到条目后，就可以调用这个函数。



#### **① 四个形参**

| 参数          | 含义                 |
| ------------- | -------------------- |
| `entry`       | 已找到的对象条目地址 |
| `state`       | 节点当前的 NMT 状态  |
| `data`        | 待写入的字节数组地址 |
| `data_length` | 待写入的字节数       |

假设 `found` 已经指向数字输出条目，节点当前处于 Operational：

```
uint8_t command[1] = {0x05};

co_status_t status =
    co_od_write(found, CO_NMT_OPERATIONAL, command, 1);
```

这里：

```
entry       → 数字输出条目
state       → CO_NMT_OPERATIONAL
data        → command 数组首元素地址
data_length → 1
```

`const uint8_t *data` 表示函数通过这个指针**只读取输入数组，不修改数组内容**。它仍然可以通过写回调修改对象对应的应用变量。



#### **② 定义两个内部变量**

```c
uint8_t expected_length;
uint32_t value;
```

分别保存：

```c
expected_length → 对象类型规定的字节数
value           → 输入字节组合成的整数，用于检查数值范围
```



#### **③ 检查空指针**

```c
if (entry == NULL || data == NULL) {
    return CO_ERR_ARGUMENT;
}
```

没有条目地址或输入数据地址，就返回参数错误。



#### **④ 检查类型与长度**

```c
expected_length = co_od_type_length(entry->type);
```

先根据类型取得标准长度。

然后：

```c
if (expected_length == 0u ||
    entry->length != expected_length ||
    data_length != entry->length) {
    return CO_ERR_OD_LENGTH;
}
```

与读取函数一样，要求：

```
类型有效
类型规定的长度 == 条目中的长度 == 本次输入长度
```

例如数字输出为 `UNSIGNED8`，三个长度都必须是 `1`。



#### **⑤ 检查是否允许写入**

```c
if (entry->access != CO_OD_READ_WRITE) {
    return CO_ERR_OD_READ_ONLY;
}
```

`!=` 表示“不等于”。

如果对象不是可读写，就拒绝写入。例如：

```
数字输入 ro → 拒绝主站写入
数字输出 rw → 继续后面的检查
```



#### **⑥ 检查预运行状态限制**

```
if (entry->write_preop_only &&
    state != CO_NMT_PRE_OPERATIONAL) {
    return CO_ERR_OD_STATE;
}
```

两个条件同时成立，才拒绝：

```c
这个条目要求只能在预运行状态写入
并且
当前节点不是预运行状态
```

对应关系：

| `write_preop_only` | 当前状态        | 这一项检查             |
| ------------------ | --------------- | ---------------------- |
| `0`                | 任意状态        | 不施加预运行限制       |
| 非零               | Pre-operational | 通过                   |
| 非零               | 其他状态        | 返回 `CO_ERR_OD_STATE` |

注意，标志为 `0` 只表示**这个检查不限制状态**。将来 SDO 服务是否允许在某个状态处理请求，还由 SDO/NMT 模块决定。



#### **⑦ 检查写回调是否存在**

```c
if (entry->write == NULL) {
    return CO_ERR_OD_CALLBACK;
}
```

即使条目标记为 `rw`，也必须绑定实际写入函数，否则没有办法提交新值。



#### **⑧ 把输入字节组合成整数**

```c
value = co_od_decode_le(data, data_length);
```

这里调用刚学过的小端解码函数。

例如：

```
data = {0x05}
value = 5
```

或者：

```
data = {0xE8, 0x03}
value = 0x03E8，也就是 1000
```

**为什么要组合成整数？因为下一步要比较大小。**



#### **⑨ 检查数值范围**

```
if (value < entry->min_value || value > entry->max_value) {
    return CO_ERR_OD_VALUE;
}
```

小于最小值，或者大于最大值，都拒绝写入。最小值和最大值本身是允许的。

例如数字输出范围为 `0～15`：

```
写入 0x05，即 5  → 通过
写入 0x0F，即 15 → 通过
写入 0x10，即 16 → 拒绝
```

到这里为止，**还没有调用写回调，也没有由这个函数提交新值。**



#### **⑩ 检查通过，调用写回调**

```
return entry->write(entry->user, data, data_length);
```

假设条目中保存的是：

```
write → fake_write
user  → 模拟变量 output 的地址
```

那么这次调用相当于：

```
fake_write(&output, command, 1);
```

回调读取 `command` 中的 `0x05`，更新模拟变量。以后接入硬件时，可以由应用层根据这个命令设置输出电平。

注意，传给写回调的仍然是**原来的字节数组**：

```
data
```

前面的：

```
value
```

只是用于范围检查的临时整数，没有作为参数传给回调。

最后，回调返回什么状态，`co_od_write()` 就原样返回什么状态；如果回调失败，这个函数不会自动重试或回滚回调已经做过的操作。





## 总览：

```c
和读取对比：
co_od_read()
对象当前值 ──读取、拆分──► data[] 数组

co_od_write()
data[] 数组 ──检查、组合──► 对象当前值
当前 PC 测试中：
我们准备 data[]，装入要写的数值
                ↓
调用 co_od_write()
                ↓
检查权限、状态、长度、数值范围等
                ↓
调用 fake_write()
                ↓
更新模拟变量的 value，writes 加 1
                ↓
返回状态码
以后控制 STM32 的 LED：
上位机发送 SDO 写请求
“向 DO 对象 0x6200:01 写入 1”
                ↓
STM32 的 SDO 服务解析请求，取得数据字节
                ↓
co_od_write() 检查后调用写回调
                ↓
DO 对象的当前值更新为 1
                ↓
应用层读取 DO 命令，设置 GPIO
                ↓
对应 LED 点亮
所以，写入的数据数组是 co_od_write() 的输入；被更新的是对象背后的变量。 GPIO 的实际操作放在后续应用层完成。
```



## PC实例：

这次我们用 **“向模拟 DO 对象写入 `5`”** 作为完整例子。低四位 `0101` 表示第 1、3 路输出命令为 1，但 PC 测试只更新内存，不控制真实 LED。



### **① 先准备模拟对象的结构体**

```c
typedef struct {
    uint32_t value;   /* 模拟对象当前数值 */
    unsigned reads;  /* 读取回调的调用次数 */
    unsigned writes; /* 写入回调的调用次数 */
} fake_value_t;
```

随后创建变量：

```c
fake_value_t output = {
    0u,  /* 初始输出值 */
    0u,  /* 初始读取次数 */
    0u   /* 初始写入次数 */
};
```

此时：

```c
output
 ├── value  = 0
 ├── reads  = 0
 └── writes = 0
```

这里故意把变量叫 `output`，方便与回调里的指针变量区分。





### **② 写好完整的模拟写入回调**

```c
static co_status_t fake_write(
    void *user,
    const uint8_t *data,
    uint8_t length
)
{
    fake_value_t *value = user;
    /* user 保存模拟对象的地址；
       value 是指向这个对象的指针，没有创建新的结构体 */

    uint8_t i;

    value->value = 0u;
    /* 清除旧数值，为下面按字节组合新数值做准备 */

    for (i = 0u; i < length; ++i) {
        value->value |= ((uint32_t)data[i]) << (8u * i);
        /* 按小端顺序，把输入数组中的字节组合成整数 */
    }

    ++value->writes;
    /* 记录一次写入 */

    return CO_OK;
    /* 返回写入成功的状态码 */
}
```

三个形参的用途：

```c
user   → 要修改的模拟对象地址
data   → 保存待写入数据的数组首元素地址
length → 本次数据有多少字节
```

其中：

```c
const uint8_t *data
```

表示回调**通过这个指针读取输入数组中的字节，不修改输入数组**。

------



### **③ 创建对象条目，把回调和模拟变量连接起来**

```c
const co_od_entry_t entry = {
    .index = 0x6200u,                /* DO 对象索引 */
    .subindex = 0x01u,               /* 子索引 */
    .type = CO_OD_UNSIGNED8,         /* 无符号8位 */
    .access = CO_OD_READ_WRITE,      /* 允许读写 */
    .length = 1u,                    /* 占1字节 */
    .min_value = 0u,                 /* 最小允许值 */
    .max_value = 15u,                /* 最大允许值：低四位全为1 */
    .write_preop_only = 0u,          /* 不要求必须在预运行状态写入 */
    .read = NULL,                   /* 此独立示例只演示写入 */
    .write = fake_write,             /* 保存写回调函数地址 */
    .user = &output                 /* 保存模拟对象地址 */
};
```

这里 `.read = NULL` 不影响本次写入；如果要通过 `co_od_read()` 读取它，还需要绑定读回调。

关键连接：

```c
entry
 ├── index/subindex = 0x6200:01
 ├── type = CO_OD_UNSIGNED8
 ├── length = 1
 ├── access = CO_OD_READ_WRITE
 ├── 允许范围 = 0～15
 │
 ├── write ─────────► fake_write 函数
 │
 └── user ──────────► output 结构体变量
                         ├── value  = 0
                         ├── reads  = 0
                         └── writes = 0
```

**到这里，只是建立条目与函数、数据的连接，还没有执行写入。**

------



### **④ 准备要写入的数据数组**

```c
uint8_t data[1] = {0x05u};
```

这里的数组已经装有待写入的数据：

```c
data[0] = 0x05
```

调用：

```c
co_status_t status = co_od_write(
    &entry,
    CO_NMT_OPERATIONAL,
    data,
    1u
);
```

四个实参分别是：

```c
&entry             → 要写入哪个对象条目
CO_NMT_OPERATIONAL → 告知函数当前节点状态
data               → 待写入数据的数组首元素地址
1u                 → 本次数据长度为1字节
```

注意：传入 `CO_NMT_OPERATIONAL` 是**提供当前状态供检查**，不会把节点切换到这个状态。

------



### **⑤ `co_od_write()` 的完整函数体**

下面保留当前实现的检查逻辑，并补充学习注释：

```c
co_status_t co_od_write(
    const co_od_entry_t *entry,
    co_nmt_state_t state,
    const uint8_t *data,
    uint8_t data_length
)
{
    uint8_t expected_length;
    int64_t value;

    /* 1. 检查条目地址和输入数据地址 */
    if (entry == NULL || data == NULL) {
        return CO_ERR_ARGUMENT;
    }

    /* 2. 根据对象类型取得标准字节数 */
    expected_length = co_od_type_length(entry->type);

    /* 类型必须有效；类型长度、条目长度、输入长度必须一致 */
    if (expected_length == 0u ||
        entry->length != expected_length ||
        data_length != entry->length) {
        return CO_ERR_OD_LENGTH;
    }

    /* 3. 检查对象是否允许写入 */
    if (entry->access != CO_OD_READ_WRITE) {
        return CO_ERR_OD_READ_ONLY;
    }

    /* 4. 如果条目要求仅预运行可写，就检查当前状态 */
    if (entry->write_preop_only &&
        state != CO_NMT_PRE_OPERATIONAL) {
        return CO_ERR_OD_STATE;
    }

    /* 5. 检查是否绑定了写回调 */
    if (entry->write == NULL) {
        return CO_ERR_OD_CALLBACK;
    }

    /* 6. 将输入的小端字节组合成整数，供范围检查 */
    value = co_od_decode_le(data, data_length);

    /* INTEGER16 类型需要把高位为1的值解释成负数 */
    if (entry->type == CO_OD_INTEGER16 && value >= 0x8000) {
        value -= 0x10000;
    }

    /* 7. 检查新值是否在对象允许范围内 */
    if (value < entry->min_value ||
        value > entry->max_value) {
        return CO_ERR_OD_VALUE;
    }

    /* 8. 检查全部通过，调用写回调，原样返回它的状态码 */
    return entry->write(entry->user, data, data_length);
}
```

它使用的两个内部辅助函数：

```c
static uint8_t co_od_type_length(co_od_type_t type)
{
    switch (type) {
    case CO_OD_UNSIGNED8:
        return 1u;

    case CO_OD_UNSIGNED16:
    case CO_OD_INTEGER16:
        return 2u;

    case CO_OD_UNSIGNED32:
        return 4u;

    default:
        return 0u;
    }
}
static uint32_t co_od_decode_le(
    const uint8_t *data,
    uint8_t length
)
{
    uint32_t value = 0u;
    uint8_t i;

    for (i = 0u; i < length; ++i) {
        value |= ((uint32_t)data[i]) << (8u * i);
    }

    return value;
}
```

**这里 `co_od_write()` 中的局部变量 `value`，只是用于检查新数值，不是模拟对象里的 `output.value`。**

------



### **⑥ 代入本次调用，看检查过程**

本次调用：

```c
co_od_write(&entry, CO_NMT_OPERATIONAL, data, 1u);
```

检查过程：

```c
entry 和 data 是否有效？
    ↓ 有效
对象类型要求多少字节？
    ↓ CO_OD_UNSIGNED8 → 1字节
条目长度、输入长度是否都是1？
    ↓ 是
对象是否可写？
    ↓ CO_OD_READ_WRITE → 可写
是否要求只在 Pre-operational 写入？
    ↓ write_preop_only = 0 → 本项不限制
是否有写回调？
    ↓ 有，fake_write
输入数据表示多少？
    ↓ data[0] = 0x05 → 数值5
5 是否在0～15之间？
    ↓ 是
调用写回调
```

最终执行：

```c
return entry->write(entry->user, data, data_length);
```

代入条目保存的信息，就相当于：

```c
return fake_write(&output, data, 1u);
entry->write  → fake_write
entry->user   → &output
data          → 输入数组首元素地址
data_length   → 1
```

------



### **⑦ 进入 `fake_write()` 后，实际修改谁？**

进入回调：

```
fake_write(&output, data, 1u);
```

参数关系：

```
user ─────────► output
data ─────────► 输入数组 data[0]
length = 1
```

第一句：

```
fake_value_t *value = user;
```

建立这个指向关系：

```
回调里的指针 value ──► 调用者的结构体 output
```

因此：

```
value->value
```

访问的就是：

```
output.value
```

而：

```
value->writes
```

访问的就是：

```
output.writes
```

本次回调执行过程：

```
value->value = 0u;
```

相当于：

```
output.value = 0u;
```

循环只有一次：

```
/* i = 0 */
value->value |= ((uint32_t)data[0]) << 0;
```

也就是：

```
output.value = 0 | 5
             = 5
```

随后：

```
++value->writes;
```

相当于：

```
++output.writes;
```

结果：

```
output
 ├── value  = 5    已更新
 ├── reads  = 0    不变
 └── writes = 1    加一
```

这里两次“把字节组合成整数”的用途不同：

```
co_od_write() 中组合 → 为了检查是否越界

fake_write() 中组合 → 为了将新值存入模拟对象
```





### **⑧ 区分“修改数据”和“返回状态”**

回调最后：

```
return CO_OK;
```

返回关系：

```
fake_write() 返回 CO_OK
              ↓
co_od_write() 原样返回 CO_OK
              ↓
调用者的 status = CO_OK
```

调用前后对比：

```
调用前：
    output.value  = 0
    output.writes = 0
    data[0]       = 5

调用后：
    output.value  = 5     通过指针更新
    output.writes = 1     通过指针更新
    data[0]       = 5     输入数组保持不变
    status        = CO_OK
```

**对象数据通过指针修改；状态码通过 `return` 返回。**

如果这次传入的是 `16`：

```
data[0] = 16
      ↓
co_od_write() 发现超过最大值15
      ↓
直接返回 CO_ERR_OD_VALUE
      ↓
没有调用 fake_write()
      ↓
output.value 和 output.writes 都保持原值
```

------



### **⑨ 完整的 PC 测试调用示例**

下面的代码依赖项目已有的 `co_od.h/.c`，展示从创建变量到检查结果的完整过程：

```c
#include "co_od.h"
#include <stddef.h>

typedef struct {
    uint32_t value;
    unsigned reads;
    unsigned writes;
} fake_value_t;

static co_status_t fake_write(
    void *user,
    const uint8_t *data,
    uint8_t length
)
{
    fake_value_t *value = user;
    uint8_t i;

    value->value = 0u;

    for (i = 0u; i < length; ++i) {
        value->value |= ((uint32_t)data[i]) << (8u * i);
    }

    ++value->writes;

    return CO_OK;
}

static int test_write_example(void)
{
    /* 1. 创建模拟对象，初始数值和计数均为0 */
    fake_value_t output = {0u, 0u, 0u};

    /* 2. 创建条目，绑定写回调和模拟对象地址 */
    const co_od_entry_t entry = {
        .index = 0x6200u,
        .subindex = 0x01u,
        .type = CO_OD_UNSIGNED8,
        .access = CO_OD_READ_WRITE,
        .length = 1u,
        .min_value = 0u,
        .max_value = 15u,
        .write_preop_only = 0u,
        .read = NULL,
        .write = fake_write,
        .user = &output
    };

    /* 3. 准备要写入的数值5 */
    uint8_t data[1] = {5u};

    /* 4. 调用通用对象写接口 */
    co_status_t status = co_od_write(
        &entry,
        CO_NMT_OPERATIONAL,
        data,
        1u
    );

    /* 5. 检查返回状态 */
    if (status != CO_OK) {
        return 1;
    }

    /* 6. 检查对象值与写入次数 */
    if (output.value != 5u || output.writes != 1u) {
        return 1;
    }

    /* 7. 改用越界值16，再尝试写入 */
    data[0] = 16u;

    status = co_od_write(
        &entry,
        CO_NMT_OPERATIONAL,
        data,
        1u
    );

    if (status != CO_ERR_OD_VALUE) {
        return 1;
    }

    /* 8. 越界时没有调用回调，所以原值、次数都不变 */
    if (output.value != 5u || output.writes != 1u) {
        return 1;
    }

    return 0;
}

int main(void)
{
    return test_write_example();
}
```

整体流程：

```c
创建模拟对象 output
        │
创建对象条目 entry
        ├── write = fake_write
        └── user  = &output
        │
准备输入数组 data[] = {5}
        │
        ▼
co_od_write(&entry, CO_NMT_OPERATIONAL, data, 1)
        │
        ├── 检查指针、长度、权限、状态、回调和范围
        │
        └── 调用 fake_write(&output, data, 1)
                           │
                           ├── 从 data[] 读取字节
                           ├── 更新 output.value = 5
                           ├── output.writes 加1
                           └── 返回 CO_OK
                                     │
                                     ▼
                           co_od_write 返回 CO_OK
```

与刚学过的读取放在一起：

```c
读对象：
模拟对象.value ──► fake_read() ──► 输出数组 data[]

写对象：
输入数组 data[] ──► fake_write() ──► 模拟对象.value
```

现在的 PC 示例只验证对象写入。以后由 SDO 服务提供上位机发来的数据，再由 STM32 应用层读取更新后的 DO 命令，控制 LED