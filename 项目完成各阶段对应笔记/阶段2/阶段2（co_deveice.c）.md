## 1：匿名枚举

```c
enum {
    ERROR_SLOT = 1,
    HEARTBEAT_SLOT = 2,
    IDENTITY_SLOT = 4,
    DI_SLOT = 9,
    DO_SLOT = 11,
    AI1_SLOT = 13,
    AI2_SLOT = 14
};
//这是 匿名枚举，C 语言允许 enum 后面不写名字。

枚举常量          数组中的位置
ERROR_SLOT = 1  → fields[1]  错误寄存器
HEARTBEAT_SLOT  → fields[2]  心跳周期
IDENTITY_SLOT   → fields[4]  身份信息的起始位置
DI_SLOT         → fields[9]  数字输入
DO_SLOT         → fields[11] 数字输出
AI1_SLOT        → fields[13] 模拟输入1
AI2_SLOT        → fields[14] 模拟输入2
```

这里的目的只是给几个整数起名字，方便后面当数组下标使用：

```c
定义后，就可以直接使用这些枚举常量：
device->fields[DI_SLOT].value = DI;
因为 DI_SLOT 的值是 9，所以上面等效于：
device->fields[9].value = DI;
但写 DI_SLOT 更容易看出：这里操作的是 DI 对应的存储位置。
```



## 2：描述“一个对象的规则和默认值”结构体

```c
typedef struct
{
    uint16_t index;                  /* 对象索引，例如 0x6200 */
    uint8_t subindex;                /* 子索引，例如 1；与 index 共同定位对象 */
    co_od_type_t type;               /* 内部数据类型标记，不等于 EDS 的类型编号 */
    uint8_t length, writable, preop; /* 字节数、是否可写、是否仅预运行可写 */
    uint32_t initial, maximum;       /* 初值与允许上限；本设备对象下限统一为 0 */
} definition_t;

```

**每个成员分别描述什么？**

| 成员       | 含义                       | DO 对象示例       |
| ---------- | -------------------------- | ----------------- |
| `index`    | 对象索引                   | `0x6200`          |
| `subindex` | 对象子索引                 | `1`               |
| `type`     | 对象的数据类型             | `CO_OD_UNSIGNED8` |
| `length`   | 对象数据占多少字节         | `1`               |
| `writable` | 是否允许通过对象字典写入   | `1`：允许         |
| `preop`    | 是否要求仅在预运行状态写入 | `0`：没有这项限制 |
| `initial`  | 初始化时使用的数值         | `0`               |
| `maximum`  | 允许的最大数值             | `15`              |

其中：

```c
writable
    ├── 0 → 只读
    └── 1 → 可读写

preop
    ├── 0 → 不要求必须处于 Pre-operational
    └── 1 → 只有 Pre-operational 才允许写入
```

`preop = 0` 并不表示所有状态下一定能通过 CAN 写入；后续 SDO/PDO 服务还会检查自己的运行状态要求。



### 1   ： 示例

用它创建一个 DO 规则变量

```c
definition_t do_rule = {
    .index = 0x6200,
    .subindex = 1,
    .type = CO_OD_UNSIGNED8,
    .length = 1,
    .writable = 1,
    .preop = 0,
    .initial = 0,
    .maximum = 15
};
```

```c
do_rule：描述 DO 对象的规则
    ├── 地址：0x6200:01
    ├── 类型：无符号8位
    ├── 长度：1字节
    ├── 权限：允许写入
    ├── 状态限制：不要求仅预运行可写
    ├── 初始值：0
    └── 最大值：15
```

为什么最大值是 `15`？

```c
4 路 DO 使用低四位

0000 0000 → 0，四路命令均为0
0000 1111 → 15，四路命令均为1
```









### 2： 和对象条目结构体的区别

```c
typedef struct
{
    uint16_t index;           /* 对象索引，例如 0x6200 */
    uint8_t subindex;         /* 对象子索引，例如 0x01 */
    co_od_type_t type;        /* 对象数据类型，同时决定标准长度 */
    co_od_access_t access;    /* 主站的读写权限 */
    uint8_t length;           /* 对象在 SDO/PDO 中占用的字节数 */
    int64_t min_value;        /* 写入值允许的最小值 */
    int64_t max_value;        /* 写入值允许的最大值 */
    uint8_t write_preop_only; /* 非零表示只允许 Pre-operational 状态写入 */
    co_od_read_fn read;       /* 读取对象值的回调 */
    co_od_write_fn write;     /* 写入对象值的回调；只读对象可为 NULL */
    void *user;               /* 回调使用的应用层私有数据 */
} co_od_entry_t;              // 对象条目结构体
```

你可以暂时把它理解成：一个完整的“对象条目”。

比如我们要描述：0x6200:01 DO 输出对象，我们可以通过手动创建 co_od_entry_t   Do_entry;



```c
typedef struct
{
    uint16_t index;                  /* 对象索引，例如 0x6200 */
    uint8_t subindex;                /* 子索引，例如 1；与 index 共同定位对象 */
    co_od_type_t type;               /* 内部数据类型标记，不等于 EDS 的类型编号 */
    uint8_t length, writable, preop; /* 字节数、是否可写、是否仅预运行可写 */
    uint32_t initial, maximum;       /* 初值与允许上限；本设备对象下限统一为 0 */
} definition_t;
```

你可以把它理解成：“对象制作说明书”。

它表达的是：我先把每个对象“应该是什么样”规定下来，变为一个模板，而不用一个个手动创建。



不直接用 `co_od_entry_t` 当模板？

这是最关键的问题。

因为：

```
co_od_entry_t
```

里面有一些东西，是运行的时候才需要确定的。

比如：

```
co_od_read_fn read;
co_od_write_fn write;
void *user;
```

你现在还不知道：

```
这个对象最终要绑定哪个 RAM 变量？
```





### 3：两个结构体举一个实际例子

我们只拿 一个 DO 对象 `0x6200:01` 举例

**① 先用 `definition_t` 创建一份规则**

```c
definition_t do_rule = {
    .index = 0x6200,
    .subindex = 1,
    .type = CO_OD_UNSIGNED8,
    .length = 1,
    .writable = 1,
    .preop = 0,
    .initial = 0,
    .maximum = 15
};
```

这份规则的意思是：

```c
DO 对象
 ├── 地址：0x6200:01
 ├── 数据：1字节
 ├── 允许写入
 ├── 默认值：0
 └── 最大允许值：15
```

此时只是写好了规则，还没有绑定读写函数，也没有指定当前数据存在哪里。



**② 准备一份存储，保存当前 DO 数值**

项目中存储类型是：

```c
typedef struct {
    uint32_t value;  /* 当前数值 */
    uint32_t writes; /* 成功写入次数 */
} co_device_value_t;
```

创建变量：

```
co_device_value_t do_data = {0};
```

然后根据模板设置初值：

```
do_data.value = do_rule.initial;
```

此时：

