# 1：宏定义

```c
/*1：报文参数*/
#define CO_EMCY_COB_BASE UINT32_C(0x080) // CO_EMCY_COB_BASE 是 EMCY 的 CAN-ID 基址。实际 ID 要加节点号：节点 1 是 0x080 + 1 = 0x081
#define CO_EMCY_DATA_LENGTH 8u           // CO_EMCY_DATA_LENGTH 表示 EMCY 帧固定有 8 个数据字节
#define CO_EMCY_MANUFACTURER_LENGTH 5u   // 其中前 3 字节放错误码和 Error Register，剩下 5 字节是厂商自定义信息，所以长度是 5

/*2：故障码：这些值是放进 EMCY 帧 前两个字节的 Error Code，用来说明“发生了什么故障”。*/
#define CO_EMCY_ERROR_GENERIC UINT16_C(0x1000)         // 0x1000：表示通用错误。
#define CO_EMCY_ERROR_CAN UINT16_C(0x8100)             // 0x8100：CANopen 错误码中属于监控类的范围，用于 CAN 通信故障。
#define CO_EMCY_ERROR_CAN_RX_OVERFLOW UINT16_C(0xFF01) // 0xFF01 到 0xFF06：项目自定义的故障码，分别对应接收溢出、Bus-off、通信超时、ADC、输出和看门狗复位。
#define CO_EMCY_ERROR_BUS_OFF UINT16_C(0xFF02)
#define CO_EMCY_ERROR_COMM_TIMEOUT UINT16_C(0xFF03)
#define CO_EMCY_ERROR_ADC UINT16_C(0xFF04)
#define CO_EMCY_ERROR_OUTPUT UINT16_C(0xFF05)
#define CO_EMCY_ERROR_WATCHDOG_RESET UINT16_C(0xFF06)
/*注意，0xFF03 和 0xFF05 目前只是定义了故障码；对应的通信超时检测和输出故障反馈尚未接入，不能因为有宏就认为检测功能已经完成。*/

/*3：Error Register 位：这些值对应 EMCY 帧的 第 3 个字节，每一位代表一类错误。它们可以按位组合：*/
#define CO_EMCY_REGISTER_GENERIC UINT8_C(0x01)       // 通用错误       0x01  bit0
#define CO_EMCY_REGISTER_CURRENT UINT8_C(0x02)       // 电流错误       0x02  bit1
#define CO_EMCY_REGISTER_VOLTAGE UINT8_C(0x04)       // 电压错误       0x04  bit2
#define CO_EMCY_REGISTER_TEMPERATURE UINT8_C(0x08)   // 温度错误       0x08  bit3
#define CO_EMCY_REGISTER_COMMUNICATION UINT8_C(0x10) // 通信错误       0x10  bit4
#define CO_EMCY_REGISTER_MANUFACTURER UINT8_C(0x80)  // 厂商自定义错误 0x80  bit7
/*通信故障时，通信位是 0x10。代码还会自动加上通用错误位 0x01，合起来就是 0x11，假如此时发生了Bus-off故障，那么EMCY 帧 前三个字节就是0xFF02（错误发）+0x11（Error Register）*/
```



### 1：完整一帧Emcy报文帧

刚刚只讲了前 3 个字节。完整 EMCY 帧是 **8 字节数据**，对应代码里的 `CO_EMCY_DATA_LENGTH = 8`：

| 数据字节 | 长度   | 内容                   |
| -------- | ------ | ---------------------- |
| Byte 0–1 | 2 字节 | Error Code，低字节在前 |
| Byte 2   | 1 字节 | Error Register         |
| Byte 3–7 | 5 字节 | 厂商自定义信息         |

以节点 1 发生 **Bus-off** 为例：

- CAN-ID：`0x080 + 1 = 0x081`
- Error Code：`0xFF02`，按低字节在前发送为 `02 FF`
- Error Register：通用错误 `0x01` + 通信错误 `0x10` = `0x11`
- 当前调用没有提供厂商信息，所以剩余 5 字节填 `00`

