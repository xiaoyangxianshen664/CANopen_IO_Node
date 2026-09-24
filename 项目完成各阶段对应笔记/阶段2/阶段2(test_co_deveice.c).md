# 解析



## 1：断言

```c
#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "%s:%d: %s\n", \
                __FILE__, __LINE__, #x); \
        return 1; \
    } \
} while (0)
```



#### **① `CHECK(x)`：接收一个待检查的表达式**

例如：

```c
CHECK(status == CO_OK);
```

这里的 `x` 就是：

```c
status == CO_OK
```



#### **② `if (!(x))`：条件不成立才进入错误处理**

```
status == CO_OK
    ├── 真 → 检查通过，继续执行后面的测试
    └── 假 → 打印错误，提前返回
```



#### **③ `fprintf(...)`：告诉你哪一行检查失败**

```
fprintf(stderr, "%s:%d: %s\n",
        __FILE__, __LINE__, #x);
```

| 部分       | 含义               |
| ---------- | ------------------ |
| `stderr`   | 标准错误输出流     |
| `__FILE__` | 使用宏的源文件名   |
| `__LINE__` | 使用宏的源代码行号 |
| `#x`       | 把表达式变成字符串 |

假设第 50 行写了：

```
CHECK(status == CO_OK);
```

失败时会打印类似：

```c
test_co_device_od.c:50: status == CO_OK
```





## 2：read_at()

```c
static uint32_t read_at(co_device_od_t *d, uint16_t index, uint8_t sub)
{
    const co_od_entry_t *e = NULL;
    uint8_t bytes[4] = {0}; /* 最多4字节，容纳当前支持的所有类型 */
    uint32_t result = 0;
    uint8_t i;
    /* ||短路：找不到时不调用co_od_read，也不访问空指针e->length。 */
    if (co_od_find(&d->table,index,sub,&e) != CO_OK ||
        co_od_read(e,bytes,e->length) != CO_OK) return UINT32_MAX;
    for (i=0;i<e->length;++i) result |= (uint32_t)bytes[i] << (8u*i);
    return result;
}
```

这个 `read_at()` 是测试辅助函数：根据对象地址查找对象、读取数据，再把字节组合成整数返回。

它让测试可以直接这样写：

```
CHECK(read_at(&device, 0x1017, 0) == 1000);
```

意思是：读取心跳周期对象，检查它是否为默认值 1000。

注意：这次返回的是**对象数值**，不再是 `co_status_t` 状态码。



#### **① 三个参数**

```c
static uint32_t read_at(
    co_device_od_t *d,
    uint16_t index,
    uint8_t sub
)
d     → 设备对象地址
index → 要查找的对象索引
sub   → 要查找的子索引

read_at → 可以理解为“读取指定地址处的对象值”
```

`static` 表示这个辅助函数只在当前测试 `.c` 中直接使用。



#### **② 准备条目指针和接收数组**

```
const co_od_entry_t *e = NULL;
```

创建指针变量 `e`，用来接收查找到的条目地址。



创建四字节数组，用于接收 `co_od_read()` 读出的数据。当前支持的数据类型unsigned 32_t 最多占四字节。

```c
uint8_t bytes[4] = {0};
```

```c
uint32_t result = 0;
uint8_t i;
result → 保存最终组合出来的整数
i      → 循环下标
```



#### **③ 先找对象，再读对象**

```c
if (co_od_find(&d->table, index, sub, &e) != CO_OK ||
    co_od_read(e, bytes, e->length) != CO_OK)
{
    return UINT32_MAX;
}
```

这段可以拆成下面的执行顺序：

```
co_od_find(&d->table, index, sub, &e)
        │
        ├── 查找失败 → 返回 UINT32_MAX，不再读取
        │
        └── 查找成功，e 已指向对应条目
                         ↓
                co_od_read(e, bytes, e->length)
                         │
                         ├── 读取失败 → 返回 UINT32_MAX
                         └── 读取成功 → 继续组合数值
```

其中：

```
&e
```

传入的是**指针变量 `e` 本身的地址**，让 `co_od_find()` 能把查找结果写进 `e`。

查找成功后：

```
e ──► 找到的对象条目
       └── length：这个对象需要读取的字节数
```

`||` 的短路规则保证：查找失败时，不执行第二个函数，也不会访问 `e->length`。



#### **④ 把读取的字节组合成整数**

```c
for (i = 0; i < e->length; ++i) {
    result |= (uint32_t)bytes[i] << (8u * i);
}
```

你之前已经见过这段小端解码逻辑：

```c
bytes[0] → 放入整数的低8位
bytes[1] → 左移8位后合入
bytes[2] → 左移16位后合入
bytes[3] → 左移24位后合入
```

只处理 `e->length` 个有效字节，并不是每次都处理四字节。



#### **⑤ 完整实例：读取默认心跳周期**

```c
co_device_od_t device = {0};

co_status_t status =
    co_device_od_init(&device, 1, NULL);

if (status != CO_OK) {
    return 1;
}

uint32_t period = read_at(&device, 0x1017, 0);	//索引0x1017，子索引0
```

函数内部过程：

```c
查找 0x1017:00
       ↓
e 指向心跳周期条目，e->length = 2
       ↓
co_od_read() 调用已绑定的 read_value()
       ↓
读取当前值1000，按小端写入 bytes[]
```

因为：

```
1000 = 0x03E8
```

所以读取后：

```
bytes[0] = 0xE8
bytes[1] = 0x03
bytes[2] = 0x00
bytes[3] = 0x00
```

再组合：

```
i = 0：result = 0x000000E8
i = 1：result = 0x000003E8
                    ↓
               十进制1000
```

最后：

```
return result;
```

调用者得到：

```
period = 1000;
```



#### **⑥ 为什么读出后又组合回整数？**

因为通用对象读取接口输出的是**字节数组**，方便后续 SDO/PDO 服务组装报文；测试则希望直接比较数值：

```c
对象当前值
    ↓ read_value() 编码
bytes[] 小端字节
    ↓ read_at() 解码
整数 result
    ↓
CHECK(result == 预期值)
```

这也实际走了一遍对象查找、读取和字节编码过程。

最后注意两个测试辅助函数的边界：

- `UINT32_MAX` 是 `0xFFFFFFFF`，这里用作失败标记，但它也可能是合法对象值，因此不能通用地区分所有成功与失败情况。
- 它按无符号位模式组合字节，不做有符号数扩展；当前 AI 值为非负数，因此不影响这些读取比较。

这个函数就是把“查找 + 读取 + 组合”封装起来，方便后面的 `CHECK` 检查对象值。



#### ⑦总览

```c
输入 index + subindex
        │
        ▼
co_od_find(&d->table, index, sub, &e)
        │
        ▼
找到对应对象条目 e
        │
        ▼
co_od_read(e, bytes, e->length)
        │
        ▼
得到小端字节数组 bytes[]
        │
        ▼
read_at() 按小端顺序拼接
        │
        ▼
存入 uint32_t result
        │
        ▼
CHECK(read_at(...) == 预期值)
```

例如：

```
CHECK(read_at(&device, 0x1017u, 0u) == 1000u);
```

它验证：

```c
0x1017:00
    → 找到心跳周期对象
    → 读出两个字节
    → 拼接成整数1000
    → 与1000比较
```

需要补充：拼接失败时，`read_at()` 返回 `UINT32_MAX`；所以测试辅助函数用这个值表示“读取失败”，而正常对象值应避免与它混淆。







## 3：write_at（）

```c
static co_status_t write_at(co_device_od_t *d, uint16_t index, uint8_t sub,
                            co_nmt_state_t state, uint32_t value, uint8_t length)
{
    const co_od_entry_t *e = NULL;
    uint8_t bytes[4];
    uint8_t i;
    co_status_t status = co_od_find(&d->table,index,sub,&e);
    if (status != CO_OK) return status;
    for (i=0;i<4;++i) bytes[i]=(uint8_t)(value >> (8u*i));
    return co_od_write(e,state,bytes,length);
}
```

作用：根据 `Index + Sub-index` 找到对象，把一个整数拆成小端字节，然后调用 `co_od_write()` 写入对象



#### 1. 参数含义

```c
d       → 设备对象表地址
index   → 对象索引，例如 0x6200
sub     → 子索引，例如 0x01
state   → 当前 NMT 状态
value   → 准备写入的整数值
length  → 实际写入的数据长度
```

例如：

```c
write_at(
    &device,
    0x6200u,
    0x01u,
    CO_NMT_OPERATIONAL,
    5u,
    1u
);
```

意思是：

```c
向 0x6200:01 写入数值 5
当前节点状态是 Operational
写入长度为 1 字节
```



#### 2. 先查找对象条目

```
const co_od_entry_t *e = NULL;
```

`e` 是一个对象条目指针，初始为空，用来接收查找结果。

```c
co_status_t status =
    co_od_find(&d->table, index, sub, &e);
```

实际过程：

```c
d->table
    │
    ▼
co_od_find()
    │
    ├── 根据 index + sub 查找
    └── 将条目地址写入 e
```

查找成功后：

```
e ───► 对应的 co_od_entry_t 条目
```

如果查找失败：

```
if (status != CO_OK)
    return status;
```

函数立即返回错误码，不再进行后续写入。



#### 3. 把整数拆成小端字节

```c
for (i = 0; i < 4; ++i)
    bytes[i] = (uint8_t)(value >> (8u * i));
```

假设：

```
value = 0x12345678;
```

拆分结果：

```
i = 0 → bytes[0] = 0x78
i = 1 → bytes[1] = 0x56
i = 2 → bytes[2] = 0x34
i = 3 → bytes[3] = 0x12
```

得到：

```
bytes = {0x78, 0x56, 0x34, 0x12}
```

这就是小端顺序：

```
低字节先放，高字节后放
```

如果：

```
value = 5u;
```

则：

```
bytes[0] = 0x05
bytes[1] = 0x00
bytes[2] = 0x00
bytes[3] = 0x00
```



#### 4. 调用正式写入函数

```
return co_od_write(e, state, bytes, length);
```

这句才真正把测试辅助函数连接到正式对象写入逻辑。

例如：

```
write_at(
    &device,
    0x6200u,
    0x01u,
    CO_NMT_OPERATIONAL,
    5u,
    1u
);
```

最后等价于：

```
co_od_write(
    找到的 0x6200:01 条目,
    CO_NMT_OPERATIONAL,
    bytes,
    1u
);
```

调用链是：

```
write_at(&device, 0x6200, 0x01, ..., 5, 1)
    │
    ├── co_od_find()
    │       └── 找到 0x6200:01 条目
    │
    ├── value=5 拆成 bytes[0]=0x05
    │
    └── co_od_write()
            ├── 检查长度
            ├── 检查是否可写
            ├── 检查 NMT 状态
            ├── 检查数值范围
            └── 调用 write_value()
                    └── 修改 fields[i].value
```



