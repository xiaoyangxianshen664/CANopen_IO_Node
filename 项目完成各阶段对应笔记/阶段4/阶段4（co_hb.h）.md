## 解析

```c
typedef struct
{
    co_context_t *node;
    co_device_od_t *device;
    uint32_t elapsed_ms;
    uint32_t observed_writes;
} co_hb_t;
```

这个结构体是 **Heartbeat Producer 的运行对象**。它把“给哪个节点发心跳、从哪个对象字典读取周期、已经计时多久、是否检测到周期被重新写入”放在一起。

可以先创建三个对象：

```c
co_context_t node = {0};
co_device_od_t device = {0};
co_hb_t heartbeat = {0};
```

初始化后大致是：

```c
co_init(&node, 2, tx, &bus);
co_device_od_init(&device, 2, NULL);
co_hb_init(&heartbeat, &node, &device);
```

此时 `heartbeat` 里面的各个成员可以理解为：

```c
heartbeat.node    = &node						//这是一个指针，指向阶段 1 建立的节点上下文
heartbeat.device  = &device						//这是一个指针，指向阶段 2 建立的设备对象字典
heartbeat.elapsed_ms = 0
heartbeat.observed_writes = 当前 0x1017 的写入次数
```





### 1. `co_context_t *node`

```
co_context_t *node;
```

这是一个指针，指向阶段 1 建立的节点上下文。

例如：

```c
co_context_t node = {0};
co_hb_t heartbeat;

heartbeat.node = &node;
```

它让 Heartbeat 模块能够访问：

```c
node.node_id    // 节点号，例如 2
node.state      // 当前 NMT 状态，例如 CO_NMT_OPERATIONAL
node.tx         // 发送回调
node.tx_user    // 发送回调的辅助数据
```

发送心跳时需要用到这些信息：

```c
frame.id = 0x700 + heartbeat.node->node_id;
frame.data[0] = (uint8_t)heartbeat.node->state;
co_send(heartbeat.node, &frame);
```

如果：

```c
node.node_id = 2;
node.state = CO_NMT_OPERATIONAL;
```

那么发送的心跳就是：

```
CAN-ID = 0x702
DLC    = 1
Data   = 05
```

这里的 `05` 就是 Operational 状态值。

注意：

```c
heartbeat.node
```

保存的是 `node` 的地址，不是复制一份完整的 `co_context_t`。所以后面 NMT 修改：

```
node.state = CO_NMT_STOPPED;
```

Heartbeat 下次读取：

```
heartbeat.node->state
```

就能得到新的 `CO_NMT_STOPPED`。



### 2. `co_device_od_t *device`

```
co_device_od_t *device;
```

这是一个指针，指向阶段 2 建立的设备对象字典。

例如：

```c
co_device_od_t device = {0};

heartbeat.device = &device;
```

Heartbeat 不自己保存一个周期变量，而是通过这个对象字典读取：

```
0x1017:00 = Producer Heartbeat Time
```

例如当前对象值是：

```c
0x1017:00 = 1000
```

表示每 1000 ms 发送一次心跳。

`co_hb_process()` 内部会调用：

```c
co_device_od_get_heartbeat(
    heartbeat.device,
    &period,
    &writes
);
```

得到：

```
period = 1000;
```

然后用这个周期决定是否到期。

如果主站以后通过 SDO 把 `0x1017:00` 改成：

```
500
```

Heartbeat 模块下一次处理时，就会使用 500 ms，而不是旧的 1000 ms。

所以这里的关系是：

```c
Heartbeat 模块
      │
      │ 通过 device 指针读取
      ▼
设备对象字典 0x1017:00
      │
      ▼
当前心跳周期
```







### 3. `uint32_t elapsed_ms`

```c
uint32_t elapsed_ms;
```

它表示：

> 从上一次成功发送 Heartbeat，或者从计时重新开始，到现在累计经过了多少毫秒。

例如对象字典中的周期是：

```c
0x1017:00 = 1000 ms
```

程序不断调用：

```c
co_hb_process(&heartbeat, 200);
```

每次代表经过 200 ms：

```c
第一次：elapsed_ms = 200
第二次：elapsed_ms = 400
第三次：elapsed_ms = 600
第四次：elapsed_ms = 800
第五次：elapsed_ms = 1000
```

当：

```
elapsed_ms >= period
```

就发送一次 Heartbeat。

发送成功后，计时器重新计算。比如累计多了 50 ms：

```
elapsed_ms = 1050
period = 1000
```

发送成功后保留余量：

```
elapsed_ms = 1050 % 1000
            = 50
```

这样不会因为每次调用不正好落在周期边界而越来越偏。

可以把它看成一个软件计时器：

```
elapsed_ms：当前已经走了多少毫秒
period：    需要走多少毫秒才发送一次
```





### 4. `uint32_t observed_writes`

```c
uint32_t observed_writes;
```

这个变量记录的是：

> Heartbeat 模块上一次观察到的 `0x1017:00` 成功写入次数。

对象字典中每个实际数据都有一个：

```
writes
```

例如：

```
0x1017:00 当前值 = 1000
0x1017:00 writes = 0
```

初始化 Heartbeat 时：

```
heartbeat.observed_writes = 0;
```

如果主站把周期写成 500：

```
0x1017:00 = 500
writes    = 1
```

Heartbeat 下一次执行时发现：

```
writes != heartbeat.observed_writes
```

也就是：

```
1 != 0
```

于是知道周期对象刚刚被写过，就执行：

```
heartbeat.observed_writes = writes;
heartbeat.elapsed_ms = 0;
```

表示：

> 从修改心跳周期这一刻重新开始计时。

这个成员还可以检测“同值重写”。

例如原来：

```
周期 = 500
writes = 1
```

主站再次写入同一个值：

```
周期 = 500
writes = 2
```

虽然周期数值没有变化，但 `writes` 变化了：

```
2 != 1
```

因此 Heartbeat 仍然会重新开始计时。

这正是阶段 2 的写入计数和阶段 4 配合的地方：

```
co_od_write()
    ↓
写入 0x1017:00
    ↓
writes 加 1
    ↓
Heartbeat 发现 writes 变化
    ↓
elapsed_ms 清零
```



整个结构体可以用这张图记忆：

```c
co_hb_t heartbeat
├── node
│   └── 当前节点号、NMT 状态、发送回调
│
├── device
│   └── 对象字典，尤其是 0x1017:00
│
├── elapsed_ms
│   └── 当前已经累计的计时时间
│
└── observed_writes
    └── 上一次观察到的 0x1017 写入次数
```

因此，Heartbeat 发送一次报文时，完整的数据来源是：

```c
heartbeat.node->node_id
    └── 决定 CAN-ID：0x700 + Node-ID

heartbeat.node->state
    └── 决定数据字节：当前 NMT 状态

heartbeat.device
    └── 提供周期：0x1017:00

heartbeat.elapsed_ms
    └── 判断是否到期

heartbeat.observed_writes
    └── 检测周期是否被重新写入
```