```
do_rule.initial = 0  → 默认值
do_data.value   = 0  → 当前值
```

它们是两个不同的成员。以后修改value当前值，不会改变模板里do_rule 的默认值。

------



**③ 用 `co_od_entry_t` 创建实际条目**

假设已经写好了 `read_value()` 和 `write_value()`，现在把规则、回调和存储地址放进条目：

```c
co_od_entry_t do_entry = {
    .index = do_rule.index,
    .subindex = do_rule.subindex,
    .type = do_rule.type,
    .access = do_rule.writable
              ? CO_OD_READ_WRITE
              : CO_OD_READ_ONLY,
    .length = do_rule.length,
    .min_value = 0,
    .max_value = do_rule.maximum,
    .write_preop_only = do_rule.preop,

    .read = read_value,
    .write = do_rule.writable ? write_value : NULL,
    .user = &do_data
};
```

现在连接完整了：

```
do_rule：规则模板
    │
    │ 根据它填写
    ▼
do_entry：实际对象条目
    ├── index = 0x6200
    ├── subindex = 1
    ├── length = 1
    ├── access = 可读写
    ├── 允许范围 = 0～15
    │
    ├── read ─────► read_value()
    ├── write ────► write_value()
    │
    └── user ─────► do_data
                       ├── value = 0
                       └── writes = 0
```

------



**④ 尝试向 DO 对象写入 `5`**

```c
uint8_t data[1] = {5};

co_status_t status = co_od_write(
    &do_entry,
    CO_NMT_OPERATIONAL,
    data,
    1
);
```

执行过程：

```
co_od_write(&do_entry, ..., data, 1)
    │
    ├── 从 do_entry 中读取规则，进行检查
    │     可写？是
    │     长度1字节？是
    │     数值5在0～15之间？是
    │
    └── 调用 do_entry.write(do_entry.user, data, 1)
                       │
                       ▼
             write_value(&do_data, data, 1)
                       │
                       ▼
                do_data.value = 5
                do_data.writes = 1
```

写完以后：

```
do_rule.initial = 0    默认值没有变化

do_entry              规则和回调连接没有变化

do_data.value = 5      当前值更新了
do_data.writes = 1     写入次数增加了
```



**⑤ 对应回项目里的数组**

上面为了方便理解，分别创建了三个独立变量。项目里对象比较多，所以用数组统一存放：

```c
这个例子的独立变量       项目里 DO 对应的数组元素

do_rule          ↔      definitions[11]
do_entry         ↔      device->entries[11]
do_data          ↔      device->fields[11]
```

`co_device_od_init()` 就是通过循环，为每个对象执行类似的配置：

```
取出一份规则
    ↓
给当前数据设置初值
    ↓
填写实际条目
    ↓
绑定回调和当前数据地址
```

你之前已经会手动创建 `co_od_entry_t` 并绑定 `fake_write`、`&output`。现在只是多了一份模板，把许多对象的规则集中写好，再由初始化函数批量建立条目。





## 3：所有对象的“规则模板”结构体数组

```c
 = {
    {0x1000, 0, CO_OD_UNSIGNED32, 4, 0, 0, 0x00070191, UINT32_MAX}, /* 设备类型：401 加 DI/DO/AI 功能标志，原文复核待完成 */
    {0x1001, 0, CO_OD_UNSIGNED8, 1, 0, 0, 0, 255},                  /* 错误寄存器，初始无故障 */
    {0x1017, 0, CO_OD_UNSIGNED16, 2, 1, 0, 1000, 65535},            /* 心跳周期，默认 1000 ms，0 关闭 */
    {0x1018, 0, CO_OD_UNSIGNED8, 1, 0, 0, 4, 4},                    /* Identity 最大子索引为 4 */
    {0x1018, 1, CO_OD_UNSIGNED32, 4, 0, 0, 0, UINT32_MAX},          /* Vendor-ID=0 是未分配开发占位 */
    {0x1018, 2, CO_OD_UNSIGNED32, 4, 0, 0, 1, UINT32_MAX},          /* 开发产品编号为 1 */
    {0x1018, 3, CO_OD_UNSIGNED32, 4, 0, 0, 0x10000, UINT32_MAX},    /* 开发版本号 0x00010000 */
    {0x1018, 4, CO_OD_UNSIGNED32, 4, 0, 0, 0, UINT32_MAX},          /* 开发序列号 0，不保证唯一 */
    {0x6000, 0, CO_OD_UNSIGNED8, 1, 0, 0, 1, 1},                    /* DI 有 1 个输入字节 */
    {0x6000, 1, CO_OD_UNSIGNED8, 1, 0, 0, 0, 15},                   /* 低四位为四路 DI 的逻辑状态 */
    {0x6200, 0, CO_OD_UNSIGNED8, 1, 0, 0, 1, 1},                    /* DO 有 1 个输出字节 */
    {0x6200, 1, CO_OD_UNSIGNED8, 1, 1, 0, 0, 15},                   /* 低四位为四路 DO 命令，范围 0..15 */
    {0x6401, 0, CO_OD_UNSIGNED8, 1, 0, 0, 2, 2},                    /* AI 有两路输入 */
    {0x6401, 1, CO_OD_INTEGER16, 2, 0, 0, 0, 4095},                 /* AI1：INTEGER16 容纳 ADC 原始值 0..4095 */
    {0x6401, 2, CO_OD_INTEGER16, 2, 0, 0, 0, 4095},                 /* AI2：同样占两字节 */
    {0x1400, 0, CO_OD_UNSIGNED8, 1, 0, 0, 2, 2},                    /* RPDO1 最大子索引为 2 */
    {0x1400, 1, CO_OD_UNSIGNED32, 4, 0, 0, 0x200, UINT32_MAX},      /* RPDO1 的 COB-ID 基值，初始化加节点号 */
    {0x1400, 2, CO_OD_UNSIGNED8, 1, 0, 0, 255, 255},                /* RPDO1 传输类型固定为 255 */
    {0x1600, 0, CO_OD_UNSIGNED8, 1, 0, 0, 1, 1},                    /* RPDO1 固定映射一个对象 */
    {0x1600, 1, CO_OD_UNSIGNED32, 4, 0, 0, 0x62000108, UINT32_MAX}, /* 映射 DO：索引6200、子索引01、长度08位 */
    {0x1800, 0, CO_OD_UNSIGNED8, 1, 0, 0, 5, 5},                    /* TPDO1 最大子索引5，子索引4不实现 */
    {0x1800, 1, CO_OD_UNSIGNED32, 4, 0, 0, 0x40000180, UINT32_MAX}, /* TPDO1 基值；bit30=1 禁止 RTR，并非纯 CAN-ID */
    {0x1800, 2, CO_OD_UNSIGNED8, 1, 0, 0, 255, 255},                /* TPDO1 传输类型固定为 255 */
    {0x1800, 3, CO_OD_UNSIGNED16, 2, 1, 1, 0, 65535},               /* TPDO1 抑制时间，单位100us，仅预运行可写 */
    {0x1800, 5, CO_OD_UNSIGNED16, 2, 1, 1, 0, 65535},               /* TPDO1 事件周期，单位ms，默认0关闭周期触发 */
    {0x1801, 0, CO_OD_UNSIGNED8, 1, 0, 0, 5, 5},                    /* TPDO2 最大子索引5，子索引4不实现 */
    {0x1801, 1, CO_OD_UNSIGNED32, 4, 0, 0, 0x40000280, UINT32_MAX}, /* TPDO2 基值；bit30=1 禁止 RTR */
    {0x1801, 2, CO_OD_UNSIGNED8, 1, 0, 0, 255, 255},                /* TPDO2 传输类型固定为 255 */
    {0x1801, 3, CO_OD_UNSIGNED16, 2, 1, 1, 0, 65535},               /* TPDO2 抑制时间，默认0，仅预运行可写 */
    {0x1801, 5, CO_OD_UNSIGNED16, 2, 1, 1, 100, 65535},             /* TPDO2 事件周期，默认100ms，仅预运行可写 */
    {0x1A00, 0, CO_OD_UNSIGNED8, 1, 0, 0, 1, 1},                    /* TPDO1 固定映射一个对象 */
    {0x1A00, 1, CO_OD_UNSIGNED32, 4, 0, 0, 0x60000108, UINT32_MAX}, /* 映射 DI：6000:01，8位 */
    {0x1A01, 0, CO_OD_UNSIGNED8, 1, 0, 0, 2, 2},                    /* TPDO2 固定映射两个对象 */
    {0x1A01, 1, CO_OD_UNSIGNED32, 4, 0, 0, 0x64010110, UINT32_MAX}, /* 映射 AI1：6401:01，16位 */
    {0x1A01, 2, CO_OD_UNSIGNED32, 4, 0, 0, 0x64010210, UINT32_MAX}  /* 映射 AI2：6401:02，16位 */
};
```