#### 5. 和 `read_at()` 对比

```c
read_at()
    index + sub
        ↓
    找到条目
        ↓
    co_od_read()
        ↓
    得到 bytes[]
        ↓
    拼接成 uint32_t
        ↓
    返回数值
   write_at()
    index + sub
        ↓
    找到条目
        ↓
    uint32_t value
        ↓
    拆成 bytes[]
        ↓
    co_od_write()
        ↓
    更新对象当前值
```

可以记成：

```
read_at  ：对象 → 字节数组 → 整数
write_at ：整数 → 字节数组 → 对象
```

这里 `write_at()` 只是测试辅助函数，真正负责权限、长度、状态和值范围检查的仍然是：

```
co_od_write()
```

另外，`bytes[4]` 的四个字节都会被填充，但最终只会把传入的 `length` 个字节交给 `co_od_write()`。例如 `length = 1` 时，正式函数只使用 `bytes[0]`。



#### 6.总览

##### 1：

这里的“两个对象”其实指不同层次，容易混淆。

```c
1. 找到对象条目
   co_od_find() 找到的是 co_od_entry_t

2. 写入对象数据
   co_od_write() 最终修改的是 fields[i].value
```

以 DO `0x6200:01` 为例：

```c
device.entries[11]                 device.fields[11]
类型：co_od_entry_t                类型：co_device_value_t
┌──────────────────────┐           ┌────────────────┐
│ index = 0x6200       │           │ value = 0      │
│ subindex = 0x01      │           │ writes = 0     │
│ access = 可读写       │           └────────────────┘
│ length = 1            │                   ▲
│ write ─────► write_value()                │
│ user ──────►──────────────────────────────┘
└──────────────────────┘
```

所以：

```c
co_od_find(&d->table, 0x6200, 0x01, &e);
```

找到的是：

```
e ──► device.entries[11]
```

它是**对象条目**，保存这个对象的地址、类型、权限、长度、回调等规则。

随后：

```
co_od_write(e, state, bytes, length);
```

`co_od_write()` 根据条目检查：

```c
是否可写？
长度是否正确？
数值是否越界？
当前状态是否允许？
```

检查通过后调用：

```
e->write(e->user, bytes, length);
```

实际等价于：

```c
write_value(&device->fields[11], bytes, length);
```

这时真正被修改的是：

```
device->fields[11].value
device->fields[11].writes
```

因此原句更准确地说是：

> 根据 `Index + Sub-index` 找到对应的 **对象条目 `co_od_entry_t`**，把整数拆成小端字节，再通过这个条目绑定的写回调，更新该对象对应的 **当前数据 `fields[i].value`**。

`write_at()` 的流程：

```c
Index + Sub-index
        ↓
找到 entries[i] 条目
        ↓
整数拆成 bytes[]
        ↓
co_od_write(entries[i], ...)
        ↓
write_value(&fields[i], ...)
        ↓
修改 fields[i].value
```

以后看到“对象”时，要看上下文：

```
对象条目 → co_od_entry_t，描述规则和访问方法
对象数据 → fields[i]，保存对象当前值
CANopen 对象 → 两者组合起来，对外表现为一个 0x6200:01 对象
```



##### 2：

`write_at()` 里的 `value` 就是测试代码准备的待写入整数。

例如：

```c
write_at(
    &device,
    0x6200u,
    0x01u,
    CO_NMT_OPERATIONAL,
    5u,
    1u
);
```

这里：

```
value = 5
```

是测试代码准备的值，表示准备向 `0x6200:01` 写入 `5`。

函数内部先把它转换成小端字节：

```
value = 5
bytes[0] = 0x05
```

然后调用：

```
co_od_write(e, state, bytes, length);
```

正式写入流程是：

```c
测试准备 value = 5
        ↓
write_at() 拆成 bytes[0] = 0x05
        ↓
co_od_write() 检查权限、长度、状态、范围
        ↓
write_value()
        ↓
device.fields[11].value = 5
```

所以要区分：

```
value = 测试代码准备的待写入整数
bytes[] = 根据 value 拆出的输入字节
fields[i].value = 检查通过后真正保存的对象当前值
```

在未来真实 SDO 流程中，`write_at()` 的 `value` 由测试代码提供；而真实写入时，数据会来自上位机的 CAN 报文。





## 4：static int test_device(void)



#### 1、函数头

```c
static int test_device(void)
```

表示：

```c
static → 只在当前测试文件使用
int    → 返回测试结果
test_device → 测试整张实际对象表
void   → 不需要调用者传入参数
```

测试约定：

```c
return 0 → 全部 CHECK 通过
return 1 → 某个 CHECK 失败
```



#### 2、创建测试变量

```c
    co_device_od_t d = {0}, other = {0};
    const co_od_entry_t *e = NULL;
    co_device_identity_t identity = {123,456,789,321}; /* 测试数据，不代表真实注册身份 */
    size_t i,j;
    uint8_t output=99; /* 故意不同于默认DO，确认接口确实填写此变量 */
    uint16_t period=0;
    uint32_t writes=0;


//1：
co_device_od_t d = {0}, other = {0}; 这一句创建了两个独立的设备对象字典：
d
 ├── fields[35]
 ├── entries[35]
 └── table

other
 ├── fields[35]
 ├── entries[35]
 └── table
 `{0}` 表示两台设备一开始都全部清零，**此时还没有完成对象字典初始化**。
   
 //2：创建一个对象条目指针
 const co_od_entry_t *e = NULL; 用来接收查找到的条目地址。
     
 //3创建一组测试身份信息
 co_device_identity_t identity = {123, 456, 789, 321};
    vendor_id    = 123
    product_code = 456
    revision     = 789
    serial       = 321
  后面初始化 `other` 时使用，验证自定义身份能否正确进入 `other`，并且不会影响 `d`
        
 //4创建两个循环下标：
 size_t i, j;
 i → 遍历对象或 TPDO
 j → 遍历子索引或比较对象
     
//5用于接收 DO 输出命令
uint8_t output = 99;
故意设成 `99`，因为设备初始化后的 DO 默认值应该是 `0`。这样可以确认函数确实修改了调用者变量，而不是“变量碰巧原来就是正确值”。
    
//6
uint16_t period = 0;
uint32_t writes = 0;

用于接收：
period → 心跳周期
writes → 心跳周期对象被成功写入的次数
```



------



#### 3：第 1 组（测试：无效参数和初始化）

3.1

```c
CHECK(co_device_od_init(NULL, 1, NULL) == CO_ERR_ARGUMENT);
```

故意把第一个参数传成 `NULL`：

```c
没有提供设备对象device的地址
        ↓
初始化函数不能写入对象表
        ↓
应该返回 CO_ERR_ARGUMENT
```



3.2：错误输入是否被正确拒绝

清零不等于已初始化： 未初始化而是清0就直接调用 co_device_od_get_outputs ( )

------

```c
CHECK(co_device_od_get_outputs(&d, &output) == CO_ERR_ARGUMENT);
```

此时：

```
d = {0};
```

虽然 `d` 已经创建了，但还没有调用：

```
co_device_od_init(&d, 1, NULL);
```

所以：

```
d 存在
但 d.table.entries 还是 NULL
d.table.count 还是 0
```

`ready(&d)` 失败，因此读取 DO 输出应该返回：

```
CO_ERR_ARGUMENT
```

这验证了：

> 清零创建一个结构体，不等于对象字典已经初始化。

------



3.3

随后正式初始化 `d`：

```c
CHECK(co_device_od_init(&d, 1, NULL) == CO_OK);
```

```
&d  → 初始化哪一台设备
1   → Node-ID 为1
NULL → 使用默认身份
```

```c
成功后，d 内部的三部分都已经建立好了：
d
├── fields[35]   35 个当前数据结构体
├── entries[35]  35 个对象条目结构体
└── table         对象表描述结构体
其中最关键的是 entries[35]。初始化函数循环读取：
definitions[i]
然后把模板中的字段逐项填入：
d.entries[i].index
d.entries[i].subindex
d.entries[i].type
d.entries[i].length
d.entries[i].access
d.entries[i].min_value
d.entries[i].max_value
d.entries[i].write_preop_only
同时补上模板没有提供的三项：
d.entries[i].read = read_value;
d.entries[i].write = write_value 或 NULL;
d.entries[i].user = &d.fields[i];
例如 i = 11 时：
definitions[11]
        ↓
d.entries[11]  → 0x6200:01 的实际对象条目
d.fields[11]   → 0x6200:01 的当前数据
d.entries[11] 大致会变成：
index    = 0x6200
subindex = 1
type     = CO_OD_UNSIGNED8
length   = 1
access   = CO_OD_READ_WRITE
范围     = 0～15
read     → read_value
write    → write_value
user     → &d.fields[11]
d.fields[11] 则是：
value  = 0
writes = 0
最后建立表：
d.table.entries = d.entries;
d.table.count = 35;
所以：
d.table.entries ──► d.entries[0]
d.table.count   = 35
需要注意：不是把整个 definitions[] 数组直接复制进 entries[]，因为两个结构体类型不同，成员也不完全相同。实际是初始化函数把模板中的规则成员逐项转换、填写到 entries[]，再补上回调和 user 地址。
```

应该返回：

```
CO_OK
```

------



3.4

```c
CHECK(d.table.count == 35);
```

确认初始化之后：对象表记录了 35 个对象条目

```
d.table.count = CO_DEVICE_OD_COUNT = 35
```



3.5分别测试两个非法节点号：

------

```c
CHECK(co_device_od_init(&d, 0, NULL) == CO_ERR_NODE_ID);	//0   → 不允许作为本节点编号
CHECK(co_device_od_init(&d, 128, NULL) == CO_ERR_NODE_ID);  //128 → 超出最大节点号127

```

因此都必须返回：

```
CO_ERR_NODE_ID
```



3.6 函数在检查失败前不应该修改原配置。

------

```c
CHECK(read_at(&d, 0x1400, 1) == 0x201);
```

这一步验证失败初始化没有破坏原来的对象配置。

第一次成功初始化时，RPDO1 ：它对应一组通信和映射参数：

```c
fields[15] → 0x1400:00，RPDO1 最大子索引
fields[16] → 0x1400:01，RPDO1 COB-ID，节点1为 0x201
fields[17] → 0x1400:02，RPDO1 传输类型
fields[18] → 0x1600:00，映射数量
fields[19] → 0x1600:01，映射描述
RPDO1 配置参数 → fields[15]～fields[19]
RPDO1 实际接收的数据 → fields[11]
```