完整帧就是：

```c
CAN-ID：0x081
DLC：   8
Data：  02 FF 11 00 00 00 00 00
```

如果调用时提供了厂商自定义数据，例如测试里的 `01 02 03 04 05`，后五字节就会放这些数据：

```
Data：02 FF 11 01 02 03 04 05
```

这里要区分：**DLC=8 表示整帧有 8 个数据字节；其中厂商自定义部分固定占 5 字节**。目前阶段 9 的故障报告调用传入 `NULL`，所以这 5 个字节会是零。

清除故障时，代码会发一帧清除报文：Error Code、Error Register 和厂商信息都为零：

```c
CAN-ID：0x081
DLC：   8
Data：  00 00 00 00 00 00 00 00
```

它不是周期心跳，而是在故障报告或清除时发送的事件报文。





# 2：结构体

```c
typedef struct
{
    co_context_t *node;                                       // node 指向 CANopen 节点上下文。EMCY 模块需要通过它取得 Node-ID，并调用节点的发																														送函数tx
    co_device_od_t *device;                                   // device 指向设备对象字典。报告或清除故障时，需要更新 0x1001:00 Error 																															Register。
    can_frame_t pending_frame;                                // pending_frame 保存暂时没能发送出去的完整 CAN 帧。
    uint8_t pending_is_reset;                                 // 标记待重试帧的类型：0 表示故障报告帧，1 表示故障清除帧
    uint8_t pending;                                          // 是否有待重试的帧：0 表示无待发帧，1 表示 pending_frame 中有待重发报文
    uint8_t active;                                           // 是否有已成功报告、尚未清除的故障：0 表示没有，1 表示有
    uint16_t active_error_code;                               // 记录最近一次成功发送、仍处于活动状态的故障的错误码（EMCY 错误代码），2字节内容
    uint8_t active_error_register;                            // 记录该活动故障对应的Error Register 位，1字节内容
    uint8_t active_manufacturer[CO_EMCY_MANUFACTURER_LENGTH]; // 记录该活动故障的厂商自定义错误字段，5字节内容
} co_emcy_t;
```

**`active = 1`**：故障已上报但未清除

**`active = 0`**：模块当前没有活动故障。

**`pending = 1`**：有一帧 EMCY 报文暂时没发成功，保存在 `pending_frame` 里，等待重试。

**`pending = 0`**：没有等待重试的 EMCY 报文。

| `active` | `pending` | 状态                                                         |
| -------- | --------- | ------------------------------------------------------------ |
| 0        | 0         | 没有活动故障，也没有待重试的 EMCY 报文。                     |
| 1        | 0         | 故障已经上报，尚未清除；没有待重试报文。                     |
| 0        | 1         | 故障报告帧发送失败，正在等待重试；目前没有已成功上报的活动故障。 |
| 1        | 1         | 已有活动故障，同时还有一帧待重试报文。查看 `pending_is_reset`，才能知道待重试的是新故障报告帧还是故障清除帧。 |









### 1：故障报告帧和故障清除帧

**故障报告帧**是节点检测到故障后发送的 EMCY 报文，告诉主站“我发生了某种故障”。例如节点 1 检测到 Bus-off 后，报告帧可能是：

```c
CAN-ID：0x081
DLC：   8
Data：  02 FF 11 00 00 00 00 00
```

其中 `02 FF` 是错误码 `0xFF02`（Bus-off），`11` 是 Error Register 位图，后五字节是厂商自定义信息。

**故障清除帧**是节点确认故障已经恢复后发送的 EMCY 清除报文，通知主站“当前这个 EMCY 故障已清除”。当前代码构造的清除帧数据区全为零：

```c
CAN-ID：0x081
DLC：   8
Data：  00 00 00 00 00 00 00 00
```