它定义的是所有对象条目的“规则模板”，不是最终可直接访问的对象条目

这里：

```c
definition_t
    → 每个对象规则的结构体类型

definitions[]
    → 35 个规则结构体组成的数组
```

它保存：对象地址（索引+子索引）、字节类型（8，16，32）、字节数、读写权限、默认值、最大值







## 4：项目实际对象表共用的读取回调

```c
static co_status_t read_value(void *user, uint8_t *data, uint8_t length)
{
    const co_device_value_t *cell = user; /* user 保存 fields[i] 的地址；cell 指向原存储，不创建副本 */
    uint8_t i;
    for (i = 0; i < length; ++i)
    {
        data[i] = (uint8_t)(cell->value >> (8u * i)); /* 右移后取低8位：data[0]先放最低字节 */
    }
    return CO_OK;
}
```

它的整体作用是：

> 从某个对象的 `value` 中读取当前数值，拆成一个个字节，写入 `data[]` 数组。

示例：

```c
co_device_value_t readinput = {
    .value = 0x1234,
    .writes = 0
};

uint8_t data[2] = {0};
```

调用：

```c
co_status_t status =  read_value(&readinput, data, 2);
```

进入函数后：

```c
user  ──► &readinput
data  ──► data[0]
length = 2
```

执行结束：

```c
output.value = 0x1234   没有改变
output.writes = 0       没有改变

data[0] = 0x34
data[1] = 0x12

返回状态值status = CO_OK
```





## 5:   项目实际对象表共用的写回调

```c
static co_status_t write_value(void *user, const uint8_t *data, uint8_t length)
{
    co_device_value_t *cell = user; /* 还原为存储指针，通过它修改原对象的值 */
    uint32_t next = 0;              /* 暂存解码结果，尚未写入原对象 */
    uint8_t i;
    for (i = 0; i < length; ++i)
    {
        next |= (uint32_t)data[i] << (8u * i); /* 低字节移0位，高字节依次移8/16/24位 */
    }
    cell->value = next; /* 字节全部组合完成后，提交到这个对象的 RAM 存储 */
    ++cell->writes;     /* 同值重写也产生一次配置更新。 */
    return CO_OK;
}
```

它的作用是：

> 从 `data[]` 数组读取字节，按小端顺序组合成整数，然后写入某个对象的 `value`。



示例：写入 DO 值 5

```c
co_device_value_t output = {
    .value = 0,
    .writes = 0
};

uint8_t data[1] = {5};

co_status_t status =write_value(&output, data, 1);
调用时：
user    ──► &output
data    ──► data[0]
length  = 1
函数执行：
next = 0
i = 0
next |= data[0] << 0
next = 5

output.value = 5
output.writes = 1
```

最终：

```c
output.value  = 5
output.writes = 1
data[0]       = 5
status        = CO_OK
```

注意：

```
data[0] 没有被修改
```

它只是作为输入，被函数读取。



对于项目实际对象，最终就相当于：

```c
write_value(&device->fields[i], data, data_length);
```

如何理解&device->fields[ i ]  ？

```c
typedef struct {
    co_device_value_t fields[CO_DEVICE_OD_COUNT];
    co_od_entry_t entries[CO_DEVICE_OD_COUNT];
    co_od_table_t table;
} co_device_od_t;
```

创建一个 `co_device_od_t` 类型的变量 `device` 后，它内部包含：

```c
device
 ├── fields[35]
 │     └── 35 个 co_device_value_t 结构体元素
 │
 ├── entries[35]
 │     └── 35 个 co_od_entry_t 结构体元素
 │
 └── table
       └── 1 个 co_od_table_t 结构体
```

我们刚刚不是举例单独创建的一个结构体变量 ：output   

项目实际对象时，我们就无需再单独创建了，output   变成了  fields 数组中的第 i 个结构体元素→ fields[i]   





## 6：初始化

顺序就是这样：

```c
co_device_od_t device = {0};
        │
        ▼
co_device_od_init(&device, ...);
        │
        ├── 填写 fields[] 的初始值
        ├── 填写 entries[] 的对象规则
        ├── 绑定 read/write 回调
        ├── 绑定 entries[i].user
        └── 设置 table.entries 和 table.count
        │
        ▼
ready(&device);
        │
        ├── device 地址有效吗？
        ├── table.entries 是否指向 device.entries？
        └── table.count 是否等于对象总数？
```

初始化成功后：

```
ready(&device) == 1
```

如果还没有初始化，或者初始化后的表信息不完整：

```
ready(&device) == 0
```

所以 `ready()` 不是初始化函数，而是一个初始化结果检查函数。后续对 `device.fields[]`、`device.entries[]` 或 `device.table` 的操作，通常先通过它确认对象表已经准备好。



#### 1：函数解析