得到：

```
0x1400:01
    └── RPDO1 COB-ID = 0x200 + 1 = 0x201
```

之后虽然尝试了节点号 `0` 和 `128`，但它们都是非法的，所以 `d` 仍然保持：

```
0x1400:01 = 0x201
```

这一组整体验证的是：

```c
创建 d
    ↓
未初始化时不能使用
    ↓
合法初始化成功
    ↓
非法节点号被拒绝
    ↓
失败初始化不破坏原配置
```

可以画成：

```c
d = {0}
    │
    ├── get_outputs() → CO_ERR_ARGUMENT
    │
    ├── init(node_id=1) → CO_OK
    │
    ├── init(node_id=0) → CO_ERR_NODE_ID
    ├── init(node_id=128) → CO_ERR_NODE_ID
    │
    └── 原来的 0x1400:01 仍然是 0x201
```





#### 4：第 2 组（测试：基础对象、设备身份、固定 PDO 的默认配置是否正确）

```c
    /* 2. 基础对象和固定映射：核对开发身份、COB-ID标志、映射及默认周期。 */
    CHECK(read_at(&d,0x1000,0)==0x70191);
    CHECK(read_at(&d,0x1018,0)==4 && read_at(&d,0x1018,1)==0);
    CHECK(read_at(&d,0x1018,2)==1 && read_at(&d,0x1018,3)==0x10000);
    CHECK(read_at(&d,0x1018,4)==0);
    CHECK(read_at(&d,0x1800,1)==0x40000181 && read_at(&d,0x1801,1)==0x40000281);
    CHECK(read_at(&d,0x1600,1)==0x62000108 && read_at(&d,0x1A00,1)==0x60000108);
    CHECK(read_at(&d,0x1A01,1)==0x64010110 && read_at(&d,0x1A01,2)==0x64010210);
    CHECK(read_at(&d,0x1800,0)==5 && read_at(&d,0x1801,5)==100);
    CHECK(co_od_find(&d.table,0x1800,4,&e)==CO_ERR_OD_NOT_FOUND);
    CHECK(co_od_find(&d.table,0x2000,0,&e)==CO_ERR_OD_NOT_FOUND);
```



```c
CHECK(read_at(&d, 0x1000, 0) == 0x70191);
读取：0x1000:00 → Device Type
期望值：0x00070191
表示当前项目采用的设备类型标识。这里先按开发版约定使用，规范原文复核仍待完成。
```



------

```c
CHECK(read_at(&d, 0x1018, 0) == 4 && read_at(&d, 0x1018, 1) == 0);
同时检查两个对象：
0x1018:00 → Identity 子对象数量 = 4
0x1018:01 → Vendor-ID = 0
`Vendor-ID = 0` 是开发占位值，不是正式注册厂商编号。
```



------

```c
CHECK(read_at(&d, 0x1018, 2) == 1 && read_at(&d, 0x1018, 3) == 0x10000);
检查：
0x1018:02 → Product code = 1
0x1018:03 → Revision = 0x10000
```



------

```c
CHECK(read_at(&d, 0x1018, 4) == 0);
检查：
0x1018:04 → Serial number = 0
当前也是开发占位值，因为还未确定
 0x1018
 ├── :00 → 有4个身份子对象
 ├── :01 → Vendor-ID
 ├── :02 → Product code
 ├── :03 → Revision
 └── :04 → Serial number
```



```c
CHECK(read_at(&d, 0x1800, 1) == 0x40000181 &&read_at(&d, 0x1801, 1) == 0x40000281);
检查两个 TPDO 的 COB-ID：
0x1800:01 → TPDO1 COB-ID = 0x40000181
0x1801:01 → TPDO2 COB-ID = 0x40000281
其中：
0x181 = 0x180 + Node-ID
0x281 = 0x280 + Node-ID
高位的 `0x40000000` 是配置标志，表示禁止 RTR。真正发送 CAN 帧时使用实际 CAN-ID：
TPDO1 → 0x181
TPDO2 → 0x281
所以要区分：
测试对象中的 COB-ID 配置值 → 0x40000181
实际 CAN 帧 ID         → 0x181
```





```c
CHECK(read_at(&d, 0x1600, 1) == 0x62000108 && read_at(&d, 0x1A00, 1) == 0x60000108);
检查固定 PDO 映射：
0x1600:01 → RPDO1 映射 0x6200:01，长度8 bit
0x1A00:01 → TPDO1 映射 0x6000:01，长度8 bit
拆开 `0x62000108`：
0x6200 → 对象索引
0x01   → 子索引
0x08   → 8 bit，也就是1字节
因此：
RPDO1 → 接收 DO 命令
TPDO1 → 发送 DI 状态
```



```c
CHECK(read_at(&d, 0x1A01, 1) == 0x64010110 &&  read_at(&d, 0x1A01, 2) == 0x64010210);
检查 TPDO2 的两个映射： 
0x1A01:01 → 0x6401:01，16 bit
0x1A01:02 → 0x6401:02，16 bit
所以：
 TPDO2
 ├── AI1：2字节
 └── AI2：2字节
总长度 = 4字节  

```



完整固定映射：

```c
RPDO1：0x201 （can-ID）
       └── 0x6200:01，8 bit	    //输出数字量

TPDO1：0x181（can-ID）
       └── 0x6000:01，8 bit		//输出数字量

TPDO2：0x281（can-ID）
       ├── 0x6401:01，16 bit		//模拟量1
       └── 0x6401:02，16 bit		//模拟量2
```

------



````c
CHECK(read_at(&d, 0x1800, 0) == 5 &&read_at(&d, 0x1801, 5) == 100);
检查 PDO 通信参数：
0x1800:00 → TPDO1 最大子索引 = 5
0x1801:05 → TPDO2 事件定时器 = 100 ms
注意 `0x1801` 是稀疏 子索引：
存在：00、01、02、03、05
缺少：04
```
`SubNumber = 5` 表示最大子索引是 5，不表示实际存在 `:00` 到 `:05` 共 6 个条目。
````

`SubNumber = 5` 表示最大子索引是 5，不表示实际存在 `:00` 到 `:05` 共 6 个条目。

------



```c
CHECK(co_od_find(&d.table, 0x1800, 4, &e)== CO_ERR_OD_NOT_FOUND);
故意查找未实现的：  0x1800:04     
因为这个子索引保留、不实现，所以必须返回：CO_ERR_OD_NOT_FOUND
```



------

```c
CHECK(co_od_find(&d.table, 0x2000, 0, &e)== CO_ERR_OD_NOT_FOUND);
故意查找预留的厂商扩展对象： 0x2000:00
当前项目没有定义它，因此也必须返回： CO_ERR_OD_NOT_FOUND
```



第二组整体流程：

```c
读取 Device Type
    ↓
读取 Identity 身份对象
    ↓
读取 TPDO COB-ID
    ↓
读取 RPDO/TPDO 固定映射
    ↓
读取 PDO 默认定时参数
    ↓
确认保留对象和未实现对象查找失败
```

这一组没有修改对象，只通过：

```
read_at()
```

读取对象并与预期值比较，验证初始化后实际对象表的默认配置是否正确。



#### 5：第3组 （测试：35 个对象条目是否都能正常读取、地址是否重复，以及只读对象是否真的拒绝写入。）

```c
    /* 3. 遍历所有条目：每个都能读；两两比较地址组合，排除重复；只读项拒写。 */
    for (i=0;i<d.table.count;++i) { /* 所有条目唯一，且都能成功读取。 */
        uint8_t data[4]={0};
        const co_od_entry_t *e = NULL;
        e=&d.entries[i];
        //co_status_t co_od_read(const co_od_entry_t *entry, uint8_t *data,uint8_t data_length)      
        CHECK(co_od_read(e,data,e->length)==CO_OK);
        for (j=i+1;j<d.table.count;++j)
            CHECK(e->index!=d.entries[j].index || e->subindex!=d.entries[j].subindex);
        if (e->access==CO_OD_READ_ONLY)
            CHECK(co_od_write(e,CO_NMT_PRE_OPERATIONAL,data,e->length)==CO_ERR_OD_READ_ONLY);
    }
```



① 外层循环遍历 35 个条目

```
for (i = 0; i < d.table.count; ++i)
```

初始化后：

```
d.table.count = 35;
```

所以 `i` 依次是：

```
0、1、2、...、34
```

每次处理一个：

```
d.entries[i]
```

------



② 准备读取缓冲区

```
uint8_t data[4] = {0};
```

当前支持的最大对象长度是 4 字节，所以准备 4 字节数组。

```
data[0]
data[1]
data[2]
data[3]
```

但每个对象实际读取多少字节，由：

```
e->length
```

决定。

例如：

```
U8  → 读取1字节
U16 → 读取2字节
U32 → 读取4字节
```



③ 直接取得第 i 条目的地址

```
e = &d.entries[i];
```

这里没有调用 `co_od_find()`，因为外层循环已经知道要访问第 `i` 个条目。

```
取得的是第i 个**对象条目地址，e = &d.entries[i]
```

------



④ 检查每个条目都能读取

```c
co_status_t co_od_read(const co_od_entry_t *entry, uint8_t *data, uint8_t data_length)                  
CHECK(co_od_read(e, data, e->length) == CO_OK);
```

对当前条目调用正式读取函数。

在co_od_read( )函数中 它会检查：

```c
条目地址有效
data 地址有效
类型对应长度正确
条目长度正确
读回调不为空
```

通过后才会调用写read_value( )函数

```c
e->read ──► read_value()
e->user ──► d.fields[i]
```

然后把当前对象值写入 `data[]`。

这一步验证的是：

> 初始化后 35 个条目的读回调、长度和 `user` 地址都已经正确配置。



⑤ 检查对象地址不能重复

```
for (j = i + 1; j < d.table.count; ++j)
```

内层循环从第i 个条目的下一个（ j= i+1 ）开始比较。

例如：

```c
当前 i = 0
比较 entries[0] 和 entries[1]～entries[34]

