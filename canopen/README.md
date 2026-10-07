# CANopen 协议层

放置 CANopen 协议栈、对象字典和通信适配代码。

## 阶段 1 阅读顺序

1. `include/co_types.h`：帧、状态码、节点上下文和发送函数指针。
2. `include/co_core.h`：调用约定。
3. `src/co_core.c`：初始化、帧校验、发送和接收地址分类。
4. `../tests/test_co_core.c`：用模拟传输回调验证接口。

协议核只依赖 C 标准头文件。硬件适配放在 `firmware/`，当前没有队列、动态内存或真实硬件操作。

## 接口边界

- `co_init()`：Node-ID 限定为 1～127，保存回调及用户指针，初始状态为 Initialization；不发送 Boot-up。
- `co_frame_validate()`：接受 0～0x7FF 的标准经典 CAN 数据帧，DLC 为 0～8；拒绝扩展帧、远程帧和 FD 帧。NULL 指针与 DLC=0 是两种不同情况。
- `co_send()`：先校验，再调用传输回调一次；透传忙/失败结果，不阻塞等待或自动重试。
- `co_classify_rx()`：识别本节点 SDO 请求、RPDO1，以及本节点或广播 NMT。其它 ID 忽略；NMT 检查 DLC=2 后才读取目标节点。返回 CO_OK 仅代表分类成功。

帧的 `id` 是实际 CAN-ID，不是对象字典中带有效位等配置标志的 32 位 COB-ID 参数。不能把配置标志原样放入 `id`。

SDO DLC=8、RPDO1 DLC=1、DO 保留位、NMT 命令有效性和运行状态限制，在后续对应协议阶段检查。当前分类器不会响应 SDO、切换 NMT 状态或更新输出。

调用者须先初始化上下文，并串行调用接口。发送回调必须非阻塞；若实现异步队列，须在回调返回前复制帧，不能保存传入帧指针。CO_OK 仅表示传输层接受，不表示总线 ACK 成功。

阶段 1 的两个头文件、核心源文件和测试源文件已补充中文学习注释，按项目约定保存为 GB2312（代码页 936）。编辑器若显示乱码，请以 GB2312/GBK 编码重新打开；后续修改保持该编码。

## 阶段 2 实际对象表

新增 `include/co_device_od.h`、`src/co_device_od.c`：静态存储 36 个条目，绑定 DI、DO、两路 AI、错误寄存器、心跳周期、身份与固定 PDO 参数。应用先初始化 `co_device_od_t`，通过 `device.table` 使用通用查找/读写接口；通过输入更新与输出查询接口对接后续硬件层。

学习顺序：`co_device_od.h` → `co_device_od.c` → `tests/test_co_device_od.c` → EDS。代码继续使用 GB2312（代码页 936），Markdown/Python 为 UTF-8，EDS 为 ASCII。

初始化后的设备包含自引用指针，禁止按值复制；须串行访问。INTEGER16 范围检查使用 int64_t，EDS 类型编号与内部类型枚举不是同一套编号。详细对象定义见 [对象字典草案](../docs/对象字典草案.md)。

2026-09-23：阶段2基线已冻结，包含6423 BOOLEAN；AI对象值为原始ADC乘8，PDO事件周期默认0，抑制时间固定0。相关协议设计见 `../docs/EDS草案.md` 和 `../docs/COB-ID与PDO映射.md`。