```c
co_status_t co_device_od_init(co_device_od_t *device, uint8_t node_id,
                              const co_device_identity_t *identity)
{
    size_t i;
    co_device_identity_t id = {0, 1, 0x10000, 0}; /* 本地身份副本，先填开发默认值 */
    if (device == NULL)
        return CO_ERR_ARGUMENT;
    if (node_id == 0 || node_id > CO_NODE_ID_MAX)
        return CO_ERR_NODE_ID;
    if (identity != NULL)
        id = *identity; /* 写入 device 前保存输入身份。 */
    for (i = 0; i < CO_DEVICE_OD_COUNT; ++i)
    {
        const definition_t *d = &definitions[i]; /* d指向第i条只读规则 */
        co_od_entry_t *e = &device->entries[i];  /* e指向要填写的实际条目 */
        device->fields[i].value = d->initial;    /* 为本条目准备初始数据 */
        device->fields[i].writes = 0;            /* 初始化不算主站写操作 */
        if (d->subindex == 1 && (d->index == 0x1400 ||
                                 d->index == 0x1800 || d->index == 0x1801))
        {
            device->fields[i].value += node_id; /* 默认连接集与节点号绑定。 */
        }
        e->index = d->index;
        e->subindex = d->subindex;
        e->type = d->type;
        e->length = d->length;
        e->access = d->writable ? CO_OD_READ_WRITE : CO_OD_READ_ONLY; /* 条件 ? 真值 : 假值 */
        e->min_value = 0;
        e->max_value = d->maximum;
        e->write_preop_only = d->preop;
        e->read = read_value;                        /* 保存读函数地址，所有条目共用此函数 */
        e->write = d->writable ? write_value : NULL; /* 只读对象不绑定写回调 */
        e->user = &device->fields[i];                /* 关键连接：同一个回调通过不同user访问不同对象 */
    }
    device->fields[IDENTITY_SLOT].value = id.vendor_id;
    device->fields[IDENTITY_SLOT + 1].value = id.product_code;
    device->fields[IDENTITY_SLOT + 2].value = id.revision;
    device->fields[IDENTITY_SLOT + 3].value = id.serial;
    device->table.entries = device->entries; /* 数组转为首元素地址，供co_od_find查找 */
    device->table.count = CO_DEVICE_OD_COUNT;
    return CO_OK;
}
```



##### **① 先看怎样调用：这次有三个参数**

```c
co_device_od_t device = {0};

co_status_t status =
    co_device_od_init(&device, 1, NULL);
&device → 初始化哪个设备对象变量
1       → 本节点的编号，用于计算 PDO 的 COB-ID
NULL    → 不额外提供身份，使用开发默认身份
    
//identity
它指向的结构体包含四项：
typedef struct {
    uint32_t vendor_id;     /* 哪个厂商 */
    uint32_t product_code;  /* 哪种产品 */
    uint32_t revision;      /* 哪个版本 */
    uint32_t serial;        /* 哪一台设备 */
} co_device_identity_t;
可以把它理解成设备的“铭牌”：
身份信息
 ├── 厂商编号
 ├── 产品编号
 ├── 版本号
 └── 序列号
初始化函数会将这些信息保存到对象字典：
vendor_id    → 0x1018:01
product_code → 0x1018:02
revision     → 0x1018:03
serial       → 0x1018:04
以后上位机通过 SDO 读取这些对象，就能了解连接的是什么设备。
它与 node_id 用途不同：
node_id  → 当前 CANopen 网络中，这个节点的编号
identity → 这台设备的厂商、产品、版本和序列号
目前学习时直接传 NULL：
co_device_od_init(&device, 1, NULL);
意思就是：暂时不自行提供身份信息，使用函数中准备的开发默认值。
```

你已经理解了，创建 `device` 后就有：

```c
device
 ├── fields[35]   每个元素有 value、writes
 ├── entries[35]  每个元素保存对象规则、回调、user
 └── table       保存条目数组地址和数量
```

`= {0}` 只是零初始化，下面这个函数才负责把有意义的配置填进去。



##### **② 准备身份，检查参数**

```
size_t i;
```

`i` 用作数组下标，后面从 `0` 遍历到 `34`。



创建局部身份变量：

```c
co_device_identity_t id = {0, 1, 0x10000, 0};
```

```c
id
 ├── vendor_id    = 0
 ├── product_code = 1
 ├── revision    = 0x10000
 └── serial      = 0
```

随后检查设备地址和节点号：

```c
if (device == NULL)
    return CO_ERR_ARGUMENT;

if (node_id == 0 || node_id > CO_NODE_ID_MAX)
    return CO_ERR_NODE_ID;
```

如果调用者传入了身份地址：

```c
if (identity != NULL)
    id = *identity;
```

`*identity` 取得调用者的整个身份结构体，复制到局部变量 `id`。

我们这次传的是 `NULL`，因此保留默认身份。





##### **③进入循环，先拿到两个结构体的地址**

```
for (i = 0; i < CO_DEVICE_OD_COUNT; ++i)
```

每次循环处理一个对象。

```c
const definition_t *d = &definitions[i];
co_od_entry_t *e = &device->entries[i];
```

这两句只是创建指针：

```c
d ──► definitions[i]       本次要读取的规则模板

e ──► device->entries[i]   本次要填写的实际条目
```

没有创建新的规则，也没有复制整个条目。

例如循环进行到 `i = 11` 时：

```c
d ──► definitions[11]       DO 对象的规则

e ──► device->entries[11]   DO 对象的实际条目
```





##### **④ 为这个对象设置当前初值和计数**

```c
device->fields[i].value = d->initial;
device->fields[i].writes = 0;
```

对于 DO，模板中的 `initial` 是 `0`，于是：

```
device->fields[11]
    ├── value  = 0
    └── writes = 0
```

下一段是特殊处理：

```c
if (d->subindex == 1 &&
    (d->index == 0x1400 ||
     d->index == 0x1800 ||
     d->index == 0x1801))
{
    device->fields[i].value += node_id;
}
```

只有三个 PDO 的 COB-ID 对象需要加节点号。例如：

```
RPDO1 COB-ID 模板初值 = 0x200
节点号                = 1
最终保存值            = 0x201
```

**DO 数据对象 `0x6200:01` 不满足这个条件，所以不会加节点号。**





##### **⑤ 根据模板填写实际条目**

```c
e->index = d->index;
e->subindex = d->subindex;
e->type = d->type;
e->length = d->length;
```

对于 `i = 11`，相当于：

```c
device->entries[11].index = 0x6200;
device->entries[11].subindex = 1;
device->entries[11].type = CO_OD_UNSIGNED8;
device->entries[11].length = 1;
```

再填写权限：

```
e->access = d->writable
            ? CO_OD_READ_WRITE
            : CO_OD_READ_ONLY;
```

这句的意思是：

```c
d->writable 非零 → access 设置为可读写
d->writable 为0  → access 设置为只读
```

随后填写范围和状态限制：

```
e->min_value = 0;
e->max_value = d->maximum;
e->write_preop_only = d->preop;
```

DO 对象对应：

```
允许范围：0～15
不要求仅在 Pre-operational 写入
```