当前 i = 1
比较 entries[1] 和 entries[2]～entries[34]
```

比较条件：

```c
e->index != d.entries[j].index ||  e->subindex != d.entries[j].subindex
```

它要求：

```
索引不同 或者 子索引不同
```

只有当两者同时相同时，才表示重复：

```
index 相同 && subindex 相同 → 重复，测试失败
```

这里使用 `||` 是因为只要有一个部分不同，就不是同一个对象。

对象地址必须由这两个字段共同决定：

```
0x6200:01
```

不能只看 `index`，因为：

```c
0x1018:01
0x1018:02
0x1018:03
0x1018:04
```

它们的 `index` 都是 `0x1018`，但子索引不同，所以是不同对象。



⑥ 只读对象必须拒绝写入

```c
if (e->access == CO_OD_READ_ONLY)
```

只对只读对象做写入测试。

```c
CHECK(co_od_write(
    e,
    CO_NMT_PRE_OPERATIONAL,
    data,
    e->length
) == CO_ERR_OD_READ_ONLY);
```

这里把刚刚读出的 `data` 又当成待写入数据。

但对象条目本身是：

```
CO_OD_READ_ONLY
```

所以 `co_od_write()` 应该在检查权限时直接返回：

```
CO_ERR_OD_READ_ONLY
```

并且：

```c
不会调用 write_value()
不会修改 fields[i].value
不会增加 fields[i].writes
```

这一步验证：

> 对象表中标记为只读的对象，确实不能通过对象字典写入。



整个第三组测试流程：

```c
i = 0 到 34，逐个取出 entries[i]
        │
        ▼
调用 co_od_read()
        │
        ├── 检查条目可读
        └── 验证读回调和 fields[i] 连接正常
        │
        ▼
与后面的所有条目比较
        │
        └── 确认 index + subindex 没有重复
        │
        ▼
如果是只读对象
        │
        └── 故意写入，确认返回 CO_ERR_OD_READ_ONLY
```

这一组主要验证三件事：

```c
1. 35 个实际条目都能成功读取
2. 35 个对象地址唯一
3. 只读对象不能被 co_od_write() 修改
```



#### 6：第4组（测试：DI、AI 能否正常更新；任意输入越界时，整组旧数据是否保持不变）

```c
    /* 4. DI/AI：先成功更新，再分别构造DI和两路AI越界；失败必须保留整组旧值。 */
    CHECK(co_device_od_update_inputs(&d,15,0x123,4095)==CO_OK);
    CHECK(read_at(&d,0x6000,1)==15 && read_at(&d,0x6401,1)==0x123);
    CHECK(read_at(&d,0x6401,2)==4095);
    CHECK(co_od_find(&d.table,0x6401,1,&e)==CO_OK && e->type==CO_OD_INTEGER16);
    CHECK(co_device_od_update_inputs(&d,16,0,0)==CO_ERR_OD_VALUE);
    CHECK(co_device_od_update_inputs(&d,0,4096,0)==CO_ERR_OD_VALUE);
    CHECK(co_device_od_update_inputs(&d,0,0,65535)==CO_ERR_OD_VALUE);
    CHECK(read_at(&d,0x6000,1)==15 && read_at(&d,0x6401,1)==0x123);
```



**① 先更新一组合法数据**

```c
CHECK(co_device_od_update_inputs(&d, 15, 0x123, 4095)== CO_OK);
传入：
DI  = 15     二进制1111，四路逻辑输入都有效
AI1 = 0x123  十进制291
AI2 = 4095   12位ADC的最大值
    
成功后：
d.fields[9].value  = 15
d.fields[13].value = 0x123
d.fields[14].value = 4095

这里只是提供模拟采样结果，没有操作真实 GPIO 或 ADC。
```



**② 通过对象读取接口，确认数据确实更新了**

```c
CHECK(read_at(&d, 0x6000, 1) == 15 && read_at(&d, 0x6401, 1) == 0x123);
CHECK(read_at(&d, 0x6401, 2) == 4095);    
对应关系：
0x6000:01 → DI  → 应读出15
0x6401:01 → AI1 → 应读出0x123
0x6401:02 → AI2 → 应读出4095
```



**③ 检查 AI1 条目的类型**

```c
CHECK(co_od_find(&d.table, 0x6401, 1, &e) == CO_OK && e->type == CO_OD_INTEGER16);
要求两件事同时成立：

找到 0x6401:01
       ↓
该条目的 type 是 CO_OD_INTEGER16

`&&` 会短路：如果查找失败，就不会继续访问 `e->type`。      
```



**④ 分别让 DI、AI1、AI2 越界**

```c
CHECK(co_device_od_update_inputs(&d, 16, 0, 0)== CO_ERR_OD_VALUE);   
DI = 16 → 超过低四位允许的最大值15
因此整次更新被拒绝。
    
CHECK(co_device_od_update_inputs(&d, 0, 4096, 0)== CO_ERR_OD_VALUE);     
AI1 = 4096 → 超过ADC最大值4095
因此整次更新被拒绝。
    
CHECK(co_device_od_update_inputs(&d, 0, 0, 65535)== CO_ERR_OD_VALUE);
AI2 = 65535 → 超过ADC最大值4095
因此整次更新被拒绝。
```

注意，其他参数虽然传了 `0`，**也不应该因此被更新为零**。



**⑤ 再读一遍，确认原来三个值都还在**

```c
CHECK(read_at(&d, 0x6000, 1) == 15 && read_at(&d, 0x6401, 1) == 0x123);
CHECK(read_at(&d, 0x6401, 2) == 4095);
期望仍然是：
DI  = 15
AI1 = 0x123
AI2 = 4095
这是因为正式函数的顺序是：
/* 先检查全部输入 */
if (di > 15 || ai1 > 4095 || ai2 > 4095)
    return CO_ERR_OD_VALUE;

/* 全部合法，才开始赋值 */
device->fields[DI_SLOT].value = di;
device->fields[AI1_SLOT].value = ai1;
device->fields[AI2_SLOT].value = ai2;
```



第四组的完整过程：

```c
合法更新：15、0x123、4095
              ↓
读回确认三项数据
              ↓
检查 AI1 的类型
              ↓
分别构造 DI、AI1、AI2 越界
              ↓
三次调用都返回数值错误
              ↓
再次读回，确认整组旧数据未被破坏
```

核心就是：先全部检查，再一起提交；不能失败时只更新了一部分。



#### 7：第5组（测试：DO 命令能否正常写入和取出，以及错误写入是否保留原值。）

```c
    CHECK(write_at(&d,0x6200,1,CO_NMT_OPERATIONAL,15,1)==CO_OK);
    CHECK(co_device_od_get_outputs(&d,&output)==CO_OK && output==15);
    CHECK(write_at(&d,0x6200,1,CO_NMT_OPERATIONAL,16,1)==CO_ERR_OD_VALUE);
    CHECK(write_at(&d,0x6200,1,CO_NMT_OPERATIONAL,0,2)==CO_ERR_OD_LENGTH);
    CHECK(read_at(&d,0x6200,1)==15);
```

对应对象是 `0x6200:01`，四路 DO 共用一个字节，合法范围为 `0～15`。



**① 正常写入 DO 值 `15`**

```c
CHECK(write_at(&d, 0x6200, 1, CO_NMT_OPERATIONAL, 15, 1) == CO_OK);
```

把实参拆开看：

```c
&d                  → 操作设备 d 的对象字典
0x6200, 1           → 找到 DO 对象 0x6200:01
CO_NMT_OPERATIONAL  → 本次写入时，节点处于运行状态
15                  → 准备写入的整数
1                   → 写入长度为 1 字节
```

`15` 的二进制是：

```c
0000 1111
     │││└── DO1 = 1
     ││└─── DO2 = 1
     │└──── DO3 = 1
     └───── DO4 = 1
static co_status_t write_at(co_device_od_t *d, uint16_t index, uint8_t sub, co_nmt_state_t state, uint32_t value, uint8_t length)
这个函数内部定义了一个     uint8_t bytes[4];                      

//第一步：write_at() 把整数放进字节数组。
bytes[0] = 15
bytes[1] = 0
bytes[2] = 0
bytes[3] = 0
这次传入的 length = 1，所以后续只使用 bytes[0]。
//第二步：write_value() 从字节数组还原整数。
next = 0;
next |= (uint32_t)data[0] << 0;  /* next 得到 15 */

cell->value = next;             /* 对象当前值变成 15 */
++cell->writes;
这里 cell 指向 d.fields[11]，所以相当于：
d.fields[11].value = 15;
d.fields[11].writes++;
注意，fields[11].value 是 uint32_t，占 32 位，保存后可以这样看：
d.fields[11].value：

00000000 00000000 00000000 00001111
                              ↑
                         低四位都是 1
```

实际执行过程：

```c
write_at(...)
    │
    ├── co_od_find() 找到 0x6200:01 条目
    ├── 把整数 15 拆成字节，bytes[0] = 15
    └── co_od_write() 检查权限、长度、范围等
            │
            └── write_value() 更新对象存储
                    ├── d.fields[11].value = 15
                    └── d.fields[11].writes 加 1
```

成功返回 `CO_OK`，断言通过。



**② 取出 DO 命令，确认是否写进去了**

```c
CHECK(co_device_od_get_outputs(&d, &output) == CO_OK && output == 15);
```

你之前学过，这个函数内部的关键语句是：

```
*outputs = (uint8_t)device->fields[DO_SLOT].value;
```

这里相当于：

```
output = (uint8_t)d.fields[11].value;
```

所以调用后，原先设为 `99` 的 `output` 变成了 `15`。

这个断言同时检查：

- 函数返回 `CO_OK`。
- 调用者的变量 `output` 得到了 `15`。



**③ 写入越界值 `16`，应该被拒绝**

```c
CHECK(write_at(&d, 0x6200, 1, CO_NMT_OPERATIONAL, 16, 1) == CO_ERR_OD_VALUE);
```

为什么 `16` 不行？

```c
15 = 0000 1111  → 只使用低四位，合法
16 = 0001 0000  → 使用了 bit4，超出四路 DO 的范围
```

对象规定最大值为 `15`，所以 `co_od_write()` 返回 `CO_ERR_OD_VALUE`，**不会调用写回调，原来的 DO 值仍然是 `15`。**



**④ 数值合法，但长度错误，也应该被拒绝**

```c
CHECK(write_at(&d, 0x6200, 1, CO_NMT_OPERATIONAL, 0, 2) == CO_ERR_OD_LENGTH);
```

这次：

```c
准备写入的值：0       → 合法
指定写入长度：2 字节  → 不合法，DO 对象只占 1 字节
```

所以返回 `CO_ERR_OD_LENGTH`，不会把原值改成 `0`。



**⑤ 最后再读一次，确认错误写入没有破坏原值**

```c
CHECK(read_at(&d, 0x6200, 1) == 15);
```

整组数据变化就是：

```
DO 原值为 0
    │
    ├── 写 15，长度 1 → 成功，DO 变成 15
    ├── 取出 DO       → output 得到 15
    ├── 写 16，长度 1 → 越界，DO 保持 15
    ├── 写  0，长度 2 → 长度错误，DO 保持 15
    └── 再读取 DO     → 确认仍然是 15
