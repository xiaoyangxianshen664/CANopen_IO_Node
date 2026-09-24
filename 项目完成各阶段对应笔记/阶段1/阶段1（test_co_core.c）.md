# 解析



## 1：断言

```c
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
    return 1; } } while (0)
这是一个经典的 C 语言断言式检查宏，常用于错误处理和调试
```



#### 1:#define CHECK(expr)

- 定义一个名为 `CHECK` 的宏，接受一个参数 `expr`（可以是一个条件表达式，如 `x > 0`）。
- 宏会在编译时被展开为后面的代码。



#### 2：do { ... } while (0)

这是一个常见的 **宏封装技巧**，目的是：

- 让宏可以像**一条语句**一样使用（后面可以加分号 `;`）。
- 避免宏展开后可能因为上下文（如 `if-else` 语句）导致的语法错误。



C语言的“就近匹配”原则 : C语言的 `if` 语句只会控制离它最近的那一条语句。这就给多行宏带来了灾难。

假设：

```
#define REPORT() { printf("发生错误\n"); count++; }
```

这样调用：

```c
if (error)
    REPORT();
else
    printf("正常\n");
```

展开后：

```c
if (error)
    { printf("发生错误\n"); count++; }; /* 注意这个分号 */
else
    printf("正常\n");
```

大括号后多出的分号形成了一条空语句，隔开了 `if` 和 `else`，导致语法错误。

```c
这里的 else 必须是紧接着这个 if 的一部分。

但是你写成：

if (error)
{
    ...
}

;       // ← 这里插进了一条其他语句

else    // ← else 来晚了
{
    ...
}

对于编译器来说：

“刚才那个 if 已经结束了。现在突然来了一个 else，但我前面没有一个可以接它的 if。”

所以语法错误。
```

```c
而 do…while 正好需要结尾的分号：
if (error)
    do {
        printf("发生错误\n");
        count++;
    } while (0);
else
    printf("正常\n");
这就是合法代码。
回到你的 CHECK：
#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)
里面的 if 负责判断是否失败；外面的 do…while (0) 负责把整段宏包装好，让你可以像普通语句一样写 CHECK(条件);。
```









#### 3：if (!(expr)) { ... }

- 检查 `expr` 是否为假（expr为假时，即 `!expr` 是否为真 ，此时if（!(expr)）成立）执行大括号内的错误处理代码。



#### 4：fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);

这是错误信息的打印部分，使用了三个特殊功能：

- **`stderr`**：标准错误流，用于输出错误信息（不会缓冲，立即显示）。
- **`__FILE__`**：预定义宏，表示当前源文件名（字符串）。
- **`__LINE__`**：预定义宏，表示当前代码行号（整数）。
- **`#expr`**：`#` 是 字符串化运算符，会把 `expr` 转换成字符串。

例如，如果 `CHECK(x > 0)` 失败，会打印 `x > 0`。

示例输出：main.c:42: x > 0

表示在 `main.c` 文件的第 42 行，`x > 0` 这个条件检查失败了



#### 5：return 1;

- 如果条件检查失败，函数会立即返回 `1`，表示发生了错误。
  - 这通常用于 `main` 函数或某些需要错误码的函数中。



```c
假设有以下代码：
int main() {
    int x = -1;
    CHECK(x > 0);  // 检查 x 是否大于 0
    return 0;
}
宏展开后相当于：
int main() {
    int x = -1;
    do {
        if (!(x > 0)) {
            fprintf(stderr, "%s:%d: %s\n", "main.c", 4, "x > 0");
            return 1;
        }
    } while (0);
    return 0;
}
由于 x = -1，条件 x > 0 不成立，程序会打印错误信息并退出。
```

整个宏可以记成：

```c
CHECK(条件)
    │
    ├── 条件为真：继续运行
    │
    └── 条件为假：
          打印 文件名:行号:条件
          当前测试函数返回 1
```



## 2：PC总线模拟结构体

```c
typedef struct {
    unsigned calls; /* 记录发送回调被调用的次数 */
    can_frame_t frame; /* 保存最近一次提交报文的副本 */
    co_status_t result; /* 预设发送回调的返回状态 */
} fake_bus_t;
```

这个结构体是 PC 测试时使用的记录本：记录发送函数被调用了几次、提交了什么报文，并设置模拟发送时要返回的结果



### **① `unsigned calls;`：记录调用次数**

```
unsigned calls;
```

这里的 `unsigned` 等价于 `unsigned int`，是无符号整数类型。

我们用它记录：**模拟发送函数 `fake_tx()` 被调用了多少次。**

例如，开始时：

```
bus.calls = 0;
```

每次进入模拟发送函数，就执行：

```
bus.calls++;
```

于是调用一次后为 `1`，调用两次后为 `2`。

这能帮助测试判断：调用 `co_send()` 后，是否真的执行了发送回调；如果报文检查失败，是否正确地没有调用发送回调。





### **② `can_frame_t frame;`：保存提交的报文**

```
can_frame_t frame;
```

`can_frame_t` 就是前面学过的报文结构体类型，里面有：

```c
id
dlc
data[8]
is_extended
is_remote
is_fd
```

这里表示：**在 `fake_bus_t` 里面，放一个完整的报文结构体成员，名字叫 `frame`。**

假设我们创建：

```
fake_bus_t bus = {0};
```

那么可以这样访问：

```
bus.frame.id       /* 记录的报文 CAN-ID */
bus.frame.dlc      /* 记录的报文数据长度 */
bus.frame.data[0]  /* 记录的报文第一个数据字节 */
```

模拟发送函数会把提交的报文复制到 `bus.frame`，方便测试随后检查：**提交出去的内容是不是我们预期的内容。**

它保存的是完整副本，不是报文的地址。





### **③ `co_status_t result;`：预设模拟发送的结果**

```
co_status_t result;
```

`co_status_t` 是之前学过的操作结果枚举类型，可以取：

```
CO_OK
CO_ERR_TX_BUSY
CO_ERR_TX_FAILED
```

测试时，我们提前设置它：

```
bus.result = CO_OK;             /* 模拟接受发送请求 */
```

或者：

```
bus.result = CO_ERR_TX_BUSY;    /* 模拟传输层正忙 */
```

随后 `fake_tx()` 会返回这个预设值。

这样就能检查：**底层返回忙或失败时，`co_send()` 是否把对应结果原样返回。**

注意，`result` 是我们为了测试而提前设定的结果，不是真实 CAN 硬件检测出的结果。





### **④ ` fake_bus_t;`：给结构体类型起名字**

```
fake_bus_t;
```

到这里，定义了一个名为 `fake_bus_t` 的类型，**还没有创建变量**。

创建变量要另外写：

```
fake_bus_t bus = {0};
```

此时：

| 成员         | 初始内容                 |
| ------------ | ------------------------ |
| `bus.calls`  | `0`                      |
| `bus.frame`  | 内部各成员均为零         |
| `bus.result` | 数值为 `0`，对应 `CO_OK` |

这个结构体目前只是提供存储空间。**增加次数、复制报文、返回预设结果，都要由下一步学习的 `fake_tx()` 来执行。**



## 3：模拟发送函数