##### **⑥ 绑定昨天学过的读写回调**

```
e->read = read_value;
```

把读取函数的地址存入条目，**这里不执行读取函数**。

```
e->write = d->writable ? write_value : NULL;
允许写入 → 保存 write_value 的地址
只读对象 → 保存 NULL
```

接下来是关键连接：

```
e->user = &device->fields[i];
```

对于 DO：

```c
typedef struct
{
    uint16_t index;           /* 对象索引，例如 0x6200 */
    uint8_t subindex;         /* 对象子索引，例如 0x01 */
    co_od_type_t type;        /* 对象数据类型，同时决定标准长度 */
    co_od_access_t access;    /* 主站的读写权限 */
    uint8_t length;           /* 对象在 SDO/PDO 中占用的字节数 */
    int64_t min_value;        /* 写入值允许的最小值 */
    int64_t max_value;        /* 写入值允许的最大值 */
    uint8_t write_preop_only; /* 非零表示只允许 Pre-operational 状态写入 */
    co_od_read_fn read;       /* 读取对象值的回调 */
    co_od_write_fn write;     /* 写入对象值的回调；只读对象可为 NULL */
    void *user;               /* 回调使用的应用层私有数据 */
} co_od_entry_t;              // 对象条目结构体


device->entries[11]
    ├── read ──────► read_value()
    ├── write ─────► write_value()
    │
    └── user ──────► device->fields[11]
                        ├── value  = 0
                        └── writes = 0
```

这就解释了昨天的问题：**`read_value()`、`write_value()` 收到的 `user` 地址，是在这里提前保存的。**

循环接着处理下一行，直到 35 个对象全部配置完。



##### **⑦ 最后填入身份，建立查表入口**

```c
//IDENTITY_SLOT   → fields[4]  身份信息的起始位置
device->fields[IDENTITY_SLOT].value = id.vendor_id;
device->fields[IDENTITY_SLOT + 1].value = id.product_code;
device->fields[IDENTITY_SLOT + 2].value = id.revision;
device->fields[IDENTITY_SLOT + 3].value = id.serial;
```

把之前准备好的身份值，写到四个身份对象对应的存储中。

然后：

```
device->table.entries = device->entries;
device->table.count = CO_DEVICE_OD_COUNT;
```

建立这个关系：

```c
device.table
    ├── entries ───► device.entries[0]
    │                device.entries[1]
    │                ...
    │                device.entries[34]
    │
    └── count = 35
```

此后，`co_od_find()` 就能通过 `device.table` 查找这些条目。昨天的 `ready()` 检查的正是这里的数组连接和数量，**它并不会逐项检查全部条目内容。**

最后：

```
return CO_OK;
```

表示初始化完成。整个函数的执行过程是：

```
检查参数，准备身份
        ↓
逐行读取 definitions[]
        ↓
设置 fields[i] 的初始值
        ↓
填写 entries[i] 的访问规则
        ↓
保存回调地址，将 user 指向 fields[i]
        ↓
填入身份信息
        ↓
让 table 指向 entries[]，记录数量35
        ↓
返回 CO_OK
```

这个函数只准备好数据和连接，不调用读写回调、不操作 GPIO，也不发送 CAN 报文。







#### 2：思路梳理

##### ①：容易混淆的好多个结构体

```c
//co_deceive_od.c
typedef struct
{
    uint16_t index;                  /* 对象索引，例如 0x6200 */
    uint8_t subindex;                /* 子索引，例如 1；与 index 共同定位对象 */
    co_od_type_t type;               /* 内部数据类型标记，不等于 EDS 的类型编号 */
    uint8_t length, writable, preop; /* 字节数、是否可写、是否仅预运行可写 */
    uint32_t initial, maximum;       /* 初值与允许上限；本设备对象下限统一为 0 */
} definition_t;

// 一条对象规则的模板类型

```



```c
//co_deceive_od.h
typedef struct {
    uint32_t value;
    uint32_t writes;
} co_device_value_t;

//保存一个设备对象的当前值和写入次数

typedef struct {
    co_device_value_t fields[CO_DEVICE_OD_COUNT];
    co_od_entry_t entries[CO_DEVICE_OD_COUNT];
    co_od_table_t table;
} co_device_od_t;

//整台设备的对象字典容器
```



```c
//co_od .h

typedef struct
{
    uint16_t index;           /* 对象索引，例如 0x6200 */
    uint8_t subindex;         /* 对象子索引，例如 0x01 */
    co_od_type_t type;        /* 对象数据类型，同时决定标准长度 */
    co_od_access_t access;    /* 主站的读写权限 */
    uint8_t length;           /* 对象在 SDO/PDO 中占用的字节数 */
    int64_t min_value;        /* 写入值允许的最小值 */
    int64_t max_value;        /* 写入值允许的最大值 */
    uint8_t write_preop_only; /* 非零表示只允许 Pre-operational 状态写入 */
    co_od_read_fn read;       /* 读取对象值的回调 */
    co_od_write_fn write;     /* 写入对象值的回调；只读对象可为 NULL */
    void *user;               /* 回调使用的应用层私有数据 */
} co_od_entry_t; 

//一个对象的实际访问条目

typedef struct
{
    const co_od_entry_t *entries; /* 对象条目数组 */
    size_t count;                 /* 数组中有效条目数 */
} co_od_table_t;                  // 对象条目挂载的对象表

//保存条目数组地址和数量的表描述

设备对象字典					   //co_device_od_t
    ├── fields  → 各对象的数据存储 //co_device_value_t
    ├── entries → 各对象的访问条目 //co_device_value_t
    └── table   → 条目数组地址和数量//co_od_table_t
```







##### ②：创建后的结构

```c
co_device_od_t device = {0};
```

这一句创建后的结构如下：

```c
device                         类型：co_device_od_t
│
├── fields[35]                 一个结构体数组
│    │
│    └── 每个 fields[i]        类型：co_device_value_t
│         ├── value            当前数值
│         └── writes           写入次数
│
├── entries[35]                另一个结构体数组
│    │
│    └── 每个 entries[i]       类型：co_od_entry_t
│         ├── index/subindex   对象地址
│         ├── 类型、权限、长度、范围
│         ├── read/write       回调函数地址
│         └── user             数据地址
│
└── table                      单个结构体，类型：co_od_table_t
     ├── entries               一个指针，不是另一个数组
     └── count                 条目数量
```

最容易混淆的两个 `entries`，区别在这里：

```
device.entries
```

是真正存放 35 个条目的结构体数组。



```
而  device.table.entries
```

是**一个指针成员**，初始化后保存上面那个数组的首元素地址：

```c
device.table.entries ──► device.entries[0]
                         device.entries[1]
                         ...
                         device.entries[34]
```

因此，`table` 没有再复制一份条目数组。它只保存“数组在哪里、有多少项”。



再看 `definition_t`。它不在 `device` 里面。