```

目前测试只修改 RAM 中的 DO 命令。将来接入 STM32 后，应用层再根据 `output` 的低四位控制四路 LED。



#### 8：第6组（测试：心跳周期的默认值、允许写入的范围，以及写入次数是否正确累计。）

```c
    CHECK(co_device_od_get_heartbeat(&d,&period,&writes)==CO_OK && period==1000 && writes==0);
    CHECK(write_at(&d,0x1017,0,CO_NMT_PRE_OPERATIONAL,0,2)==CO_OK);
    CHECK(write_at(&d,0x1017,0,CO_NMT_OPERATIONAL,65535,2)==CO_OK);
    CHECK(write_at(&d,0x1017,0,CO_NMT_OPERATIONAL,65535,2)==CO_OK);
    CHECK(co_device_od_get_heartbeat(&d,&period,&writes)==CO_OK && period==65535 && writes==3);
    CHECK(write_at(&d,0x1017,0,CO_NMT_OPERATIONAL,1,1)==CO_ERR_OD_LENGTH);
```

这里操作的是 **`0x1017:00`——生产者心跳周期**，单位为毫秒，占 **2 字节**。



**① 读取初始化后的心跳配置**

```c
CHECK(co_device_od_get_heartbeat(&d, &period, &writes) == CO_OK&& period == 1000 && writes == 0);
```

这个函数把心跳对象中保存的两个值，读取后填写到调用者的变量中：

```
d.fields[HEARTBEAT_SLOT]       调用者的变量
    ├── value  = 1000  ─────► period = 1000
    └── writes = 0     ─────► writes = 0
```

检查的是：

- 函数执行成功。
- 默认周期为 `1000 ms`，即一秒。
- 初始化后还没有通过写回调修改过这个对象，所以写入次数为 `0`。

这里的 `writes` 是心跳配置被成功写入的次数，不是发送心跳报文的次数。 



**② 写入周期 `0`**

```
CHECK(write_at(&d, 0x1017, 0, CO_NMT_PRE_OPERATIONAL, 0, 2) == CO_OK);
```

参数含义：

```c
&d                      → 设备 d
0x1017, 0               → 心跳周期对象
CO_NMT_PRE_OPERATIONAL  → 本次按预运行状态进行写入检查
0                       → 要写入的周期为 0 ms
2                       → 数据长度为 2 字节
```

成功后：

```c
d.fields[HEARTBEAT_SLOT].value  = 0;
d.fields[HEARTBEAT_SLOT].writes = 1;
```

协议中，**周期为 `0` 表示关闭生产者心跳**。这里先测试配置能否保存，实际定时发送由后面的心跳模块处理。



**③ 在运行状态下，写入最大值 `65535`**

```c
CHECK(write_at(&d, 0x1017, 0, CO_NMT_OPERATIONAL, 65535, 2) == CO_OK);
```

`65535` 是无符号 16 位整数的最大值，也是这个对象允许的上限。

心跳周期对象**没有设置“仅预运行状态可写”**，所以本次在运行状态下写入也成功：

```
value  = 65535 ms
writes = 2
```



**④ 再写一次相同的值**

```c
CHECK(write_at(&d, 0x1017, 0, CO_NMT_OPERATIONAL, 65535, 2) == CO_OK);
```

虽然数值没有变化，但写回调仍然执行：

```
cell->value = next;
++cell->writes;
```

所以：

```
value  = 65535
writes = 3
```

**计数记录的是成功写入次数，不是数值发生变化的次数。**



**⑤ 读取配置，验证前三次写入的结果**

```
CHECK(co_device_od_get_heartbeat(&d, &period, &writes) == CO_OK
      && period == 65535 && writes == 3);
```

这次调用后：

```
period = 65535
writes = 3
```

注意：调用者的 `period` 和 `writes` 不会自动跟着对象变化，需要这次调用重新取出。



**⑥ 故意使用错误长度**

```
CHECK(write_at(&d, 0x1017, 0, CO_NMT_OPERATIONAL, 1, 1) == CO_ERR_OD_LENGTH);
```

这两个 `1` 分别表示：

```
倒数第二个 1 → 准备写入的周期为 1 ms，数值合法
最后一个   1 → 只提供 1 字节，长度错误
```

心跳周期对象要求 **2 字节**，即使数值 `1` 用一个字节就能表示，也必须按对象规定的长度写入。

因此写回调不会执行，存储仍保持：

```
value  = 65535
writes = 3
```

整组过程是：

```
初始化                 周期 = 1000，写入次数 = 0
   ↓
写入 0，长度 2         周期 = 0，   写入次数 = 1
   ↓
写入 65535，长度 2     周期 = 65535，写入次数 = 2
   ↓
再次写入 65535         周期 = 65535，写入次数 = 3
   ↓
写入 1，长度 1         长度错误，周期和计数都不变
```



#### 9：第7组（测试：两组 **TPDO** 的“抑制时间”和“事件周期”，是否只允许在预运行状态下修改；拒绝写入时，原值是否保持不变。**）

```c
    /* 7. 两组TPDO：i选择1800/1801，j选择03/05；仅Pre-operational允许写。 */
    for (i=0;i<2;++i) {
        uint8_t subs[2]={3,5}; /* 抑制时间和事件周期；04保留，不参加测试 */
        for (j=0;j<2;++j) {
            uint16_t index=(uint16_t)(0x1800+i);
            uint32_t old=read_at(&d,index,subs[j]); /* 保存写前值，用于验证失败不修改 */
            CHECK(write_at(&d,index,subs[j],CO_NMT_OPERATIONAL,9,2)==CO_ERR_OD_STATE);
            CHECK(write_at(&d,index,subs[j],CO_NMT_STOPPED,9,2)==CO_ERR_OD_STATE);
            CHECK(write_at(&d,index,subs[j],CO_NMT_INITIALIZATION,9,2)==CO_ERR_OD_STATE);
            CHECK(read_at(&d,index,subs[j])==old);
            CHECK(write_at(&d,index,subs[j],CO_NMT_PRE_OPERATIONAL,65535,2)==CO_OK);
            CHECK(read_at(&d,index,subs[j])==65535);
            CHECK(write_at(&d,index,subs[j],CO_NMT_PRE_OPERATIONAL,0,2)==CO_OK);
        }
    }
```

这里测试的是 PDO 配置对象的读写规则，还没有实际发送 PDO。



**① 先认清这四个对象**

| 对象地址    | 含义           | 数值单位 |
| ----------- | -------------- | -------- |
| `0x1800:03` | TPDO1 抑制时间 | 100 μs   |
| `0x1800:05` | TPDO1 事件周期 | ms       |
| `0x1801:03` | TPDO2 抑制时间 | 100 μs   |
| `0x1801:05` | TPDO2 事件周期 | ms       |

你可以先这样理解：

- **抑制时间**：限制同一个 TPDO 连续发送的最小间隔，避免发送太频繁。
- **事件周期**：配置定时触发发送的周期。

它们都占 **2 字节**，项目中都设置了“仅预运行状态可写”。



**② 两层循环，就是依次测试这四个对象**

外层：

```c
for (i = 0; i < 2; ++i)
```

配合：

```c
uint16_t index = (uint16_t)(0x1800 + i);
```

得到：

```c
i = 0 → index = 0x1800 → TPDO1
i = 1 → index = 0x1801 → TPDO2
```

内层：

```c
uint8_t subs[2] = {3, 5};

for (j = 0; j < 2; ++j)
```

得到：

```c
j = 0 → subs[j] = 3 → 抑制时间
j = 1 → subs[j] = 5 → 事件周期
```

所以循环顺序是：

```c
i = 0：TPDO1
    ├── j = 0：测试 0x1800:03
    └── j = 1：测试 0x1800:05

i = 1：TPDO2
    ├── j = 0：测试 0x1801:03
    └── j = 1：测试 0x1801:05
```

**内层那套断言，一共执行四遍，每遍换一个对象。**



**③ 保存这个对象原来的数值**

```c
uint32_t old = read_at(&d, index, subs[j]);
```

例如第一遍，实际就是：

```
uint32_t old = read_at(&d, 0x1800, 3);
```

把 TPDO1 抑制时间的当前值保存到局部变量 `old`，用于后面比较。



**④ 在三个不允许的状态下尝试写入**

```c
CHECK(write_at(&d, index, subs[j],
               CO_NMT_OPERATIONAL, 9, 2) == CO_ERR_OD_STATE);

CHECK(write_at(&d, index, subs[j],
               CO_NMT_STOPPED, 9, 2) == CO_ERR_OD_STATE);

CHECK(write_at(&d, index, subs[j],
               CO_NMT_INITIALIZATION, 9, 2) == CO_ERR_OD_STATE);
```

三次都尝试写入数值 `9`，长度为 `2` 字节，只有传入的状态不同：

| 传入状态                | 含义   | 预期结果 |
| ----------------------- | ------ | -------- |
| `CO_NMT_OPERATIONAL`    | 运行   | 拒绝写入 |
| `CO_NMT_STOPPED`        | 停止   | 拒绝写入 |
| `CO_NMT_INITIALIZATION` | 初始化 | 拒绝写入 |

这里**数值和长度都合法，故意让“状态”不符合要求**，因此预期返回 `CO_ERR_OD_STATE`。

注意：这里是把状态作为参数交给写函数检查，**并没有执行节点状态切换**。



**⑤ 确认三次失败没有改变原值**

```c
CHECK(read_at(&d, index, subs[j]) == old);
```

含义就是：

```
三次写入之前：对象值 = old
三次被拒绝后：对象值仍然 = old
```



**⑥ 在预运行状态下，写入最大值**

```c
CHECK(write_at(&d, index, subs[j],
               CO_NMT_PRE_OPERATIONAL, 65535, 2) == CO_OK);

CHECK(read_at(&d, index, subs[j]) == 65535);
```

这次状态符合要求，数值 `65535` 也在允许范围内：

```c
co_od_write() 检查通过
    ↓
write_value() 执行
    ↓
对应 fields 元素的 value = 65535
对应 fields 元素的 writes 加 1
    ↓
再读取，确认得到 65535
```



**⑦ 最后尝试写入下界 `0`**

```c
CHECK(write_at(&d, index, subs[j],
               CO_NMT_PRE_OPERATIONAL, 0, 2) == CO_OK);
```

确认在预运行状态下，写入 `0` 也能成功。注意这里检查了成功状态，但没有紧接着再次读取验证 `0`。

对于每个对象，测试过程都是：

```c
保存原值 old
    ↓
运行状态写 9   → 拒绝
停止状态写 9   → 拒绝
初始化状态写 9 → 拒绝
    ↓
读取确认仍为 old
    ↓
预运行写 65535 → 成功，并读回确认
    ↓