```c
static co_status_t fake_tx(void *user, const can_frame_t *frame)
{
    fake_bus_t *bus = user; /* 把通用私有数据指针还原为模拟总线指针 */
    ++bus->calls; /* 每次进入回调都计数，用来检测意外发送或重试 */
    bus->frame = *frame; /* 结构体整体复制，避免保存短生命周期的帧指针 */
    return bus->result; /* 模拟传输层的成功、忙或失败 */
}

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

这个 `fake_tx()` 就是我们在 PC 测试中使用的模拟发送函数。它会记录调用次数、保存报文，然后返回预设结果。



### **① 函数头：接收什么，返回什么**

```
static co_status_t fake_tx(void *user, const can_frame_t *frame)
```

| 部分                       | 含义                                         |
| -------------------------- | -------------------------------------------- |
| `static`                   | 这个函数只供当前 `.c` 文件使用               |
| `co_status_t`              | 返回一个操作结果，如 `CO_OK`                 |
| `fake_tx`                  | 函数名                                       |
| `void *user`               | 接收模拟总线变量的地址                       |
| `const can_frame_t *frame` | 接收待发送报文的地址，通过这个指针只读取报文 |

它的参数与返回类型符合之前的函数指针类型：

```
typedef co_status_t (*co_tx_fn)(void *user, const can_frame_t *frame);
```

所以，`fake_tx`()函数的地址 可以作为 `co_init()` 的第三个参数，保存到节点的 `tx` 中。

tx是接受函数指针的一个形参



### **② 把通用指针转换成模拟总线指针**

```
fake_bus_t *bus = user;
```

我们先用一个明确的调用例子理解：

```
fake_bus_t test_bus = {0};  /* 创建模拟总线变量 */
can_frame_t tx_frame = {0}; /* 创建报文变量 */

fake_tx(&test_bus, &tx_frame);
```

进入函数后：

```
user   /* 保存 &test_bus */
frame  /* 保存 &tx_frame */
```

但是，`user` 的类型是 `void *`，它没有说明指向的数据是什么类型，因此不能直接通过它访问 `calls` 等成员。

这一行：

```
fake_bus_t *bus = user;
```

就是创建一个 `fake_bus_t *` 类型的指针变量 `bus`，让它指向同一个对象：

```
user ──→ test_bus
bus  ──→ test_bus
```

**没有创建新的模拟总线，也没有复制结构体，只是用合适的指针类型访问原来的变量。** 在 C 语言中，这里的 `void *` 可以直接赋给对应的对象指针。



### **③ 记录一次调用**

```
++bus->calls;
```

`bus` 指向外面的 `test_bus`，所以这里修改的就是：

```
test_bus.calls
```

`++` 表示加一。原来为 `0`，执行后就变成 `1`。

这里统计的是**回调被调用的次数**，即使后面返回忙或失败，这次调用也会计数。





### **④ 保存报文副本**

```
bus->frame = *frame;
```

这一行两边虽然都有 `frame`，但含义不同：

| 写法         | 含义                                   |
| ------------ | -------------------------------------- |
| `bus->frame` | 模拟总线结构体中，用于保存报文的成员   |
| `frame`      | 指向本次待发送报文的指针               |
| `*frame`     | 通过指针取到本次待发送的整个报文结构体 |

结合刚才的调用，它相当于：

```
test_bus.frame = tx_frame;
```

**C 允许同类型结构体整体赋值**，因此 `id`、`dlc`、`data` 数组以及三个帧类型标志都会一起复制。

复制后，就算我们再修改原来的 `tx_frame`，已经记录的 `test_bus.frame` 也不会跟着改变。



### **⑤ 返回预设结果**

```
return bus->result;
```

它读取我们提前设置的结果并返回。例如：

```
test_bus.result = CO_ERR_TX_BUSY;
```

那么这次调用：

```
fake_tx(&test_bus, &tx_frame);
```

就会返回：

```
CO_ERR_TX_BUSY
```



### **⑥ 把整个过程连起来**

下面直接调用 `fake_tx()`，看清它自己的行为：

```c
fake_bus_t test_bus = {0};
test_bus.result = CO_OK;          /* 预设返回成功 */

can_frame_t tx_frame = {0};
tx_frame.id = 0x181;
tx_frame.dlc = 1;
tx_frame.data[0] = 0x05;

co_status_t status = fake_tx(&test_bus, &tx_frame);
```

执行完以后：

```
test_bus.calls         /* 1：回调被调用了一次 */
test_bus.frame.id      /* 0x181 */
test_bus.frame.dlc     /* 1 */
test_bus.frame.data[0] /* 0x05 */
status                 /* CO_OK */
```

在后面的发送测试中，我们会让 `co_send()` 间接调用它：

```
co_context_t node = {0};

co_init(&node, 1, fake_tx, &test_bus);
co_send(&node, &tx_frame);
```

`co_send()` 内部的：

```
ctx->tx(ctx->tx_user, frame);
```

此时就相当于：

```
fake_tx(&test_bus, &tx_frame);
```

这时你就能通过 `test_bus` 中留下的记录，检查 `co_send()` 有没有调用回调、交给回调的报文是否正确。







## 4：合法帧检查函数

```c
static int test_frame(void)
{
    can_frame_t f = {0}; /* 全部成员清零，构造标准帧、ID=0、DLC=0 */
    CHECK(co_frame_validate(NULL) == CO_ERR_ARGUMENT); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_frame_validate(&f) == CO_OK); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    f.id = 0x7FFu;
    f.dlc = 8;
    CHECK(co_frame_validate(&f) == CO_OK); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    f.id = 0x800u;
    CHECK(co_frame_validate(&f) == CO_ERR_CAN_ID); /* 故意传入异常条件，确认返回预期错误码 */
    f.id = UINT32_MAX;
    CHECK(co_frame_validate(&f) == CO_ERR_CAN_ID); /* 故意传入异常条件，确认返回预期错误码 */
    f.id = 1;
    f.dlc = 9;
    CHECK(co_frame_validate(&f) == CO_ERR_DLC); /* 故意传入异常条件，确认返回预期错误码 */
    f.dlc = 255;
    CHECK(co_frame_validate(&f) == CO_ERR_DLC); /* 故意传入异常条件，确认返回预期错误码 */
    f.dlc = 0;
    f.is_extended = 1;
    CHECK(co_frame_validate(&f) == CO_ERR_FRAME_TYPE); /* 故意传入异常条件，确认返回预期错误码 */
    f.is_extended = 0;
    f.is_remote = 1;
    CHECK(co_frame_validate(&f) == CO_ERR_FRAME_TYPE); /* 故意传入异常条件，确认返回预期错误码 */
    f.is_remote = 0;
    f.is_fd = 1;
    CHECK(co_frame_validate(&f) == CO_ERR_FRAME_TYPE); /* 故意传入异常条件，确认返回预期错误码 */
    return 0; /* 本组检查全部通过，向 CTest 返回成功 */
}