它用于 `.c` 文件中另外定义的规则数组：

```c
static const definition_t definitions[35] = {
    /* 已经逐行填写好的对象规则 */
};
```

所以整体只有这两块：

```
definitions[35]                device
已经写好的规则模板              一台设备的实际存储
                              ├── fields[35]
                              ├── entries[35]
                              └── table
```

拿你熟悉的 **DO 对象**看：

```c
definitions[11]
    “DO 地址6200:01，1字节，可写，默认0，最大15”
                      │
                      │ 初始化时使用这些规则
                      ▼
device.entries[11]
    “DO 的访问规则、读写回调”
    user ─────────────────────► device.fields[11]
                                    value：当前 DO 命令
                                    writes：写入次数
```

先只记这四句话：

```
definitions[] → 已经写好的规则模板
entries[]     → 实际访问条目，保存规则和回调
fields[]      → 当前数值和写入次数
table         → 保存 entries 数组的地址和数量
```





##### ③：我的理解

```c
entries 的赋值由模板结构体，然后他其中有3个值，模板提供不了，read和write和user ，然后他们会在读和写函数书写后指向他们，然后user指向deceive的fields 中的每一个结构体变量，由i索引实现，fields的结构体变量，然后结构体变量的子变量分别是value和writes写入次数，这个应该是对应后面stm32的AD采集值或者数字输出值和写入次数 ，最后是table   结构体变量的两个子变量，一个是指针，指向了entries 数组中的第一个对象条目地址，count是对象条目数量
```

你已经把主要连接关系理解对了。 只需要把“什么时候建立连接”说得更准确：函数写好以后，由 `co_device_od_init()` 把函数地址和数据地址填入条目。

以循环中的第 `i` 个对象为例：

```c
definitions[i]：规则模板
       │
       │ 初始化时读取
       ▼
device.entries[i]：实际条目
       ├── 索引、类型、长度、权限、范围等
       │
       ├── read  ─────► read_value
       ├── write ─────► write_value（只读对象则为 NULL）
       │
       └── user ──────► device.fields[i]
                            ├── value   当前数值
                            └── writes  写回调执行次数
```

对应代码就是：

```c
e->read = read_value;
e->write = d->writable ? write_value : NULL;
e->user = &device->fields[i];
```

你说的 ADC 采集值、数字输出值都对。不过 `fields[i].value` 也可能保存心跳周期、身份信息、PDO 参数等，取决于它对应哪个对象。

另外，应用更新 ADC、DI 数据时不会自动增加 `writes`；当前代码中，只有执行 `write_value()` 才会增加它。

最后，你对 `table` 的理解完全正确：

```c
device.table
    ├── entries ──► device.entries[0]
    └── count = 35
```



## 7：检查初始化是否正确

```c
static int ready(const co_device_od_t *device)
{
    return device != NULL && device->table.entries == device->entries &&
           device->table.count == CO_DEVICE_OD_COUNT;
}
```

它检查设备指针、条目数组连接和条目数量这三项是否符合预期

```c
static → 仅供这个 .c 文件内部使用
int    → 这里返回真假：1表示通过，0表示未通过
device → 接收设备结构体变量的地址
const  → 检查过程中不通过这个指针修改设备
```



第一个条件：没有传入空指针

```
device != NULL
传入 &某个设备变量 → 继续检查
传入 NULL         → 条件为假
```

注意，这只能排除空指针，不能判断任意非空地址是否有效。调用者仍要提供有效的设备对象地址。



第二个条件：表是否指向本设备自己的条目数组**

```
device->table.entries == device->entries
```

你已经知道两边的区别：

```
左边：device->table.entries
      table 中保存的指针

右边：device->entries
      本设备的条目数组，在这里转换为首元素地址
```

初始化函数之前做了：

```
device->table.entries = device->entries;
```

因此初始化成功后：

```
device->table.entries ──► device->entries[0]
```

这里的 `==` 比较的是**地址是否相等**，不是逐个比较数组内容。



第三个条件：数量是不是 35**

```
device->table.count == CO_DEVICE_OD_COUNT
```

初始化函数之前做了：

```
device->table.count = CO_DEVICE_OD_COUNT;
```

所以这里检查：

```
table.count 是否为 35
```



`&&` 把三个条件连起来**

```
return 条件1 && 条件2 && 条件3;
device 不是 NULL？
       │ 是
       ▼
table.entries 指向本设备 entries 数组？
       │ 是
       ▼
table.count 等于35？
       │ 是
       ▼
返回1

任意一步为假 → 停止后续判断，返回0
```

这叫**短路求值**。因此传入 `NULL` 时，不会继续执行 `device->table.entries`，避免解引用空指针。



#### 总览：

```c
① device != NULL
   只检查不是空指针，不能证明地址一定有效。

② device->table.entries == device->entries
   检查表指针是否指向当前 device 自己的条目数组首元素。
   不检查数组中每个条目的内容。

③ device->table.count == 35
   检查记录的条目数量是不是35。
   不会实际遍历、清点条目。
三个条件都成立就返回 1，否则返回 0。
```





## 8：更新输入采集值函数

```c
co_status_t co_device_od_update_inputs(co_device_od_t *device, uint8_t di,
                                       uint16_t ai1, uint16_t ai2)
{
    if (!ready(device))
        return CO_ERR_ARGUMENT;
    if (di > 15 || ai1 > 4095 || ai2 > 4095)
        return CO_ERR_OD_VALUE;
    device->fields[DI_SLOT].value = di;
    device->fields[AI1_SLOT].value = ai1;
    device->fields[AI2_SLOT].value = ai2;
    return CO_OK;
}
```

把应用提供的 DI 状态和两路 ADC 采样值，更新到对象字典背后的存储中。



#### **① 四个参数分别是什么**

| 形参     | 传入内容                              |
| -------- | ------------------------------------- |
| `device` | 已初始化的设备对象变量地址            |
| `di`     | 四路数字输入的逻辑状态，范围 `0～15`  |
| `ai1`    | 第一路 ADC 原始采样值，范围 `0～4095` |
| `ai2`    | 第二路 ADC 原始采样值，范围 `0～4095` |

例如：

```c
co_status_t status =
    co_device_od_update_inputs(&device, 5, 1234, 3000);
```

意思是：

```c
更新 device 这台设备的数据
    ├── DI  = 5
    ├── AI1 = 1234
    └── AI2 = 3000
```



#### **② 先检查设备是否准备好**

```c
if (!ready(device))
    return CO_ERR_ARGUMENT;
```

这里就用到了刚学的 `ready()`：

```c
ready(device) 返回1
    ↓
!1 为0，不进入错误分支，继续

ready(device) 返回0
    ↓
!0 为1，返回参数错误
```



#### **③ 检查三个输入值的范围**

```c
if (di > 15 || ai1 > 4095 || ai2 > 4095)
    return CO_ERR_OD_VALUE;
```

为什么是这些上限？