它们使用相同的 EMCY CAN-ID。区别在数据区：故障报告帧带有非零错误码和错误寄存器；清除帧用零错误码、零错误寄存器和零厂商信息表示清除。代码也会相应地更新对象字典 `0x1001:00`。





### 2：示例

流程可以这样记：

```c
//情况1
报告帧发送成功
active=1, pending=0
active_error_* 保存已上报的故障内容

系统恢复，清除帧发送成功
active=0, pending=0
active_error_* 清零
```

若发送失败：

```c
//情况2
报告帧发送失败
active=0, pending=1
pending_frame 保存故障报告帧

报告帧重试成功
active=1, pending=0
active_error_* 保存故障内容

清除帧发送成功的情况
调用 co_emcy_clear()
清除帧第一次发送成功
    
清除后：
active=0, pending=0
active_error_code=0
active_error_register=0
active_manufacturer={0, 0, 0, 0, 0}

直接发送成功时，清除帧不会存进 pending_frame。co_emcy_clear() 在局部变量 frame 里组好清除帧，然后直接交给 co_send() 发送。
发送成功后，代码会把 pending 清为 0，表示没有待重试帧。pending_frame 这个结构体字段本身不会被清零；它可能仍是初始化时的全零，也可能残留之前重试过的旧帧内容，但此时这些内容无效，程序不会把它当作待发帧使用。判断它是否有效要看 pending是否=1


//情况3
报告帧发送失败
active=0, pending=1
pending_frame 保存故障报告帧

报告帧重试成功
active=1, pending=0
active_error_* 保存故障内容

清除帧发送失败
active=1, pending=1, pending_is_reset=1
pending_frame 保存清除帧

清除帧重试成功
active=0, pending=0
active_error_* 清零
```



节点 1 的 CAN 接收队列溢出，故障码 `0xFF01`，Error Register 为 `0x11`。故障报告帧是：

```c
CAN-ID 0x081，DLC=8
Data：01 FF 11 00 00 00 00 00
```

系统恢复后发送的清除帧是：

```c
CAN-ID 0x081，DLC=8
Data：00 00 00 00 00 00 00 00
```





##### 情况一：报告帧和清除帧都直接发送成功

报告帧成功后：

```c
active=1, pending=0
active_error_code=0xFF01
active_error_register=0x11
active_manufacturer={0, 0, 0, 0, 0}
```

系统恢复，调用 `co_emcy_clear()`，清除帧直接发送成功后：

```c
active=0, pending=0
active_error_code=0
active_error_register=0
active_manufacturer={0, 0, 0, 0, 0}
```



##### 情况二：报告帧第一次失败，重试成功；清除帧直接成功

报告帧第一次发送失败时：

```c
active=0, pending=1
pending_frame=故障报告帧
```

报告帧重试成功后：

```c
active=1, pending=0
active_error_code=0xFF01
active_error_register=0x11
```

之后系统恢复，清除帧直接发送成功，状态回到：

```c
active=0, pending=0
active_error_code=0
active_error_register=0
```



##### 情况三：报告帧重试成功；清除帧第一次失败，之后重试成功

报告帧第一次发送失败时：

```c
active=0, pending=1
pending_frame=故障报告帧
```

报告帧重试成功后：

```c
active=1, pending=0
active_error_code=0xFF01
active_error_register=0x11
```

系统恢复，但清除帧发送失败时：

```c
active=1, pending=1, pending_is_reset=1
pending_frame=存着全零清除帧
```

这里原故障仍保留在 `active_error_*` 里；清除帧留在 `pending_frame` 等待重试。清除帧重试成功后：

```c
active=0, pending=0
active_error_code=0
active_error_register=0
active_manufacturer={0, 0, 0, 0, 0}
```

三个过程的关键区别是：**故障报告帧失败时待重试的是报告帧；清除帧失败时待重试的是清除帧。**只有 `pending=1` 时，`pending_frame` 才是当前有效的待重试帧。