```

这个 `test_frame()` 专门测试之前学过的 `co_frame_validate()`：合法帧能不能通过，非法帧能不能返回正确的错误码。

先记住一个关键点：故意传入错误数据，得到预期错误码，也算测试通过。





### **① 函数头和创建报文变量**

```c
static int test_frame(void)
{
    can_frame_t f = {0};
```

- `static`：仅在当前 `.c` 文件中使用。
- `int`：返回测试结果，`0` 表示通过，`1` 表示失败。
- `void`：不接收参数。
- `f`：我们专门用来测试的报文变量。

`{0}` 将它的所有成员初始化为零：

```c
f.id = 0;
f.dlc = 0;
f.data[0] /* 到 data[7] 都是 0 */
f.is_extended = 0;
f.is_remote = 0;
f.is_fd = 0;
```

这表示一帧 **ID 为 0、数据长度为 0 的经典 CAN 标准数据帧**。

​	



### **② 检查空指针是否被拒绝**

```c
CHECK(co_frame_validate(NULL) == CO_ERR_ARGUMENT);
```

执行顺序是：

```c
调用 co_frame_validate(NULL)
        ↓
得到返回值
        ↓
判断它是否等于 CO_ERR_ARGUMENT
        ↓
CHECK 判断这个条件是否成立
```

我们故意传入 `NULL`，期望函数发现参数错误。

如果它返回 `CO_ERR_ARGUMENT`，这个检查就通过；返回其他值，才是测试失败。





### **③ 检查全零的帧是否能通过基本格式校验**

```
CHECK(co_frame_validate(&f) == CO_OK);
```

当前：

```
f.id = 0;
f.dlc = 0;
```

ID 没有超出 `0x7FF`，DLC 没有超出 `8`，帧类型标志都是零，所以预期返回 `CO_OK`。

这里检查的是 **CAN 帧基本格式**。虽然 ID 为 `0` 在 CANopen 中用于 NMT，但本函数不检查 NMT 必须有两个数据字节；那是后面服务分类时的检查。





### **④ 检查合法范围的最大值**

```c
f.id = 0x7FFu;
f.dlc = 8;
CHECK(co_frame_validate(&f) == CO_OK);
```

`0x7FF` 是 11 位标准 CAN-ID 的最大值，`8` 是经典 CAN 数据长度的最大值。

它们都处于合法范围内，所以应该通过。

`0x7FFu` 后面的 `u` 表示无符号整数常量。

这叫**边界测试**：检查“刚好等于最大允许值”时，函数有没有误判。





### **⑤ 检查 CAN-ID 越界**

```
f.id = 0x800u;
CHECK(co_frame_validate(&f) == CO_ERR_CAN_ID);
```

`0x800` 比最大合法值 `0x7FF` 大一，因此应该返回：

```
CO_ERR_CAN_ID
```

然后再测试一个更大的数：

```
f.id = UINT32_MAX;
CHECK(co_frame_validate(&f) == CO_ERR_CAN_ID);
```

`UINT32_MAX` 是 `uint32_t` 能表示的最大值：

```
0xFFFFFFFF
```

`f.id` 可以存下这个数，但它不符合标准 CAN-ID 的范围，所以也应该被拒绝。





### **⑥ 恢复合法 ID，再检查 DLC 越界**

```
f.id = 1;
f.dlc = 9;
CHECK(co_frame_validate(&f) == CO_ERR_DLC);
```

这里先把 `id` 改回合法值 `1`，才能单独检查 DLC 的问题。

因为 `co_frame_validate()` 会先检查 ID，再检查 DLC。如果还保留前面的非法 ID，就会先返回 `CO_ERR_CAN_ID`，无法验证 DLC 检查。

`9` 比允许的最大长度 `8` 大一，所以预期返回：

```
CO_ERR_DLC
```

接着：

```
f.dlc = 255;
CHECK(co_frame_validate(&f) == CO_ERR_DLC);
```

`dlc` 是 `uint8_t` 类型，能存下 `255`，但经典 CAN 不允许这个数据长度，因此也应返回长度错误。

**注意：给 `f.dlc` 赋值为 `255`，只是修改一个数字，并没有向 `data[8]` 数组中写入 255 字节。**





### **⑦ 恢复合法 DLC，再分别测试不支持的帧类型**

先检查扩展帧：

```
f.dlc = 0;
f.is_extended = 1;
CHECK(co_frame_validate(&f) == CO_ERR_FRAME_TYPE);
```

此时 ID 和 DLC 都合法，但扩展帧标志为 `1`，所以预期返回帧类型错误。

然后检查远程帧：

```
f.is_extended = 0;
f.is_remote = 1;
CHECK(co_frame_validate(&f) == CO_ERR_FRAME_TYPE);
```

先清除扩展帧标志，再设置远程帧标志，确保这次测试的是**远程帧能否被拒绝**。

最后检查 CAN FD 帧：

```
f.is_remote = 0;
f.is_fd = 1;
CHECK(co_frame_validate(&f) == CO_ERR_FRAME_TYPE);
```

同样，先清除上一次的标志，再单独设置 FD 标志。

这些帧类型在本项目中都不支持，因此都预期返回：

```
CO_ERR_FRAME_TYPE
```





### **⑧ 全部检查通过，返回 `0`**

```
return 0;
```

只有前面的所有 `CHECK` 都通过，才会执行到这里。

任何一个检查不符合预期，`CHECK` 中的 `return 1;` 都会立即退出 `test_frame()`。

严格来说，这里的 `0` 先返回给调用它的 `main()`，再由 `main()` 返回进程退出码，CTest 据此判断成功或失败。

整个函数一直在重复这三步：

```
修改 f，构造一种测试情况
        ↓
调用 co_frame_validate(&f)
        ↓
用 CHECK 比较实际返回值与预期返回值
```

它不发送报文，只验证我们写的帧校验函数是否按规则工作。



## 5：检查初始化节点函数是否正确

```c
static int test_init(void)
{
    co_context_t ctx = {0}; /* 建立清零的上下文，使用前仍需调用 co_init */
    fake_bus_t bus = {0}; /* 调用次数为 0，默认返回 CO_OK */
    CHECK(co_init(NULL, 1, fake_tx, &bus) == CO_ERR_ARGUMENT); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_init(&ctx, 1, NULL, &bus) == CO_ERR_ARGUMENT); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_init(&ctx, 1, fake_tx, &bus) == CO_OK); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(ctx.node_id == 1 && ctx.state == CO_NMT_INITIALIZATION); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(ctx.tx == fake_tx && ctx.tx_user == &bus); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(co_init(&ctx, 0, fake_tx, NULL) == CO_ERR_NODE_ID); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_init(&ctx, 128, fake_tx, NULL) == CO_ERR_NODE_ID); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_init(&ctx, 255, fake_tx, NULL) == CO_ERR_NODE_ID); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(ctx.node_id == 1 && ctx.tx_user == &bus); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(co_init(&ctx, 127, fake_tx, NULL) == CO_OK); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(ctx.node_id == 127 && ctx.tx_user == NULL); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    return 0; /* 本组检查全部通过，向 CTest 返回成功 */
}
```

这个 `test_init()` 专门测试 `co_init()`，确认它能正确初始化上下文，也能拒绝非法参数。

```c
这里创建两个变量：

- `ctx`：准备交给 `co_init()` 初始化的节点上下文。
- `bus`：作为 `fake_tx()` 的辅助数据，保存模拟发送记录。