```
DI：四路状态使用低四位
    0000 → 0
    1111 → 15

AI：12位 ADC 原始值
    最小0，最大4095
```

例如 `di = 5`：

```c
5 = 二进制 0101

bit3  bit2  bit1  bit0
 0     1     0     1
DI4   DI3   DI2   DI1
```

表示 DI1、DI3 的逻辑状态为有效。低电平按下到逻辑 `1` 的转换，**由应用采集代码处理**，**这个函数只接收转换好的状态值**。

三个参数都是无符号类型，因此这里不需要再检查小于零。



#### **④ 检查通过后，更新对应的存储**

```c
device->fields[DI_SLOT].value = di;
device->fields[AI1_SLOT].value = ai1;
device->fields[AI2_SLOT].value = ai2;
```

之前的枚举定义是：

```c
DI_SLOT  = 9,
AI1_SLOT = 13,
AI2_SLOT = 14
```

所以这次调用相当于执行：

```c
device.fields[9].value  = 5;
device.fields[13].value = 1234;
device.fields[14].value = 3000;
```

对应关系：

```
di = 5 ───────► fields[9].value    对象 0x6000:01

ai1 = 1234 ───► fields[13].value   对象 0x6401:01

ai2 = 3000 ───► fields[14].value   对象 0x6401:02
```

**这里直接更新 `value`，不会增加 `writes`，也没有调用 `write_value()`。**

因为这是应用更新输入采样值，不是上位机通过对象字典写入。输入对象对上位机只读，但应用仍需要不断更新采样结果。



#### **⑤ 为什么先全部检查，再赋值？**

例如调用：

```
co_device_od_update_inputs(&device, 5, 1234, 5000);
```

`ai2 = 5000` 超过上限，函数会在检查处直接返回：

```
发现 AI2 越界
      ↓
返回 CO_ERR_OD_VALUE
      ↓
DI、AI1、AI2 的旧值全部保留
```

不会出现“DI 和 AI1 已更新，AI2 却失败”的半组更新。这里仍要求调用者串行访问，不代表三条赋值是硬件上的原子操作。



#### **⑥ 和真实 STM32 的关系**

```
GPIO / ADC 采集代码
    │
    ├── 整理四路 DI 逻辑状态
    ├── 取得 AI1 采样值
    └── 取得 AI2 采样值
              │
              ▼
co_device_od_update_inputs(&device, di, ai1, ai2)
              │
              ▼
更新 fields[] 中的输入数据
              │
              ▼
之后对象读回调就能读到新值
```

这个函数不采集 ADC、不读取 GPIO，也不发送 CAN 报文；它只检查并保存应用已经取得的输入数据。



#### ⑦理解偏差

**ADC 那部分理解对，但四个按键的状态都存进 `fields[9].value`，不是分别占用四个 `fields` 元素。**

因为四路 DI 是按位打包的：

```c
fields[9].value 的低四位

bit3   bit2   bit1   bit0
 DI4    DI3    DI2    DI1
```

例如 DI1、DI3 有效：

```c
二进制：0101
十进制：5

fields[9].value = 5;
```

实际分配是：

```c
fields[9].value  → 四路 DI 的打包状态
fields[10].value → DO 数据字节数量，固定为1
fields[11].value → 四路 DO 的打包命令
fields[12].value → AI 通道数量，固定为2
fields[13].value → AI1 的 ADC 采样值
fields[14].value → AI2 的 ADC 采样值
```

所以这次调用：

```c
co_device_od_update_inputs(&device, 5, 1234, 3000);
```

只更新三个位置：

```c
fields[9].value  = 5
fields[13].value = 1234
fields[14].value = 3000
```



## 9：更新输入采集值函数

```c
co_status_t co_device_od_get_outputs(const co_device_od_t *device, uint8_t *outputs)
{
    if (!ready(device) || outputs == NULL)
        return CO_ERR_ARGUMENT;
    *outputs = (uint8_t)device->fields[DO_SLOT].value; /* 解引用，修改调用方传入的变量 */
    return CO_OK;
}
```

这个函数负责：取出对象表中保存的四路 DO 命令，写入调用者提供的变量。

然后应用层，依据这个变量来控制led 的状态变化。



#### **① 两个参数**

```
device  → 已初始化的设备对象地址，从它里面取 DO 值
outputs → 调用者变量的地址，把取出的 DO 值写到这里
```

`const` 表示这个函数不通过 `device` 修改设备数据，但可以通过 `outputs` 修改调用者的输出变量。



#### **② 先检查参数**

```
if (!ready(device) || outputs == NULL)
    return CO_ERR_ARGUMENT;
```

任意一项不满足，就返回错误：

```
设备对象未通过 ready() 检查
                或
没有提供接收结果的变量地址
```

这样才能安全执行后面的读取和赋值。



#### **③ 读取 DO 值，填写输出变量**

```
*outputs = (uint8_t)device->fields[DO_SLOT].value;
```

我们已经知道：

```
DO_SLOT = 11
```

所以右边读取的是：

```
device->fields[11].value
```

它保存四路 DO 的打包命令：

```
bit3   bit2   bit1   bit0
 DO4    DO3    DO2    DO1
```

`value` 是 `uint32_t`，这里转成 `uint8_t`，用于填写一个字节的输出变量。

左边：

```
*outputs
```

表示找到 `outputs` 指向的变量，并修改它。







#### **④ 一个具体调用例子**

假设设备已初始化，并且前面一次合法对象写入已经使：

```
device.fields[11].value = 5
```

现在调用：

```
uint8_t led_command = 0;

co_status_t status =
    co_device_od_get_outputs(&device, &led_command);
```

进入函数后：

```
device  ──► 调用者的设备变量
outputs ──► led_command
```

关键赋值：

```
*outputs = (uint8_t)device->fields[11].value;
```

在本例中相当于：

```
led_command = 5;
```

最终：

```
device.fields[11].value = 5       保持不变
led_command             = 5       得到 DO 命令
status                  = CO_OK   返回成功状态
```



#### **⑤ 这个函数不直接控制 LED**

后续完整过程是：

```
SDO / RPDO 服务接受输出命令
              ↓
更新 fields[11].value
              ↓
应用调用 co_device_od_get_outputs()
              ↓
取得 led_command
              ↓
应用根据各个位设置 GPIO
              ↓
LED 状态变化
```

因此，这里的 `get_outputs` 是“获取输出命令”，不是读取 GPIO 引脚实际电平，也不是在函数中设置引脚。

与刚学的输入更新函数对比：

```
update_inputs()
采集结果 ──► fields[] 中的 DI、AI 数据

get_outputs()
fields[] 中的 DO 命令 ──► 调用者的变量
```



#### ⑤总览

```c
对象表中的四路 DO 命令
    device.fields[11].value
              ↓
co_device_od_get_outputs()
              ↓
调用者的变量，例如 led_command
              ↓
应用层分别读取低四位
              ↓
控制四路 GPIO，使 LED 状态变化
例如 led_command = 5，二进制为 0101。按我们项目高电平点亮的设计，正常输出时就是 LED1、LED3 亮，LED2、LED4 灭。
这个函数只负责取出命令，实际操作 GPIO 的是应用层。
```