预运行写 0     → 成功
```



#### 10：第8组（测试：**应用能否更新错误寄存器，以及非法更新是否保留原值**）

```c
    CHECK(co_device_od_set_error(&d,0x11)==CO_OK);
    CHECK(co_device_od_set_error(&d,0x40)==CO_ERR_OD_VALUE);
    CHECK(read_at(&d,0x1001,0)==0x11);
```

错误寄存器对应对象 **`0x1001:00`**，占一个字节，不同的位表示不同类别的错误。



**① 写入合法错误值 `0x11`**

```c
CHECK(co_device_od_set_error(&d, 0x11) == CO_OK);
```

先看它的二进制：

```c
0x11 = 0001 0001
          ↑   ↑
        bit4 bit0

bit0 = 1 → 通用错误
bit4 = 1 → 通信错误
```

回顾函数体：

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

`0x11` 没有设置保留位 bit6，（bit6 是保留位，不能设置为 1。）所以检查通过，执行：

```
d.fields[ERROR_SLOT].value = 0x11;
```

返回 `CO_OK`，断言通过。



**② 尝试写入非法值 `0x40`**

```c
CHECK(co_device_od_set_error(&d, 0x40) == CO_ERR_OD_VALUE);
0x40 = 0100 0000
        ↑
       bit6
```

**bit6 是保留位，不能设置为 1。**

因此函数中的条件成立：

```
if ((error & 0x40u) != 0)
    return CO_ERR_OD_VALUE;
```

函数提前返回，没有执行后面的赋值，所以原值仍然是 `0x11`。



**③ 通过对象字典读回，确认原值没变**

```c
CHECK(read_at(&d, 0x1001, 0) == 0x11);
```

这次通过索引和子索引找到错误寄存器，读取它的值，确认失败更新没有破坏之前的数据。

```c
应用调用 set_error(&d, 0x11)
    └── 合法 → 错误寄存器保存 0x11
                    ↓
应用调用 set_error(&d, 0x40)
    └── 非法 → 拒绝，仍保存 0x11
                    ↓
read_at(&d, 0x1001, 0)
    └── 读到 0x11，测试通过
```

这里还有一个区别：**错误寄存器对主站是只读的，但 STM32 应用可以通过 `co_device_od_set_error()` 更新它。** 本次更新直接修改存储，不经过 `write_value()`，也不会增加写回调计数。



#### 11：第9组（测试：**再创建一份设备对象字典，使用节点号 `127` 和自定义身份，确认它与原来的设备 `d` 互不影响。**）

```c
    CHECK(co_device_od_init(&other,127,&identity)==CO_OK);
    CHECK(read_at(&other,0x1400,1)==0x27f && read_at(&other,0x1800,1)==0x400001ff);
    CHECK(read_at(&other,0x1801,1)==0x400002ff && read_at(&d,0x1400,1)==0x201);
    CHECK(read_at(&other,0x1018,1)==123 && read_at(&other,0x1018,4)==321);
    CHECK(read_at(&other,0x6200,1)==0 && read_at(&d,0x6200,1)==15);
```

先回顾测试开头创建的变量：

```c
co_device_od_t d = {0}, other = {0};
co_device_identity_t identity = {
    123,  /* vendor_id：厂商编号 */
    456,  /* product_code：产品代码 */
    789,  /* revision：版本号 */
    321   /* serial：序列号 */
};
```

`d` 和 `other` 是两个独立的结构体变量，各自拥有自己的存储：

```c
d                          other
├── fields[35]             ├── fields[35]
├── entries[35]            ├── entries[35]
└── table                  └── table
```



**① 初始化第二份对象字典**

```c
CHECK(co_device_od_init(&other, 127, &identity) == CO_OK);
```

三个实参分别是：

```
&other    → 初始化 other
127       → 使用最大合法节点号 127
&identity → 使用我们提供的身份信息
```

原先初始化 `d` 时传的是 `NULL`，使用默认开发身份；这里则使用 `123、456、789、321` 这组测试数据。



**② 检查节点号是否正确影响 PDO 的 COB-ID**

```c
CHECK(read_at(&other, 0x1400, 1) == 0x27f && read_at(&other, 0x1800, 1) == 0x400001ff);
```

节点号十进制 `127`，就是十六进制 `0x7F`。

因此：

```c
other 的 RPDO1：
0x200 + 0x7F = 0x27F

other 的 TPDO1：
0x40000180 + 0x7F = 0x400001FF
```

注意，TPDO 配置值中的 `0x40000000` 是 **禁止 RTR 的标志位**，实际 CAN-ID 是 `0x1FF`。

接下来：

```c
CHECK(read_at(&other, 0x1801, 1) == 0x400002ff&& read_at(&d, 0x1400, 1) == 0x201);
```

检查：

```
other 的 TPDO2：
0x40000280 + 0x7F = 0x400002FF
实际 CAN-ID 为 0x2FF

d 的 RPDO1：
仍然是 0x200 + 1 = 0x201
```

初始化 `other`，不能把 `d` 的配置也改掉。



**③ 检查自定义身份是否写入**

```c
CHECK(read_at(&other, 0x1018, 1) == 123 && read_at(&other, 0x1018, 4) == 321);
```

对应关系是：

| 对象        | 身份成员       | 本次设置 |
| ----------- | -------------- | -------- |
| `0x1018:01` | `vendor_id`    | 123      |
| `0x1018:02` | `product_code` | 456      |
| `0x1018:03` | `revision`     | 789      |
| `0x1018:04` | `serial`       | 321      |

这一句具体检查了厂商编号和序列号。



**④ 检查两份 DO 数据是否独立**

```c
CHECK(read_at(&other, 0x6200, 1) == 0&& read_at(&d, 0x6200, 1) == 15);
```

为什么预期值不同？

```c
other：刚完成初始化
    └── DO 默认值 = 0

d：第五组测试成功写入过 15
    └── DO 当前值 = 15
```

底层对应的是不同的结构体元素：

```c
other.fields[11].value = 0
d.fields[11].value     = 15
```

这也检查了对象条目的 `user` 是否连接到各自的存储：**读 `other` 就应该读到 `other.fields[]`，读 `d` 就应该读到 `d.fields[]`。**

这里是在 PC 内存中创建两份设备对象字典，用于验证多实例独立性，并没有真的接入两块 STM32。



#### 12:最后一组（测试：重新初始化能否恢复默认配置，以及函数能否正确拒绝空指针参数。**）

```c
    CHECK(co_device_od_init(&d,1,NULL)==CO_OK);
    CHECK(read_at(&d,0x6200,1)==0 && read_at(&d,0x1017,0)==1000);
    CHECK(co_device_od_get_outputs(&d,NULL)==CO_ERR_ARGUMENT);
    CHECK(co_device_od_get_heartbeat(&d,NULL,&writes)==CO_ERR_ARGUMENT);
    CHECK(co_device_od_get_heartbeat(&d,&period,NULL)==CO_ERR_ARGUMENT);
    CHECK(co_device_od_update_inputs(NULL,0,0,0)==CO_ERR_ARGUMENT);
    CHECK(co_device_od_set_error(NULL,0)==CO_ERR_ARGUMENT);
```



**① 重新初始化已经使用过的 `d`**

```c
CHECK(co_device_od_init(&d, 1, NULL) == CO_OK);
```

这里没有创建新变量，而是**对原来的 `d` 重新执行初始化**：

```
&d   → 原来的设备对象字典
1    → 节点号为 1
NULL → 使用默认开发身份
```

之前测试已经修改过 DO、心跳周期等数据。重新初始化后，会重新填写对象条目、恢复初值、清零写入计数，并建立表和存储之间的连接。



**② 检查 DO 和心跳周期是否恢复默认值**

```c
CHECK(read_at(&d, 0x6200, 1) == 0&& read_at(&d, 0x1017, 0) == 1000);
```

对比一下：

```
重新初始化前                 重新初始化后
DO = 15                 →   DO = 0
心跳周期 = 65535 ms      →   心跳周期 = 1000 ms
```

这句具体检查了两个对象，确认重新初始化确实恢复了它们的默认值。



**③ 获取 DO 时，没有提供输出变量地址**

```c
CHECK(co_device_od_get_outputs(&d, NULL) == CO_ERR_ARGUMENT);
```

正常调用应该是：

```
co_device_od_get_outputs(&d, &output);
```

函数通过 `&output` 把 DO 值写入调用者的变量。

现在传入 `NULL`，意味着没有提供可写入的位置，因此函数返回 `CO_ERR_ARGUMENT`，不会执行：

```
*outputs = ...;
```



**④ 获取心跳配置时，分别漏掉两个输出地址**

```c
CHECK(co_device_od_get_heartbeat(&d, NULL, &writes) == CO_ERR_ARGUMENT);
```

这里：

```
设备地址      &d       → 已提供
周期输出地址  NULL     → 缺失
次数输出地址  &writes  → 已提供
```

因此返回参数错误。

下一句：

```
CHECK(co_device_od_get_heartbeat(&d, &period, NULL) == CO_ERR_ARGUMENT);
```

这次反过来：

```
设备地址      &d       → 已提供
周期输出地址  &period  → 已提供
次数输出地址  NULL     → 缺失
```

同样返回参数错误。**这个接口要求两个输出地址都提供。**



**⑤ 更新输入、设置错误时，没有提供设备地址**

```c
//co_status_t co_device_od_update_inputs(co_device_od_t *device, uint8_t di,uint16_t ai1, uint16_t ai2)
                                       
CHECK(co_device_od_update_inputs(NULL, 0, 0, 0) == CO_ERR_ARGUMENT);
CHECK(co_device_od_set_error(NULL, 0) == CO_ERR_ARGUMENT);
```

第一句的 DI、AI 数值 `0` 都合法，第二句的错误值 `0` 也合法。

但两句的第一个参数都是 `NULL`：

```
没有设备地址
    ↓
ready(NULL) 返回 0
    ↓
函数返回 CO_ERR_ARGUMENT
    ↓
不访问设备成员，不修改数据
```



**⑥ 所有检查通过，测试函数返回 `0`**

```
return 0;
```

还记得 `CHECK` 失败时会执行：

```
return 1;
```

因此整个 `test_device()` 的结果是：

```c
任意一个 CHECK 失败 → 当场返回 1