`{0}` 会把所有成员清零。因为 `CO_OK` 的枚举值是 `0`，所以 `bus.result` 初始就是 `CO_OK`。
```



### **① 测试节点上下文指针为空**

```
CHECK(co_init(NULL, 1, fake_tx, &bus) == CO_ERR_ARGUMENT);
```

第一个参数本应是 `ctx` 的地址，这里故意传 `NULL`。

`co_init()` 发现不能写入空地址，于是返回：

```
CO_ERR_ARGUMENT
```

​	

### **② 测试发送函数为空**

```
CHECK(co_init(&ctx, 1, NULL, &bus) == CO_ERR_ARGUMENT);
```

这次 `ctx` 地址有效，但第三个参数发送函数是 `NULL`。

没有发送函数，初始化后无法发送，因此也返回参数错误。



### **③ 测试正常初始化**

```
CHECK(co_init(&ctx, 1, fake_tx, &bus) == CO_OK);
```

这次四个参数都有效：

```
&ctx     → 要初始化的上下文
1        → 节点号
fake_tx  → 保存发送函数地址
&bus     → 保存辅助数据地址
```

函数内部相当于完成：

```
ctx.node_id = 1;
ctx.state = CO_NMT_INITIALIZATION;
ctx.tx = fake_tx;
ctx.tx_user = &bus;
```

然后检查这些成员：

```
CHECK(ctx.node_id == 1 &&
      ctx.state == CO_NMT_INITIALIZATION);
```

确认节点号和初始 NMT 状态正确。

```
CHECK(ctx.tx == fake_tx &&
      ctx.tx_user == &bus);
```

确认：

- `ctx.tx` 确实保存了 `fake_tx` 的函数地址；
- `ctx.tx_user` 确实保存了 `bus` 的地址。

注意，这里只是比较地址，还没有调用 `fake_tx()`。





### **④ 测试非法节点号**

```
CHECK(co_init(&ctx, 0, fake_tx, NULL) == CO_ERR_NODE_ID);
CHECK(co_init(&ctx, 128, fake_tx, NULL) == CO_ERR_NODE_ID);
CHECK(co_init(&ctx, 255, fake_tx, NULL) == CO_ERR_NODE_ID);
```

本项目节点号只允许 `1～127`：

- `0` 是广播目标，不能作为本节点编号；
- `128` 超过上限；
- `255` 也超过上限。

所以都应返回：

```
CO_ERR_NODE_ID
```





### **⑤ 检查失败时不会破坏原配置**

```c
static int test_init(void)
{
    co_context_t ctx = {0}; /* 建立清零的上下文，使用前仍需调用 co_init */
    fake_bus_t bus = {0}; /* 调用次数为 0，默认返回 CO_OK */
    CHECK(co_init(NULL, 1, fake_tx, &bus) == CO_ERR_ARGUMENT); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_init(&ctx, 1, NULL, &bus) == CO_ERR_ARGUMENT); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_init(&ctx, 1, fake_tx, &bus) == CO_OK); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(ctx.node_id == 1 && ctx.state == CO_NMT_INITIALIZATION); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(ctx.tx == fake_tx && ctx.tx_user == &bus); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(co_init(&ctx, 0, fake_tx, NULL) == CO_ERR_NODE_ID); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_init(&ctx, 128, fake_tx, NULL) == CO_ERR_NODE_ID); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_init(&ctx, 255, fake_tx, NULL) == CO_ERR_NODE_ID); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(ctx.node_id == 1 && ctx.tx_user == &bus); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(co_init(&ctx, 127, fake_tx, NULL) == CO_OK); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(ctx.node_id == 127 && ctx.tx_user == NULL); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    return 0; /* 本组检查全部通过，向 CTest 返回成功 */
}
```

```c
我们按时间顺序看，就清楚了。
先执行这次成功初始化：
co_init(&ctx, 1, fake_tx, &bus);
执行后，ctx 里面保存：
ctx.node_id = 1;
ctx.tx_user = &bus;
接着测试非法节点号：
co_init(&ctx, 0, fake_tx, NULL);
进入 co_init() 后：
if (ctx == NULL || tx == NULL) { ... }

if (node_id == 0u || node_id > CO_NODE_ID_MAX) {
    return CO_ERR_NODE_ID;
}
因为 node_id 是 0，函数直接返回：
CO_ERR_NODE_ID
它还没有执行下面这些赋值：
ctx->node_id = node_id;
ctx->state = CO_NMT_INITIALIZATION;
ctx->tx = tx;
ctx->tx_user = tx_user;
所以原来的内容不会改变：
ctx.node_id  /* 仍然是 1 */
ctx.tx_user  /* 仍然是 &bus */
随后这两次也是同样道理：
co_init(&ctx, 128, fake_tx, NULL);
co_init(&ctx, 255, fake_tx, NULL);
节点号不合法，函数都在检查阶段返回，没有改写 ctx。
因此：
CHECK(ctx.node_id == 1 &&
      ctx.tx_user == &bus);
实际检查两个条件：
ctx.node_id == 1
以及：
ctx.tx_user == &bus
中间的 && 表示“并且”，两个条件都成立，整个检查才通过。
这段测试是在确认：
非法初始化
    ↓
返回错误
    ↓
原来的有效配置仍保留
如果 co_init() 写成下面这样，就会有问题：
ctx->node_id = node_id;   /* 先写入 */
if (node_id == 0u) {
    return CO_ERR_NODE_ID;
}
那么传入 0 后，虽然返回错误，但 ctx.node_id 已经被改成 0，原配置被破坏了。
我们现在的实现采用：
先检查所有参数
    ↓
全部合法后才写入 ctx
所以测试中的“不会破坏原配置”就是指：初始化失败时，ctx 保持上一次成功初始化的内容。
```



```
CHECK(ctx.node_id == 1 &&
      ctx.tx_user == &bus);
```

前面三次非法初始化都使用了错误节点号。这个检查确认：失败后，原来成功保存的配置仍然存在：

```
ctx.node_id 仍然是 1
ctx.tx_user 仍然是 &bus
```

这是因为 `co_init()` 会先完成参数检查，检查通过后才写入 `ctx`。







### **⑥ 测试最大合法节点号**

```
CHECK(co_init(&ctx, 127, fake_tx, NULL) == CO_OK);
```

`127` 是允许的最大节点号，因此初始化成功。

确认：CHECK(ctx.node_id == 127 && ctx.tx_user == NULL);

- 节点号被更新为 `127`；
- 本次传入的辅助数据是 `NULL`，所以 `ctx.tx_user` 也变成 `NULL`。

这里说明 `tx_user` 可以为空，只要发送函数本身不需要辅助数据即可。

最后：

```
return 0;
```

表示这一组初始化测试全部通过。

整个测试顺序可以概括为：

```
测试空参数
    ↓
测试发送函数为空
    ↓
测试正常初始化
    ↓
检查 ctx 成员是否正确
    ↓
测试非法节点号
    ↓
确认失败不会破坏原配置
    ↓