## 10：把应用提供的错误状态，保存到对象字典的错误寄存器 `0x1001:00` 中

```c
co_status_t co_device_od_set_error(co_device_od_t *device, uint8_t error)
{
    if (!ready(device))
        return CO_ERR_ARGUMENT;
    if ((error & 0x40u) != 0)
        return CO_ERR_OD_VALUE;
    device->fields[ERROR_SLOT].value = error;
    return CO_OK;
}
```



#### **① 两个参数**

```
device → 要更新的设备对象地址
error  → 应用准备好的错误状态字节
```

`error` 是一个字节，其中不同的位表示不同类别的错误，可以同时有多个位置为 `1`。具体故障如何对应这些位，后面的 EMCY 阶段再实现。



#### **② 检查设备**

```
if (!ready(device))
    return CO_ERR_ARGUMENT;
```

与前面的函数一样，设备没有通过基础检查，就提前返回。



#### **③ 检查保留位 bit6**

```
if ((error & 0x40u) != 0)
    return CO_ERR_OD_VALUE;
```

这里的 `&` 是**按位与**，不是取地址。`0x40` 对应：

```
位号：   bit7 bit6 bit5 bit4 bit3 bit2 bit1 bit0
0x40：     0    1    0    0    0    0    0    0
```

因此：

```
error & 0x40u
```

就是单独检查 `error` 的 **bit6 是否为 1**：

```
bit6 = 0 → 结果为0，允许继续
bit6 = 1 → 结果非0，返回 CO_ERR_OD_VALUE
```

因为错误寄存器的 **bit6 是保留位，必须为 0**。

例如：

```
error = 0x01：

0000 0001
0100 0000  按位与
─────────
0000 0000  → 通过
error = 0x40：

0100 0000
0100 0000  按位与
─────────
0100 0000  → 拒绝
```



#### **④ 保存错误状态**

```
device->fields[ERROR_SLOT].value = error;
```

之前定义了：

```
ERROR_SLOT = 1
```

因此相当于：

```
device->fields[1].value = error;
error
  │
  ▼
fields[1].value
  │
  └── 对应对象 0x1001:00：错误寄存器
```

**这是替换整个错误状态字节，不是自动累加错误位。** 也不会增加 `writes`。



#### **⑤ 一个调用例子**

假设 `device` 已初始化：

```
co_status_t status =
    co_device_od_set_error(&device, 0x01u);
```

执行后：

```
device.fields[1].value = 0x01
status                 = CO_OK
```

如果接着传入非法的保留位：

```
status = co_device_od_set_error(&device, 0x40u);
```

执行后：

```
device.fields[1].value = 0x01          保留旧值
status                 = CO_ERR_OD_VALUE
```

如果应用确认所有错误已经解除，可以写入 `0`：

```
co_device_od_set_error(&device, 0u);
```

这个函数的范围是：

```
应用检测、判断故障
        ↓
整理出 error 错误状态字节
        ↓
co_device_od_set_error()
        ↓
检查并更新错误寄存器
```

它不检测故障，也不发送 EMCY 报文；这里只负责保存应用已经确定的错误状态。





## 11：取出当前心跳周期和它的成功写入次数，分别填入调用者提供的两个变量。

```c
co_status_t co_device_od_get_heartbeat(const co_device_od_t *device,
                                       uint16_t *period_ms, uint32_t *writes)
{
    if (!ready(device) || period_ms == NULL || writes == NULL)
        return CO_ERR_ARGUMENT;
    *period_ms = (uint16_t)device->fields[HEARTBEAT_SLOT].value;
    *writes = device->fields[HEARTBEAT_SLOT].writes;
    return CO_OK;
}
```



#### **① 三个参数**

| 形参        | 用途                               |
| ----------- | ---------------------------------- |
| `device`    | 已初始化设备的地址，从里面读取数据 |
| `period_ms` | 接收心跳周期的变量地址，单位毫秒   |
| `writes`    | 接收成功写入次数的变量地址         |

`period` 是“周期”，`ms` 是“毫秒”。





#### **② 检查设备和两个输出地址**

```
if (!ready(device) || period_ms == NULL || writes == NULL)
    return CO_ERR_ARGUMENT;
```

任意一项不满足，就返回参数错误，两个输出变量都不修改。





#### **③ 取出心跳周期**

```
*period_ms = (uint16_t)device->fields[HEARTBEAT_SLOT].value;
```

之前定义了：

```
HEARTBEAT_SLOT = 2
```

所以读取的是：

```
device->fields[2].value
    └── 对象 0x1017:00 的当前心跳周期
```

`value` 的存储类型为 `uint32_t`，心跳周期本身是 `uint16_t`，因此这里做类型转换。



#### **④ 取出写入次数**

```
*writes = device->fields[HEARTBEAT_SLOT].writes;
```

读取的是：

```
device->fields[2].writes
    └── 心跳周期对象通过写回调成功写入的次数
```

注意，这不是已经发送了多少帧心跳。

------



#### **⑤ 完整调用实例**

设备初始化后，默认心跳周期为 `1000 ms`，写入次数为 `0`：

```c
uint16_t heartbeat_period = 0;
uint32_t heartbeat_writes = 0;

co_status_t status = co_device_od_get_heartbeat(
    &device,
    &heartbeat_period,
    &heartbeat_writes
);
```

进入函数后：

```
device    ──► 设备变量

period_ms ──► heartbeat_period

writes    ──► heartbeat_writes
```

两句赋值的效果是：

```
device.fields[2]
    ├── value = 1000 ─────► heartbeat_period = 1000
    └── writes = 0 ───────► heartbeat_writes = 0

函数返回 CO_OK ──────────► status
```

原来的 `device.fields[2]` 不变，只是把数据取出来。

------



#### **⑥ 为什么还要取得写入次数？**

后续心跳服务需要知道：心跳周期有没有被重新写入。

例如：

```
初始：
周期 = 1000，写入次数 = 0
          ↓
上位机成功写入周期500
          ↓
周期 = 500，写入次数 = 1
```

甚至写入相同数值：

```
当前：
周期 = 500，写入次数 = 1
          ↓
上位机再次成功写入500
          ↓
周期 = 500，写入次数 = 2
```

只比较周期，看不出第二次写入；观察写入次数，就能识别这次更新，让后续心跳服务重新开始计时。

```c
co_device_od_get_heartbeat()
    ├── 取周期：多久发送一次
    └── 取写入次数：配置是否被重新写入
                 ↓
后续 Heartbeat 服务
    └── 根据配置管理计时、生成并发送心跳
```

这个函数本身只读取配置，不计时，也不发送心跳。周期为 `0` 时，后续心跳服务应关闭周期发送。