所有 CHECK 都通过  → 最后返回 0
```

执行到这里，就表示 **`test_device()` 这十组检查全部通过了**。





## 5：signed_write ( )

```c
/* 用有符号边界验证通用写检查；回调只保存两个字节，不替代范围检查。 */
static co_status_t signed_write(void *user,const uint8_t *data,uint8_t length)
{
    (void)length; /* 本回调固定复制2字节；显式标记参数未用，避免编译警告 */
    memcpy(user,data,2); /* user是saved数组首地址；保存收到的字节，不自行解码 */
    return CO_OK;
}
```

这个 `signed_write()` 是**供后面有符号整数测试使用的写回调**。它的工作很简单：

把输入数组中的两个字节，复制到调用者准备的保存数组中，然后返回成功。



**① 三个形参分别是什么**

用一个具体例子看：

```c
uint8_t saved[2] = {0, 0};          /* 接收并保存数据 */
uint8_t negative[2] = {0, 0x80};   /* 准备写入的两个字节 */
```

为了理解函数，先看直接调用：

```c
co_status_t status = signed_write(saved, negative, 2);
```

数组名在这里转换为首元素地址，进入函数后：

```c
user   → saved[0] 的地址      → 数据复制到哪里
data   → negative[0] 的地址   → 数据从哪里来
length = 2                   → 传入的长度
```

`const uint8_t *data` 表示：函数通过 `data` 读取输入字节，不通过它修改输入内容。



**② `(void)length;` 是什么意思**

```
(void)length;
```

这句是显式表明：

> 我知道有 `length` 这个参数，但这个函数内部不使用它。

它通常用于避免“参数未使用”的编译警告。**它不会把 `length` 清零，也没有检查长度。**

为什么保留这个参数？因为对象字典的写回调规定了统一的函数形式：

```
co_status_t 函数名(void *user, const uint8_t *data, uint8_t length);
```

`signed_write()` 需要符合这个形式，才能绑定到条目的 `write` 成员上。

本测试只处理两个字节，因此下一句直接写了固定长度 `2`。



**③ `memcpy(user, data, 2);` 是核心**

`memcpy()` 是 `<string.h>` 提供的内存复制函数，参数顺序是：

```c
memcpy(目标地址, 来源地址, 复制的字节数);
```

所以本次相当于：

```
memcpy(saved, negative, 2);
```

对于这里的两个字节，可以理解为：

```c
saved[0] = negative[0];
saved[1] = negative[1];
```

数据变化：

```c
复制前：

negative： [ 0x00 ][ 0x80 ]
saved：    [ 0x00 ][ 0x00 ]

                  ↓ 复制两个字节

复制后：

negative： [ 0x00 ][ 0x80 ]   保持不变
saved：    [ 0x00 ][ 0x80 ]
```

这里不需要先把 `user` 转成结构体指针，因为 **`memcpy()` 本来就能接收 `void \*` 地址，按字节复制内存**。



**④ 它有没有把字节组合成负数？**

**没有。** 它只保存原始字节，不进行整数解码，也不检查数值范围。

`{0x00, 0x80}` 按小端顺序表示 `0x8000`，按照有符号 16 位整数解释就是 `-32768`。但负责这个解释和范围检查的是前面的 `co_od_write()`。

在后面的测试中，调用关系是：

```c
co_od_write(...)
    │
    ├── 检查长度、权限等
    ├── 按对象类型解释输入值
    ├── 检查数值范围
    │
    └── 检查通过，调用 signed_write(...)
            │
            ├── 把两个原始字节复制到 saved[]
            └── 返回 CO_OK
```

如果前面的检查失败，就不会执行这个回调，`saved[]` 保持原样。



**⑤ 最终结果和返回值分开看**

对于刚才的直接调用：

```c
co_status_t status = signed_write(saved, negative, 2);
```

执行后：

```c
saved[0] = 0x00;
saved[1] = 0x80;

status = CO_OK;
```

与之前学过的 `write_value()` 比较：

```
write_value()  → 把字节组合成整数，存入 cell->value，并增加写入计数

signed_write() → 直接把两个字节复制到 saved[]，供测试断言检查
```





## 6：test_signed()

目的就是：检查 `co_od_write()` 能否正确处理有符号 16 位整数，以及拒绝超范围或长度错误的写入。

它单独创建了一个测试条目 `e`，不使用设备 `d` 的那 35 个对象。



**① 准备三个数组**

```c
uint8_t saved[2] = {0};
uint8_t negative[2] = {0, 0x80};
uint8_t positive[2] = {0xff, 0x7f};
```

分别用于：

```
saved[]    → 保存写回调收到的字节，供断言检查
negative[] → 准备写入的最小负数
positive[] → 准备写入的最大正数
```

有符号 16 位整数的范围是 **`-32768～32767`**。这两个边界值的小端字节排列为：

| 数组       | 第 0 字节（低字节） | 第 1 字节（高字节） | 按 INTEGER16 解释   |
| ---------- | ------------------- | ------------------- | ------------------- |
| `negative` | `0x00`              | `0x80`              | `0x8000` → `-32768` |
| `positive` | `0xFF`              | `0x7F`              | `0x7FFF` → `32767`  |

数组本身只是保存字节，**由 `co_od_write()` 根据条目的 `CO_OD_INTEGER16` 类型，把它们解释为有符号整数。**



**② 创建一个临时测试条目**

```c
co_od_entry_t e = {
    0x2000, 0,
    CO_OD_INTEGER16,
    CO_OD_READ_WRITE,
    2,
    -32768, 32767,
    0,
    NULL, signed_write, saved
};
```

把这条初始化展开看：

```c
e
├── index            = 0x2000
├── subindex         = 0
├── type             = CO_OD_INTEGER16
├── access           = CO_OD_READ_WRITE
├── length           = 2
├── min_value        = -32768
├── max_value        = 32767
├── write_preop_only = 0
├── read             = NULL
├── write            = signed_write 的函数地址
└── user             = saved 数组首元素地址
```

这里仅测试写入，所以没有绑定读取回调。

`0x2000:00` 是临时测试使用的地址，**没有加入项目实际对象表**。后面直接传 `&e` 写入，不需要调用 `co_od_find()`。



**③ 写入最小值 `-32768`**

```c
CHECK(co_od_write(&e, CO_NMT_PRE_OPERATIONAL, negative, 2) == CO_OK);
CHECK(saved[1] == 0x80);
```

执行过程：

```
negative[] = {0x00, 0x80}
    ↓
co_od_write()
    ├── 长度 2，正确
    ├── 按 INTEGER16 解释为 -32768
    ├── 在允许范围 -32768～32767 内
    └── 调用 signed_write(saved, negative, 2)
                ↓
        saved[] = {0x00, 0x80}
```

第一句检查写入成功，第二句检查保存的高字节是 `0x80`。这里没有同时检查低字节。



**④ 写入最大值 `32767`**

```c
CHECK(co_od_write(&e, CO_NMT_PRE_OPERATIONAL, positive, 2) == CO_OK);
CHECK(saved[0] == 0xff && saved[1] == 0x7f);
```

同样在允许范围内，回调把新字节复制到 `saved[]`：

```c
原来：saved[] = {0x00, 0x80}
                    ↓
现在：saved[] = {0xFF, 0x7F}
```

断言检查两个字节都正确。



**⑤ 收紧这个条目允许的数值范围**

```
e.min_value = 0;
e.max_value = 4095;
```

这里只修改写入规则：

```
原来允许：-32768～32767
现在允许：     0～4095
```

类型仍是 `CO_OD_INTEGER16`，长度仍是 2 字节，`saved[]` 也不会因为修改规则而自动变化。



**⑥ 再写入刚才的两个边界值，这次都应该失败**

```c
CHECK(co_od_write(&e, CO_NMT_PRE_OPERATIONAL, negative, 2)
      == CO_ERR_OD_VALUE);
```

`negative[]` 表示 `-32768`，小于新下限 `0`，所以拒绝。

```c
CHECK(co_od_write(&e, CO_NMT_PRE_OPERATIONAL, positive, 2)
      == CO_ERR_OD_VALUE);
```

`positive[]` 表示 `32767`，大于新上限 `4095`，所以也拒绝。

这两次都会在 `co_od_write()` 的范围检查处返回，不调用 `signed_write()`。



**⑦ 再构造一次长度错误**

```c
CHECK(co_od_write(&e, CO_NMT_PRE_OPERATIONAL, positive, 1)
      == CO_ERR_OD_LENGTH);
```

对象要求 `2` 字节，这次传的是 `1`，因此在长度检查时就返回错误，不会继续执行数值范围检查和写回调。



**⑧ 检查保存内容，再返回测试结果**

```c
CHECK(saved[1] == 0x7f);
return 0;
```

按函数执行逻辑，三次失败都没有调用写回调，`saved[]` 应保持上次成功写入的内容：

```
saved[] = {0xFF, 0x7F}
```

不过这里的断言**只检查了高字节 `saved[1]`**，没有完整比较两个字节。

整组测试的过程是：

```
允许范围：-32768～32767
    ├── 写 -32768 → 成功，保存 00 80
    └── 写  32767 → 成功，保存 FF 7F

修改范围：0～4095
    ├── 写 -32768 → 数值越界，拒绝
    ├── 写  32767 → 数值越界，拒绝
    └── 长度传 1 → 长度错误，拒绝

最后检查保存的高字节仍为 7F
    ↓
所有断言通过，返回 0
```



#### 纠错：

我记得我们项目是规定用的无符号8和无符号16 和无符号32，这里测试有符号16位的原因和目的是什么呢？

你记得的是**前面最初那一版**：支持无符号 8、16、32 位。后来项目中 **AI 模拟量对象的类型改成了有符号 16 位 `CO_OD_INTEGER16`**，因此补了这组测试。

你之前第四组测试里已经见过这句：

```
CHECK(co_od_find(&d.table, 0x6401, 1, &e) == CO_OK
      && e->type == CO_OD_INTEGER16);
```

它就是确认 **AI1 对象的类型是有符号 16 位**。AI2 也使用这个类型。

**这里需要区分“类型能表示的范围”和“项目允许写入的范围”：**

| 内容                         | 范围            |
| ---------------------------- | --------------- |
| `INTEGER16` 类型能表示的数值 | `-32768～32767` |
| 本项目 ADC 采集值范围        | `0～4095`       |

所以，**对象使用有符号类型，不代表我们的 ADC 会采集出负数**。`0～4095` 完全可以放在有符号 16 位整数中。

这组测试分两步验证：

```
先允许 -32768～32767
    └── 确认 co_od_write() 能正确理解有符号数
        例如 00 80 要理解为 -32768，而不是 32768

再限制为 0～4095
    └── 确认类型合法，也仍然要遵守对象自己的数值范围
        -32768 和 32767 都必须拒绝