测试最大合法节点号
```

这个函数测试的重点是：**`co_init()` 是否正确保存节点号、状态、发送函数地址和辅助数据地址，并且在参数错误时保持原上下文不变。**





## 6:测试发送函数

```c
static int test_tx(void)
{
    //初始化
    fake_bus_t bus = {0}; /* 调用次数为 0，默认返回 CO_OK */
    co_context_t ctx = {0}; /* 建立清零的上下文，使用前仍需调用 co_init */
    can_frame_t f = {0x7FFu, 8, {0, 1, 2, 3, 4, 5, 6, 255}, 0, 0, 0};
    
    
    CHECK(co_send(&ctx, &f) == CO_ERR_ARGUMENT); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_init(&ctx, 1, fake_tx, &bus) == CO_OK); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(co_send(NULL, &f) == CO_ERR_ARGUMENT); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_send(&ctx, NULL) == CO_ERR_ARGUMENT); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(bus.calls == 0); /* 检查回调次数，确保非法帧不发送、失败不自动重试 */
    CHECK(co_send(&ctx, &f) == CO_OK); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(bus.calls == 1 && bus.frame.id == f.id && bus.frame.dlc == 8); /* 检查回调次数，确保非法帧不发送、失败不自动重试 */
    CHECK(memcmp(bus.frame.data, f.data, 8) == 0); /* 逐字节比较 8 字节载荷，确认发送内容未改变 */
    bus.result = CO_ERR_TX_BUSY;
    CHECK(co_send(&ctx, &f) == CO_ERR_TX_BUSY); /* 故意传入异常条件，确认返回预期错误码 */
    bus.result = CO_ERR_TX_FAILED;
    CHECK(co_send(&ctx, &f) == CO_ERR_TX_FAILED); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(bus.calls == 3); /* 检查回调次数，确保非法帧不发送、失败不自动重试 */
    f.dlc = 9;
    CHECK(co_send(&ctx, &f) == CO_ERR_DLC); /* 故意传入异常条件，确认返回预期错误码 */
    f.dlc = 8;
    f.id = 0x800u;
    CHECK(co_send(&ctx, &f) == CO_ERR_CAN_ID); /* 故意传入异常条件，确认返回预期错误码 */
    f.id = 1;
    f.is_remote = 1;
    CHECK(co_send(&ctx, &f) == CO_ERR_FRAME_TYPE); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(bus.calls == 3); /* 检查回调次数，确保非法帧不发送、失败不自动重试 */
    f.is_remote = 0;
    f.dlc = 0;
    bus.result = CO_OK;
    CHECK(co_send(&ctx, &f) == CO_OK); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(bus.calls == 4 && bus.frame.dlc == 0); /* 检查回调次数，确保非法帧不发送、失败不自动重试 */
    return 0; /* 本组检查全部通过，向 CTest 返回成功 */
}

`test_tx()` 用来测试 **`co_send()` 的完整发送流程**：
 1. 上下文未初始化时，不能发送；
2. 参数错误时，不能调用发送回调；
3. 合法报文会调用 `fake_tx()`；
4. 回调返回什么状态，`co_send()` 就返回什么状态；
5. 非法报文不会触发发送回调。
```

第一步：

这里创建：

- `bus`：记录 `fake_tx()` 的执行情况；
- `ctx`：节点上下文；
- `f`：准备发送的报文。

这个初始化按照 `can_frame_t` 成员顺序对应：

```
f.id = 0x7FF;
f.dlc = 8;
f.data = {0, 1, 2, 3, 4, 5, 6, 255};
f.is_extended = 0;
f.is_remote = 0;
f.is_fd = 0;
```

所以 `f` 是一帧合法的标准经典 CAN 数据帧，但是节点信息没有赋值，而是全弄为0了。



### **① 上下文还没初始化，发送应该失败**

```
CHECK(co_send(&ctx, &f) == CO_ERR_ARGUMENT);
```

此时 `ctx` 只是清零：

```
ctx.tx = NULL;
ctx.node_id = 0;
```

还没有调用 `co_init()`，因此没有有效发送函数，也没有合法节点号。

`co_send()` 首先调用内部的：

```
co_context_validate(ctx);
```

发现上下文无效，于是返回：

```
CO_ERR_ARGUMENT
```

这时不会调用 `fake_tx()`。





### **② 正确初始化节点**

```
CHECK(co_init(&ctx, 1, fake_tx, &bus) == CO_OK);
```

这一步把以下内容保存到 `ctx`：

```
ctx.node_id = 1;
ctx.state = CO_NMT_INITIALIZATION;
ctx.tx = fake_tx;
ctx.tx_user = &bus;
```

以后执行：

```
co_send(&ctx, &f);
```

内部的：

```
ctx->tx(ctx->tx_user, frame);
```

就等价于：

```c
fake_tx(&bus, &f);
```





### **③ 测试空上下文**

```
CHECK(co_send(NULL, &f) == CO_ERR_ARGUMENT);
```

`ctx` 是空指针，发送失败。



### **④ 测试空报文指针**

```
CHECK(co_send(&ctx, NULL) == CO_ERR_ARGUMENT);
```

上下文有效，但 `frame` 是空指针。

`co_send()` 在校验报文时调用：

```
co_frame_validate(NULL);
```

因此返回参数错误。

然后检查：

```
CHECK(bus.calls == 0);
```

前面的错误都发生在执行代码真正调用 `fake_tx()` 之前，所以回调调用次数仍然是 `0`。





### **⑤ 测试合法发送**

```
CHECK(co_send(&ctx, &f) == CO_OK);
```

执行流程是：

```
检查 ctx
    ↓
检查 f
    ↓
调用 ctx->tx(ctx->tx_user, &f)
    ↓
实际调用 fake_tx(&bus, &f)
    ↓
fake_tx 返回 bus.result
```

因为 `bus.result` 初始是 `CO_OK`，所以最终返回 `CO_OK`。

回调执行后：

```
CHECK(bus.calls == 1 &&
      bus.frame.id == f.id &&
      bus.frame.dlc == 8);
```

检查三件事：

- 回调确实调用了一次；
- 保存的报文 ID 正确；
- 保存的 DLC 正确。

接着：

```
CHECK(memcmp(bus.frame.data, f.data, 8) == 0);
```

`memcmp` 比较两段内存。这里比较 `8` 个字节，确认 `fake_tx()` 保存的 `data` 数组和原报文完全一致。





### **⑥ 模拟发送忙**

```
bus.result = CO_ERR_TX_BUSY;
CHECK(co_send(&ctx, &f) == CO_ERR_TX_BUSY);
```

这里不是让 `co_send()` 自己判断忙，而是提前设置：

```
bus.result = CO_ERR_TX_BUSY;
```

当 `fake_tx()` 被调用时，它执行：

```
return bus->result;
```

于是返回 `CO_ERR_TX_BUSY`，`co_send()` 再原样返回这个结果。



### **⑦ 模拟发送失败**

```
bus.result = CO_ERR_TX_FAILED;
CHECK(co_send(&ctx, &f) == CO_ERR_TX_FAILED);
```

过程相同，只是这次模拟底层返回发送失败。

到这里，回调总共调用了三次：

```
第一次：CO_OK
第二次：CO_ERR_TX_BUSY
第三次：CO_ERR_TX_FAILED
```

所以：

```
CHECK(bus.calls == 3);
```

注意：虽然忙和失败，仍然算已经调用过回调，因此次数会增加。





### **⑧ 非法 DLC 不应调用回调**

```
f.dlc = 9;
CHECK(co_send(&ctx, &f) == CO_ERR_DLC);
```

`co_send()` 会先执行：

```
co_frame_validate(&f);
```

发现 DLC 为 9，超过经典 CAN 的最大值 8，于是直接返回 `CO_ERR_DLC`，不会进入 `fake_tx()`。

然后恢复：

```
f.dlc = 8;
```



### **⑨ 非法 CAN-ID 不应调用回调**

```
f.id = 0x800u;
CHECK(co_send(&ctx, &f) == CO_ERR_CAN_ID);
```

`0x800` 超出 11 位标准 CAN-ID 的最大值 `0x7FF`，所以返回 CAN-ID 错误。

之后：

```
f.id = 1;
```

把 ID 恢复为合法值。



### **⑩ 非法帧类型不应调用回调**

```
f.is_remote = 1;
CHECK(co_send(&ctx, &f) == CO_ERR_FRAME_TYPE);
```

远程帧在本项目中不支持，因此校验失败。

```
CHECK(bus.calls == 3);
```

这里再次确认回调次数仍然是 3。也就是说，DLC、CAN-ID、帧类型错误都被 `co_send()` 在前面拦截，没有调用 `fake_tx()`。



### **⑪ 测试 DLC=0 的合法空数据帧**

```
f.is_remote = 0;
f.dlc = 0;
bus.result = CO_OK;
CHECK(co_send(&ctx, &f) == CO_OK);
```

DLC 为 0 在基本帧校验中是合法的，表示这帧没有有效数据字节。

因此发送成功，回调次数变成 4：

```
CHECK(bus.calls == 4 &&
      bus.frame.dlc == 0);
```

最后：

```
return 0;
```

表示整个发送测试通过。

这组测试最重要的验证关系是：

```
co_send()
  ├─ 上下文错误       → 直接返回，不调用 fake_tx
  ├─ 报文格式错误     → 直接返回，不调用 fake_tx
  └─ 上下文和报文正确 → 调用 fake_tx
                           └─ 原样返回 fake_tx 的结果
```





## 7：检查分类函数

```c
static int test_routing(void)
{
    co_context_t ctx = {0}; /* 建立清零的上下文，使用前仍需调用 co_init */
    fake_bus_t bus = {0}; /* 调用次数为 0，默认返回 CO_OK */
    can_frame_t f = {0}; /* 全部成员清零，构造标准帧、ID=0、DLC=0 */
    co_rx_kind_t kind = CO_RX_SDO; /* 故意设非空初值，检查出错时是否清除结果 */
    unsigned node, id;
    CHECK(co_init(&ctx, 1, fake_tx, &bus) == CO_OK); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(co_classify_rx(NULL, &f, &kind) == CO_ERR_ARGUMENT); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(kind == CO_RX_NONE); /* 检查分类输出，避免错误或忽略时残留旧结果 */
    CHECK(co_classify_rx(&ctx, NULL, &kind) == CO_ERR_ARGUMENT); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_classify_rx(&ctx, &f, NULL) == CO_ERR_ARGUMENT); /* 故意传入异常条件，确认返回预期错误码 */
    for (node = 1; node <= 127; ++node) { /* 遍历全部合法本机节点编号 */
        CHECK(co_init(&ctx, (uint8_t)node, fake_tx, &bus) == CO_OK); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
        f.id = 0;
        f.dlc = 2;
        f.data[0] = 1;
        for (id = 0; id <= 255; ++id) { /* 遍历 NMT 目标字节全部取值，包括广播和非法目标 */
            co_status_t expected = (id == 0 || id == node) ? CO_OK : CO_IGNORED; /* 只有广播或本机编号应被接受 */
            f.data[1] = (uint8_t)id;
            CHECK(co_classify_rx(&ctx, &f, &kind) == expected); /* 检查分类输出，避免错误或忽略时残留旧结果 */
            CHECK(kind == (expected == CO_OK ? CO_RX_NMT : CO_RX_NONE)); /* 检查分类输出，避免错误或忽略时残留旧结果 */
        }
        for (id = 0; id <= 8; ++id) { /* 遍历经典 CAN 长度，NMT 只有 DLC=2 合法 */
            f.dlc = (uint8_t)id;
            f.data[1] = 0;
            CHECK(co_classify_rx(&ctx, &f, &kind) == (id == 2 ? CO_OK : CO_ERR_DLC)); /* 故意传入异常条件，确认返回预期错误码 */
        }
        f.dlc = 8;
        for (id = 1; id <= 0x7FF; ++id) { /* 遍历全部非零标准 ID，检查不应误收其它服务 */
            co_rx_kind_t expected = CO_RX_NONE; /* 默认忽略，仅两个本机接收 ID 可改变预期 */
            if (id == 0x600u + node) expected = CO_RX_SDO; /* SDO 请求使用 0x600 加节点号 */
            if (id == 0x200u + node) expected = CO_RX_RPDO1; /* RPDO1 使用 0x200 加节点号 */
            f.id = id;
            CHECK(co_classify_rx(&ctx, &f, &kind) ==
                  (expected == CO_RX_NONE ? CO_IGNORED : CO_OK));
            CHECK(kind == expected); /* 检查分类输出，避免错误或忽略时残留旧结果 */
        }
    }
    f.id = 0x800u;
    CHECK(co_classify_rx(&ctx, &f, &kind) == CO_ERR_CAN_ID); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(kind == CO_RX_NONE); /* 检查分类输出，避免错误或忽略时残留旧结果 */
    f.id = 0x67Fu;
    f.dlc = 9;
    CHECK(co_classify_rx(&ctx, &f, &kind) == CO_ERR_DLC); /* 故意传入异常条件，确认返回预期错误码 */
    f.dlc = 8;
    f.is_extended = 1;
    CHECK(co_classify_rx(&ctx, &f, &kind) == CO_ERR_FRAME_TYPE); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(ctx.state == CO_NMT_INITIALIZATION && bus.calls == 0); /* 检查回调次数，确保非法帧不发送、失败不自动重试 */
    return 0; /* 本组检查全部通过，向 CTest 返回成功 */
}
```

```c
test_routing() 用来测试 co_classify_rx()，也就是检查接收到的 CAN 帧应该被分类为：
- CO_RX_NMT
- CO_RX_SDO
- CO_RX_RPDO1
- CO_RX_NONE
它还会检查错误帧和无关帧是否被正确处理。
```

```c
这些变量的作用是：