```

还有一个关键区别：**实际 AI 对象对主站是只读的**，采集值通过 `co_device_od_update_inputs()` 更新。因此，这里专门创建了一个临时的、可写的 `INTEGER16` 条目，来测试通用函数 `co_od_write()` 的有符号处理能力。





## 7：static int dump(unsigned node)

```c
static int dump(unsigned node)
{
    co_device_od_t d;
    size_t i;
    CHECK(node>=1 && node<=127);
    CHECK(co_device_od_init(&d,(uint8_t)node,NULL)==CO_OK);
    for(i=0;i<d.table.count;++i) {
        const co_od_entry_t *e=&d.entries[i];
        /* 内部枚举不是EDS类型号：INTEGER16=3，U8=5，U16=6，U32=7。 */
        unsigned type=e->type==CO_OD_INTEGER16 ? 3u :
                      e->type==CO_OD_UNSIGNED8 ? 5u : e->type==CO_OD_UNSIGNED16 ? 6u : 7u;
        /* PRIu32/PRId64由inttypes.h提供，与固定宽度整数匹配且便于跨平台。 */
        printf("%04X,%u,%u,%u,%u,%" PRIu32 ",%" PRId64 ",%" PRId64 "\n",
               e->index,e->subindex,type,e->length,(unsigned)e->access,
               read_at(&d,e->index,e->subindex),e->min_value,e->max_value);
    }
    return 0;
}
```

这个 `dump()` 的作用是：**初始化一份指定节点号的对象字典，再把所有对象的信息逐行打印出来，**

**供 Python 测试程序与 EDS 文件核对。**

`dump` 在这里可以理解为“导出数据”。它不会发送 CAN 报文

整体过程：

```c
传入节点号，例如 dump(1)
    ↓
初始化设备对象字典 d
    ↓
遍历 35 个对象条目
    ↓
每个对象打印一行：
索引、子索引、EDS类型号、长度、权限、当前值、最小值、最大值
    ↓
返回 0
```



**① 创建对象字典变量，检查节点号并初始化**

```c
co_device_od_t d;
size_t i;

CHECK(node >= 1 && node <= 127);
CHECK(co_device_od_init(&d, (uint8_t)node, NULL) == CO_OK);
```

形参：

```c
static int dump(unsigned node)中的unsigned node
```

就是一个无符号整数，用于接收节点号，例如：

```
dump(1);
```

这里 `d` 没有写 `= {0}`，因为接下来成功执行的 `co_device_od_init()` 会填写它需要的内容。**在初始化前，没有读取 `d` 的成员。**

`(uint8_t)node` 是把节点号转换为初始化函数要求的类型。前面已经确认它在 `1～127` 内，因此这个转换不会丢失数值。

最后一个参数 `NULL` 表示使用默认开发身份。



**② 遍历所有对象条目**

```c
for (i = 0; i < d.table.count; ++i) {
    const co_od_entry_t *e = &d.entries[i];
```

这部分你已经熟悉了：

```c
i = 0  → e 指向 d.entries[0]
i = 1  → e 指向 d.entries[1]
……
i = 34 → e 指向 d.entries[34]
```

`e` 是指针，没有复制或创建新的对象条目。后面通过它读取当前条目的成员。



**③ 把内部类型标记转换为 EDS 类型号**

```c
unsigned type = e->type == CO_OD_INTEGER16 ? 3u :
                e->type == CO_OD_UNSIGNED8 ? 5u :
                e->type == CO_OD_UNSIGNED16 ? 6u : 7u;
```

这里使用的是三目运算符：

```
条件 ? 条件成立时的值 : 条件不成立时的值
```

这一串写法等价于：

```c
unsigned type;

if (e->type == CO_OD_INTEGER16) {
    type = 3u;
} else if (e->type == CO_OD_UNSIGNED8) {
    type = 5u;
} else if (e->type == CO_OD_UNSIGNED16) {
    type = 6u;
} else {
    type = 7u;
}
```

对应关系：

| 对象类型           | 导出的 EDS 类型号 |
| ------------------ | ----------------- |
| `CO_OD_INTEGER16`  | 3                 |
| `CO_OD_UNSIGNED8`  | 5                 |
| `CO_OD_UNSIGNED16` | 6                 |
| `CO_OD_UNSIGNED32` | 7                 |

我们代码里的枚举值，与 EDS 规定的类型编号不是一回事，所以需要转换。

这里最后一个 `else` 按 `UNSIGNED32` 处理，是基于当前对象表只使用这四种类型。



**④ 把当前对象打印成一行**

```c
printf("%04X,%u,%u,%u,%u,%" PRIu32 ",%" PRId64 ",%" PRId64 "\n",
       e->index,
       e->subindex,
       type,
       e->length,
       (unsigned)e->access,
       read_at(&d, e->index, e->subindex),
       e->min_value,
       e->max_value);
```

先不用被格式字符串吓到，它就是打印八项内容，用逗号隔开：

```
索引,子索引,EDS类型号,字节数,访问权限,当前值,最小值,最大值
```

以初始化后的 DO 对象 `0x6200:01` 为例，会打印：

```
6200,1,5,1,1,0,0,15
```

逐项对应：

| 输出   | 含义                   |
| ------ | ---------------------- |
| `6200` | 索引，按十六进制打印   |
| `1`    | 子索引                 |
| `5`    | EDS 类型号：UNSIGNED8  |
| `1`    | 长度为 1 字节          |
| `1`    | 项目内部权限值：可读写 |
| `0`    | 当前 DO 值             |
| `0`    | 允许的最小值           |
| `15`   | 允许的最大值           |

其中当前值通过你学过的函数读取：

```
read_at(&d, e->index, e->subindex)
```



**⑤ 这几个打印格式是什么意思**

```c
%04X
```

表示以大写十六进制输出，最少四位，不足时在前面补 `0`。

```
%u
```

表示输出 `unsigned int` 类型的十进制数。

这两种稍微特殊：

```
"%" PRIu32
"%" PRId64
```

`PRIu32` 和 `PRId64` 是 `<inttypes.h>` 提供的格式宏：

- `PRIu32`：用于打印 `uint32_t`，这里打印对象当前值。
- `PRId64`：用于打印 `int64_t`，这里打印上下限。

C 会把相邻字符串拼接起来，因此：

```
"%" PRIu32
```

最终会组成适合当前平台的完整打印格式。**你目前记住“固定宽度整数对应的打印写法”就可以。**



**⑥ 全部导出后返回成功**

```
return 0;
```

前面的 `CHECK` 如果失败，会提前返回 `1`；成功打印完全部条目，返回 `0`。

它在测试中的位置是：

```c
C 程序实际初始化出的对象字典
             │
             └── dump() 打印数据
                       │
                       ▼
                Python 测试程序
                       ▲
                       │
                   读取 EDS
                       │
                       └── 比较两边配置是否一致
```

因此，`dump()` 导出的是**代码实际生成的对象信息**，用来帮助发现“C 代码已经改了，但 EDS 没同步”等问题。







## 8：main函数

```c
int main(int argc,char **argv)
{
    if(argc==3 && strcmp(argv[1],"dump")==0) return dump((unsigned)strtoul(argv[2],NULL,10));
    if(argc==2 && strcmp(argv[1],"signed")==0) return test_signed();
    return test_device();
}
```

这个 `main()` 是整个测试程序的入口，负责**根据命令行参数，决定运行哪个函数**。



① 先回顾 `argc` 和 `argv`

`argc`：命令行参数的数量，**包括程序名**。

`argv`：用来访问各个参数字符串，`argv[0]` 通常是程序名或路径。

例如输入：

```c
test_co_device_od.exe dump 1
```

进入 `main()` 时：

```c
argc = 3

argv[0] → "test_co_device_od.exe"
argv[1] → "dump"
argv[2] → "1"
```

这些参数由程序运行环境准备好，再传给 `main()`，不用你自己创建 `argv` 数组。



**② 第一条路线：导出对象字典**

```
if (argc == 3 && strcmp(argv[1], "dump") == 0)
    return dump((unsigned)strtoul(argv[2], NULL, 10));
```

先看条件：

```
argc == 3
```

要求共有三个参数。

```
strcmp(argv[1], "dump") == 0
```

`strcmp()` 用来比较两个字符串，**返回 `0` 表示内容相同**。

所以条件的意思是：

> 参数数量是三个，并且第一个附加参数是 `"dump"`。

`&&` 有短路作用：如果数量不对，就不执行后面的字符串比较。

再拆开这一句：

```
return dump((unsigned)strtoul(argv[2], NULL, 10));
```

对于命令：

```
test_co_device_od.exe dump 1
```

执行过程是：

```c
argv[2] 是字符串 "1"
    ↓
strtoul(argv[2], NULL, 10)
按十进制，把字符串转换成整数 1
    ↓
(unsigned)
转换为 dump() 形参使用的 unsigned 类型
    ↓
dump(1)
初始化节点号为 1 的对象字典并导出
    ↓
main() 返回 dump() 的返回值
```

`strtoul()` 的三个参数可以这样记：

```
strtoul(要转换的字符串, 转换结束位置的输出地址, 进制)
```

这里的 `NULL` 表示不接收转换结束位置，`10` 表示十进制。这段代码是受控测试入口，没有完整检查字符串是否全部为合法数字。





**③ 第二条路线：运行有符号整数测试**

```c
if (argc == 2 && strcmp(argv[1], "signed") == 0)
    return test_signed();
```

输入：

```c
test_co_device_od.exe signed
```

对应：

```c
argc = 2
argv[1] → "signed"
    ↓
条件成立
    ↓
调用 test_signed()
    ↓
main() 原样返回测试结果
```

也就是运行我们刚学过的 `INTEGER16` 边界、范围和长度测试。



**④ 默认路线：运行设备对象字典测试**

```c
return test_device();
```

如果输入：

```c
test_co_device_od.exe
```

此时 `argc = 1`，前面两个条件都不成立，就执行：

```c
test_device();
```

它就是你已经学完的那十组测试。

注意，当前写法是**只要前面两个条件都不匹配，就运行 `test_device()`**。所以输入不认识的参数，也会进入这里。



**⑤ `return 函数调用();` 表示什么**

例如：

```
return test_signed();
```

可以拆成：

```
int result = test_signed();
return result;
```

不是只调用函数，还会把它的结果交回程序运行环境：

```
测试全部通过 → test_signed() 返回 0 → main() 返回 0
某个断言失败 → test_signed() 返回 1 → main() 返回 1
```

CTest 等测试工具就可以根据这个退出码判断测试是否成功。

三种正常用法放在一起就是：

| 启动命令                       | 执行内容                  |
| ------------------------------ | ------------------------- |
| `test_co_device_od.exe`        | 运行十组设备对象字典测试  |
| `test_co_device_od.exe signed` | 运行有符号整数写入测试    |
| `test_co_device_od.exe dump 1` | 导出节点号为 1 的对象字典 |

到这里，这个测试文件从辅助函数、测试内容到主函数入口，就完整串起来了。