- `ctx`：本节点上下文；
- `bus`：发送回调的辅助数据；
- `f`：反复修改、用于接收测试的报文；
- `kind`：接收分类结果；
- `node`、`id`：后面循环测试节点号和 CAN-ID。
```

这里故意让：

```
co_rx_kind_t kind = CO_RX_SDO;
```

而不是初始化为 `CO_RX_NONE`，是为了验证 `co_classify_rx()` 在失败时是否会主动把它清零为 `CO_RX_NONE`。



### **① 初始化节点**

```
CHECK(co_init(&ctx, 1, fake_tx, &bus) == CO_OK);
```

建立节点 1 的上下文：

```c
ctx.node_id = 1;
ctx.tx = fake_tx;
ctx.tx_user = &bus;
```

虽然分类函数不发送报文，但它要求上下文已经有效，所以仍然需要初始化。



### **② 测试空上下文**

```c
CHECK(co_classify_rx(NULL, &f, &kind) == CO_ERR_ARGUMENT);
CHECK(kind == CO_RX_NONE);
```

传入 `NULL` 上下文，函数应该返回参数错误。

同时，`kind` 应该被清为：

```
CO_RX_NONE
```

这说明失败时不会残留旧的分类结果。





### **③ 测试空报文和空输出地址**

```
CHECK(co_classify_rx(&ctx, NULL, &kind) == CO_ERR_ARGUMENT);
CHECK(co_classify_rx(&ctx, &f, NULL) == CO_ERR_ARGUMENT);
```

分别测试：

- `frame` 为空；
- `kind` 为空。

`kind` 是输出参数，函数需要通过它写入分类结果，因此不能传 `NULL`。





### **④ 遍历所有合法节点号**

```
for (node = 1; node <= 127; ++node) {
    CHECK(co_init(&ctx, (uint8_t)node, fake_tx, &bus) == CO_OK);
```

这里让本节点依次取：

```
1、2、3、……、127
```

这样可以确认分类逻辑对所有合法 Node-ID 都正确，而不是只测试节点 1。



### **⑤ 构造 NMT 报文**

```
f.id = 0;
f.dlc = 2;
f.data[0] = 1;
```

NMT 的 CAN-ID 是：

```
0x000
```

NMT 报文必须有两个字节：

```
data[0]：NMT 命令
data[1]：目标节点号
```

这里先把命令设置为 `1`，后面重点测试 `data[1]`。



### **⑥ 测试所有 NMT 目标节点值**

```c
for (id = 0; id <= 255; ++id) {
    co_status_t expected =
        (id == 0 || id == node) ? CO_OK : CO_IGNORED;

    f.data[1] = (uint8_t)id;

    CHECK(co_classify_rx(&ctx, &f, &kind) == expected);
    CHECK(kind == (expected == CO_OK
                       ? CO_RX_NMT
                       : CO_RX_NONE));
}
```

`data[1]` 是一个字节，所以测试它可能出现的全部值：

```
0～255
```

判断规则是：

```
id == 0 || id == node
```

也就是：

- `0`：广播，所有节点都应该接收；
- 当前节点号：发给本节点；
- 其他节点号：与本节点无关，应该忽略。

所以：

```
expected = CO_OK
```

时，分类结果必须是：

```
CO_RX_NMT
```

如果是其他节点号：

```
expected = CO_IGNORED
```

分类输出必须保持：

```
CO_RX_NONE
```

因为无关帧没有被分类成任何服务。



### **⑦ 测试 NMT 的 DLC 要求**

```c
for (id = 0; id <= 8; ++id) {
    f.dlc = (uint8_t)id;
    f.data[1] = 0;

    CHECK(co_classify_rx(&ctx, &f, &kind) ==
          (id == 2 ? CO_OK : CO_ERR_DLC));
}
```

这里依次测试 DLC：

```
0、1、2、3、……、8
```

NMT 要求长度必须**恰好等于 2**：

```
DLC=2 → CO_OK
其他长度 → CO_ERR_DLC
```

注意，这里使用的是 `co_classify_rx()` 对 NMT 服务的专门检查。之前的 `co_frame_validate()` 只检查 DLC 不超过 8，并不会要求 NMT 必须是 2 字节。



### **⑧ 测试所有标准 CAN-ID 的分类**

```c
f.dlc = 8;

for (id = 1; id <= 0x7FF; ++id) {
    co_rx_kind_t expected = CO_RX_NONE;

    if (id == 0x600u + node)
        expected = CO_RX_SDO;

    if (id == 0x200u + node)
        expected = CO_RX_RPDO1;

    f.id = id;

    CHECK(co_classify_rx(&ctx, &f, &kind) ==
          (expected == CO_RX_NONE ? CO_IGNORED : CO_OK));

    CHECK(kind == expected);
}
```

这里遍历所有非零标准 CAN-ID：

```
0x001～0x7FF
```

对于当前节点 `node`，只有两个 ID 特别关注：

```c
0x600 + node  → 本节点 SDO 请求
0x200 + node  → 本节点 RPDO1
```

例如节点 1：

```
0x601 → SDO
0x201 → RPDO1
```

如果 ID 匹配：

```
返回 CO_OK
kind 设置为对应类别
```

如果 ID 不匹配：

```
返回 CO_IGNORED
kind 保持 CO_RX_NONE
```

这个循环验证了整个标准 CAN-ID 范围内，没有错误匹配。



### **⑨ 测试非法 CAN-ID**

```
f.id = 0x800u;
CHECK(co_classify_rx(&ctx, &f, &kind) == CO_ERR_CAN_ID);
CHECK(kind == CO_RX_NONE);
```

`0x800` 超过标准 11 位 CAN-ID 最大值 `0x7FF`，所以返回 CAN-ID 错误。

分类结果也必须清零为：

```
CO_RX_NONE
```



### **⑩ 测试非法 DLC**

```
f.id = 0x67Fu;
f.dlc = 9;
CHECK(co_classify_rx(&ctx, &f, &kind) == CO_ERR_DLC);
```

ID 合法，但 DLC 为 9，超过经典 CAN 的 8 字节限制，因此返回 DLC 错误。



### **⑪ 测试非法帧类型**

```
f.dlc = 8;
f.is_extended = 1;
CHECK(co_classify_rx(&ctx, &f, &kind) ==
      CO_ERR_FRAME_TYPE);
```

扩展帧在本项目中不支持，因此返回帧类型错误。



### **⑫ 确认分类函数没有发送或修改 NMT 状态**

这两项是在检查 `co_classify_rx()` 的**职责边界**。

```
CHECK(ctx.state == CO_NMT_INITIALIZATION &&
      bus.calls == 0);
```

可以拆成两个检查。



**第一部分：状态没有改变**

```
ctx.state == CO_NMT_INITIALIZATION
```

在测试开始时：

```
co_init(&ctx, 1, fake_tx, &bus);
```

`co_init()` 会把状态设为：

```
ctx.state = CO_NMT_INITIALIZATION;
```

之后，测试不断调用：

```
co_classify_rx(&ctx, &f, &kind);
```

即使接收到的是 NMT 报文，`co_classify_rx()` 也只做：

```
判断这是什么类型的帧
把结果写入 kind
返回状态码
```

它不会执行 NMT 命令，例如：

- 不把状态改成 `CO_NMT_OPERATIONAL`；
- 不把状态改成 `CO_NMT_STOPPED`；
- 不处理 `data[0]` 中的命令。

所以测试结束时，状态仍然应该是：

```
CO_NMT_INITIALIZATION
```



**第二部分：没有调用发送回调**

```
bus.calls == 0
```

初始化时虽然把发送函数保存进了上下文：

```
ctx.tx = fake_tx;
ctx.tx_user = &bus;
```

但这只是“保存发送函数”，并没有调用它。

`fake_tx()` 只有在 `co_send()` 内部才会被调用：

```
return ctx->tx(ctx->tx_user, frame);
```

而 `test_routing()` 整个过程中只调用：

```
co_classify_rx(...)
```

没有调用：

```
co_send(...)
```

所以：

```
bus.calls
```

始终是 `0`。

`co_classify_rx()` 不应该因为收到某种帧就自动发送报文。



**第三部分：`&&` 的意思**

```
ctx.state == CO_NMT_INITIALIZATION &&
bus.calls == 0
```

`&&` 表示“并且”。

只有下面两个条件都成立，整个条件才成立：

```
ctx.state == CO_NMT_INITIALIZATION
bus.calls == 0
```

如果其中任何一个不成立，`CHECK` 就会认为测试失败。



**第四部分：`return 0` 的意思**

```
return 0;
```

只有前面的所有 `CHECK` 都通过，程序才会执行到这里。

它表示：

```
test_routing 这组测试全部成功
```

如果中间有任何一个检查失败，宏内部会执行：

```
return 1;
```

立即结束 `test_routing()`，表示失败。

因此，这一段完整表达的是：

```
分类测试结束后：
节点状态仍是初始化状态
发送回调从未被调用
如果两点都满足，则本组测试成功
```







### 总览：

整个函数可以概括为：

```c
准备节点和接收帧
        ↓
测试空指针和错误输入
        ↓
遍历所有节点号
        ↓
测试 NMT 广播和本节点目标
        ↓
测试 NMT DLC 必须为 2
        ↓
遍历所有标准 CAN-ID
        ↓
确认 SDO、RPDO1 和无关帧分类正确
        ↓
确认分类不会发送报文，也不会改变状态
```





## 8：main函数

这里的 `main()` 是整个测试程序的入口。它根据命令行传入的测试组名称，选择执行哪一组测试。

```c
int main(int argc, char **argv)
{
    if (argc != 2) return 1; /* 需要程序名和一个测试组名 */
    if (strcmp(argv[1], "frame") == 0) return test_frame(); /* 运行帧格式测试 */
    if (strcmp(argv[1], "init") == 0) return test_init(); /* 运行初始化测试 */
    if (strcmp(argv[1], "tx") == 0) return test_tx(); /* 运行发送回调测试 */
    if (strcmp(argv[1], "routing") == 0) return test_routing(); /* 运行节点分类测试 */
    return 1; /* 未知测试组名，返回失败 */
}
```

```c
argc → argument count，命令行参数数量
argv → argument vector，命令行参数内容
argv[0] 通常是程序名
argv[1] 是用户传入的第一个参数
👉 这个 main 的作用是：根据用户输入的测试组名称，调用对应的测试函数。
```



#### 1：

```
char *argv[]
```

可以分成三部分：

```
char       字符
*          指针
argv       变量名
[]         数组
```

所以它表示：

> `argv` 是一个数组，数组里的每个元素都是 `char *` 类型的指针。

而 `char *` 可以理解为“指向字符的地址”，通常用来指向一个字符串：

```
char *name = "frame";
```

这里：

```
name ─────► 'f' 'r' 'a' 'm' 'e' '\0'
```

因此：

```
char *argv[]
```

就是“字符串指针数组”：

```
argv[0] ───► "test_co_core.exe"
argv[1] ───► "frame"
argv[2] ───► "extra"
```

访问时：

```
argv[0]   /* 指向第一个字符串 */
argv[1]   /* 指向第二个字符串 */
```

每个 `argv[i]` 的类型都是：

```
char *
```

也就是一个字符串地址。





#### 2：

为什么又写成：char **argv

因为数组传给函数时，会自动变成指向第一个元素的指针：

```c
char *argv[]
        ↓ 传入函数时
char **argv
```









```
void test(int arr[])
```

你可以**先按照字面意思**理解成：

> `test` 这个函数接收一个叫 `arr` 的 `int` 数组。

那怎么给它传进去？

例如：

```c
int main(void)
{
    int a[3] = {10, 20, 30};

    test(a);

    return 0;
}
这里：test(a);
就是：把数组 a 传给 test。
```



```c
这个例子里，调用时直接传数组名 a 就可以：
void test(int arr[])
{
    printf("%d\n", arr[0]); /* 输出 10 */
}

int main(void)
{
    int a[3] = {10, 20, 30};

    test(a); /* 直接写数组名 */

    return 0;
}
把“定义函数”和“调用函数”分开看：
定义：void test(int arr[])
      └── 说明接收什么类型，以及函数内用什么名字

调用：test(a)
      └── 提供实际要访问的数组，不用再写类型或 []
这里 a 会自动转换成首元素地址 &a[0]，由形参 arr 接收：
arr ──► a[0]、a[1]、a[2] 所在的原数组
所以写法上就是 test(a)；实际传递的是首元素地址，没有复制整个数组。
    
但是严格来说，C 语言的函数参数这里有一个特殊规则。

当 [] 出现在函数形参里面：

void test(int arr[])

它会被 C 语言自动调整成：

void test(int *arr)

也就是说：

void test(int arr[])

和：

void test(int *arr)

在函数参数声明中是等价的。
    
最简单的读法就是：

arr 是一个指针，指向一个 int。
 
int main(int argc, char **argv)

第一个形参 int argc：保存命令行参数的数量。
它就是一个普通的 int 变量。在我们正常从终端启动程序时，数量包括程序名。
第二个形参 char **argv：指向一个数组的首元素，这个数组里保存着各段命令行文字的地址。
    
例如终端输入：test_co_core.exe     frame
程序启动时，运行环境会把这些信息交给 main()，不需要我们自己调用 main()：
argc = 2

argv
 │
 ▼
┌─────────────────┐
│ argv[0]：地址 ───┼──► "test_co_core.exe"
├─────────────────┤
│ argv[1]：地址 ───┼──► "frame"
├─────────────────┤
│ argv[2]：NULL    │    结束标记，不计入 argc
└─────────────────┘
所以在函数里面：
argc       /* 数量：2 */
argv[0]    /* 指向程序名字符串 */
argv[1]    /* 指向 "frame" 字符串 */
这两个形参配合起来就是：argc 告诉你有几段文字，argv 让你逐段访问这些文字。
```





#### 3:

我们输入 "test_co_core.exe"  和 "frame" 为什么乖乖填入到数组的[0]和[1]

**因为程序启动时，系统和 C 运行时会帮你整理命令行参数，再交给 `main()`。这不是我们自己写的赋值代码。**

当你在终端输入：

```
test_co_core.exe frame
```

这里首先表示：

```
test_co_core.exe → 要启动哪个程序
frame            → 传给这个程序的附加参数
```

程序启动过程中，C 运行时准备好参数：

```
终端输入命令
      ↓
系统启动 test_co_core.exe
      ↓
C 运行时整理命令行参数
      ├── argc = 2
      ├── argv[0] → 程序名字符串
      └── argv[1] → "frame"
      ↓
进入你写的 main(argc, argv)
```

**顺序是约定好的：**

| 位置      | 内容                     |
| --------- | ------------------------ |
| `argv[0]` | 程序名，具体可能包含路径 |
| `argv[1]` | 第一个附加参数           |
| `argv[2]` | 第二个附加参数，如果有   |

比如输入：

```
test_co_core.exe frame hello
```

就会得到：

```
argc = 3

argv[0] → "test_co_core.exe"
argv[1] → "frame"
argv[2] → "hello"
```

所以，**你写的 `main()` 开始执行时，这些参数就已经准备好了，你只需要读取它们。**





#### 4：

我发现代码没有创建数组argv呀

**你的代码里没有创建这个数组。它由程序启动时的 C 运行时准备好，再把地址传给 `main()`。**

和你之前的例子对比：

```
void test(int *arr)
{
    /* arr 接收外面传进来的地址 */
}

int main(void)
{
    int a[3] = {10, 20, 30}; /* 你创建数组 */
    test(a);                /* 你把地址传给 test */
}
```

这里是：

```
你创建数组 a
    ↓
你调用 test(a)
    ↓
形参 arr 接收地址
```

而带命令行参数的 `main()` 是：

```
C 运行时准备好“字符串指针数组”
    ↓
C 运行时把数量和数组首元素地址交给 main
    ↓
argc 接收数量
argv 接收地址
```

所以：

```
int main(int argc, char **argv)
```

里面的 **`argv` 是指针形参，不是在创建数组**。它指向的数组已经由运行时准备好了。

**你在 `main()` 中使用 `argv[0]`、`argv[1]`，就是通过这个指针访问运行时提供的数组元素。**
