> 2026-09-23：本文已按 [阶段2冻结基线-v1.1](阶段2冻结基线-v1.1.md) 和 [代码修正对照](阶段2代码修正前后对照-学习版.md) 同步修订正文。阶段2对象表及 EDS 已冻结；下文 NMT、Heartbeat、SDO、PDO、EMCY 行为属于后续实现要求，不能视为已完成。

# CANopen EDS 字段草案

```c
Electronic Data Sheet
```

中文通常叫：电子数据表

在 CANopen 中，EDS 是描述设备能力的文件，通常使用：.eds作为扩展名，它类似于设备的“说明书”或“配置清单”，告诉上位机：

```c
设备名称
厂商信息
Node-ID 和波特率支持
有哪些对象字典对象
每个对象的 Index/Sub-index
数据类型和长度
读写权限
有哪些 PDO
PDO 的 COB-ID 和映射
```

例如我们的 EDS 会描述：

```
0x6000:01：4 路数字输入
0x6200:01：4 路数字输出
0x6401:01：模拟输入 AI1
0x6401:02：模拟输入 AI2
0x1017:00：Heartbeat 周期
```

上位机导入 EDS 后，就知道如何识别和操作 STM32 节点。

可以类比项目 1：

```
项目 1 的寄存器表 + 通信参数 + 设备说明
≈ CANopen 的 EDS 文件
```

但 EDS 不是 STM32 固件本身，也不是运行时数据。它是提供给 CANopen 主站工具使用的**设备描述文件**。







状态：本文是EDS设计与学习说明；可导入文件为 [CANopen_IO_Node.eds](../eds/CANopen_IO_Node.eds)。阶段2已生成并冻结v1.1，14索引、36条目；第三方离线导入已通过，GUI主站及实机互通待后续集成验证。

| 区段 | 要准备的字段 |
|---|---|
| FileInfo(文件信息) | FileName(文件名)、FileVersion（文件版本）、FileRevision（文件修订版）、EDSVersion（EDS 版本）、Description（描述）、创建/修改日期 |
| DeviceInfo（设备信息） | VendorName/Number（厂商名称/编号）、ProductName/Number（产品名称/编号）、RevisionNumber（产品修订号）、BaudRate_500（是否支持 500 kbit/s 波特率）、SimpleBootUpSlave（是否为简单启动从节点）、NrOfRXPDO（接收 PDO 数量）、NrOfTXPDO（发送 PDO 数量） |
| MandatoryObjects（必需对象） | SupportedObjects （支持的对象数量）和从 1 开始的对象索引列表：0x1000、0x1001、0x1018 |
| OptionalObjects（可选对象） | 0x1017、PDO通信/映射对象、0x6000、0x6200、0x6401、0x6423 |
| ManufacturerObjects（厂商自定义对象） | 暂无；0x2000 仅预留，未定义前不导出 |
| 各对象及子索引 | ParameterName（参数名称）、ObjectType（对象类型）、DataType（数据类型）、AccessType（访问类型）、DefaultValue（默认值）、PDOMapping（是否允许 PDO 映射） 等适用字段 |

当前文件关键字段：FileVersion=1、FileRevision=1、EDSVersion=4.0、Granularity=0。MandatoryObjects=3、OptionalObjects=11、ManufacturerObjects=0，总计14个索引、36条目。1800/1801实现00、01、02、03、05，SubNumber=5包含00；:00的最大子索引值同为5，但含义不同。BOOLEAN的EDS类型号为0x0001，INTEGER16为0x0003；它们不等于C内部枚举值。

几个区段的中文意思：

```
FileInfo             = 文件信息
DeviceInfo           = 设备信息
MandatoryObjects     = 必须支持的对象
OptionalObjects      = 可选对象
ManufacturerObjects  = 厂商自定义对象
```

其中：`NrOf` 是 `Number of` 的缩写，意思是“……的数量”：



MandatoryObjects 的“必须”指协议要求，不表示本项目用到的全部对象。列表格式为 SupportedObjects=N 和 1=0x1000 等；不能写成 0x1000=1。

- 产品名：CANopen IO Node；Vendor-ID 未确认，不能冒用已分配的厂商号。
- 500 kbit/s；1 个 RPDO、2 个 TPDO；只支持本项目声明的协议子集。
- Node-ID=1 是本项目默认运行配置，不随意加入 DeviceInfo 的非标准 NodeID 字段。
- COB-ID 默认值应支持适用的 $NODEID 表达式，确保与节点默认配置一致。
- 0x1018:00 是 UNSIGNED8 计数；RECORD 是父对象结构，不是子索引 00 的数据类型。
- PDO映射固定const，Granularity=0；两组事件周期 :05 在Pre-op可写，默认0；抑制时间 :03 固定0/const，状态限制另在文档说明。
- 0x6401为INTEGER16，原始ADC乘8，范围0..32760；Device Type=0x00070191；Identity保留开发占位，VendorNumber=0，不代表已注册厂商。

验收：工具能导入，能读出对象和映射；实际 SDO 类型、长度、权限、默认值和 PDO 帧与 EDS 一致。已有11/11 CTest与python-canopen 2.4.1在Node-ID 1/2/127离线导入通过记录；实际SDO/PDO报文和硬件尚待实现验证。





# 解析说明：Codex

## 1：`MandatoryObjects` 是什么

```
MandatoryObjects = 必须对象
```

表示 CANopen 规范要求设备必须描述的对象。

例如：

```
0x1000：Device Type
0x1001：Error Register
0x1018：Identity Object
```

但“必须”只表示**协议要求必须有**，不表示我们的设备只支持这三个对象。

项目 2 还会有：

```
0x1017
0x6000
0x6200
0x6401
PDO 参数对象
```

这些放到 OptionalObjects 或其他对象列表中。



## 2.：`SupportedObjects=N` 是什么

EDS 中需要告诉工具：

```
我总共支持多少个对象
```

例如：

```
SupportedObjects=3
1=0x1000
2=0x1001
3=0x1018
```

这里：

```
SupportedObjects=3
```

表示后面有 3 个对象。

```
1=0x1000
```

表示第 1 个对象是 `0x1000`。

不能写成：

```
0x1000=1
```

因为 EDS 要的是：

```
序号 = 对象索引
```

而不是：

```
对象索引 = 序号
```



## 3：产品和厂商信息

```
ProductName = CANopen IO Node
```

表示设备产品名称。

```
Vendor-ID 未确认
```

表示我们还没有正式分配的 CANopen 厂商编号，所以不能随便把 GitHub 用户名当成 Vendor-ID。

也就是说：

```
VendorName 可以写产品开发者名称
VendorNumber 必须使用真实合法厂商编号
```

本版VendorNumber及1018:01使用0作为开发占位；产品号1、修订号0x00010000、序列号0，均不表示量产身份。



## 4：波特率和 PDO 数量

草案说明：

```
500 kbit/s
1 个 RPDO
2 个 TPDO
```

对应我们的项目：

```
RPDO1：接收 4 路数字输出
TPDO1：发送 4 路数字输入
TPDO2：发送 2 路模拟输入
```

EDS 要把这些能力告诉主站工具，让工具知道设备支持多少 PDO。





## 5. Node-ID 为什么不直接写进 DeviceInfo

我们的默认 Node-ID 是：

```
Node-ID = 1
```

但 Node-ID 通常属于：

```
设备运行配置
```

而不是设备固有身份。

以后同一块设备可能配置成：

```
Node-ID = 1
Node-ID = 2
Node-ID = 10
```

所以 EDS 中不随便加入一个非标准的 `NodeID` 字段。Node-ID 可以由拨码、参数或主站配置决定。

## 6. $NODEID 是什么

EDS 中的 COB-ID 通常不能只写死为 `0x181`，因为不同节点的 ID 不同。

例如 TPDO1 的默认公式是：

```
0x180 + Node-ID
```

在 EDS 中可以表示成类似：

```
$NODEID+0x40000180
```

这样：

```
Node-ID=1 → COB-ID参数0x40000181 → 总线CAN-ID 0x181
Node-ID=2 → COB-ID参数0x40000182 → 总线CAN-ID 0x182
```

同一份 EDS 才能描述不同 Node-ID 的设备。

## 7. `0x1018:00` 为什么不是 RECORD

`0x1018` 整体是一个记录对象：

```
0x1018 = Identity Object
```

它下面有多个子索引：

```
0x1018:00 = 子索引数量
0x1018:01 = Vendor-ID
0x1018:02 = Product Code
0x1018:03 = Revision
0x1018:04 = Serial Number
```

因此：

```
0x1018 整组的 ObjectType 可以是 RECORD
0x1018:00 的 DataType 应是 UNSIGNED8
```

不能因为整个组是 RECORD，就把 `:00` 也写成 RECORD。

`0x1018:00` 的值通常是：

```
4
```

表示后面有 4 个有效成员。

## 8. PDO 为什么固定只读

我们的第一版不做动态 PDO 映射，所以：

```
0x1600、0x1A00、0x1A01映射条目：const固定常量
```

主站不能随意修改：

```
TPDO1 到底发送什么
TPDO2 的字节怎么排列
```

但部分 PDO 定时参数可以在 Pre-operational 状态修改，例如：

```
事件定时器 :05（默认0，rw）
抑制时间 :03 固定0/const，不在可写范围
```

所以要区分：

```
PDO 映射：固定，只读
PDO 定时参数：可以有限度修改
```

## 9. 0x6401 与 EDS 当前已确认的内容

两路ADC原始输入均为0..4095，对象类型INTEGER16，占2字节；对象值为原始值乘8（0..32760），bit15=0、bit14..3为有效数据、bit2..0=0。暂不转换为电压或温度。

Device Type=0x00070191（DI/DO/AI，M=0），Identity为开发占位。EDS v1.1已冻结并完成离线导入；冻结范围为数据模型，不代表服务实现、实机验收或CiA认证。

## 10. 最后的“验收”在验什么

EDS 不是写完就算完成，还要用 CANopen 工具导入并核对：

```
工具能否打开 EDS
对象是否能显示
Index/Sub-index 是否正确
数据类型是否一致
长度是否一致
读写权限是否一致
默认值是否一致
PDO 映射是否一致
实际 CAN 报文是否符合 EDS
```

例如 EDS 写：

```
TPDO2 byte0~1 = AI1
TPDO2 byte2~3 = AI2
```

实际 STM32 就必须真的发送：

```
byte0~1：AI1
byte2~3：AI2
```

不能文档写一套，程序实现另一套。

所以这份草案的核心作用是：

```
表格：列出 EDS 需要填写的栏目
说明：规定每个栏目应该如何填写
验收：确认 EDS、对象字典和真实报文完全一致
```

最终可以把 EDS 理解成：

> **给 CANopen 上位机看的“设备说明书”和“自动识别配置文件”。**







# 解析表格：gpt

你这份 **CANopen EDS 字段草案**，可以把它理解成：

> **给 CANopen 工具看的“设备说明书/数据库文件”。**

它本身**不是 CANopen 协议运行代码**，也不是对象字典本身，而是把你的对象字典、PDO、设备信息等，用标准化格式描述出来，让 CANopen 配置工具知道：

> “这个 STM32 节点有什么对象？每个对象是什么类型？能不能读写？能不能映射到 PDO？默认值是多少？”





## 1：先理解 EDS 到底是什么

项目最终结构如下（对象字典已实现，其余协议服务按阶段推进）：

```c
CANopen Node
    │
    ├── NMT
    ├── Heartbeat
    ├── SDO
    ├── PDO
    ├── EMCY
    │
    └── Object Dictionary
            │
            ├── 0x1000 Device Type
            ├── 0x1001 Error Register
            ├── 0x1018 Identity
            ├── 0x1017 Heartbeat
            ├── 0x1400 RPDO1 Communication
            ├── 0x1600 RPDO1 Mapping
            ├── 0x1800 TPDO1 Communication
            ├── 0x1A00 TPDO1 Mapping
            ├── 0x6000 DI
            ├── 0x6200 DO
            └── ...
```

**对象字典是设备实际运行的数据结构。**

而 EDS 是：

```
        EDS
         │
         ▼
告诉 PC/CANopen 工具：
         │
         ├── 这个设备叫什么？
         ├── 厂商是谁？
         ├── 有哪些对象？
         ├── 0x1000 是什么类型？
         ├── 0x1017 能不能写？
         ├── TPDO1 映射了什么？
         ├── 默认 COB-ID 是多少？
         └── 每个数据长度是多少？
```

所以可以简单记：

> **对象字典 = 设备实际提供的数据接口**
>
> **EDS = 对象字典的“机器可读说明书”**





## 2：为什么要专门搞 EDS？

假设你的 STM32 已经实现：

```
0x1017: Heartbeat Time
```

类型：

```
UNSIGNED16
```

权限：

```
rw
```

默认：

```
1000 ms
```

如果没有 EDS，PC 工具并不知道这些东西。

你只能人工告诉工具：

> 这个设备有 0x1017，类型 UNSIGNED16，可以读写，默认 1000。

有了 EDS，工具直接读取：

```
0x1017
DataType = UNSIGNED16
AccessType = rw
DefaultValue = 1000
```

于是工具就知道怎么操作你的设备。

这就是 EDS 的核心价值。



## 3： FileInfo 

你写：

```c
FileInfo
    FileName
    FileVersion
    FileRevision
    EDSVersion
    Description
    创建/修改日期
```

这是：

> **“这份 EDS 文件自身的信息。”**

例如可以理解成文件头：

```
FileName = CANopen_IO_Node.eds
FileVersion = 1
FileRevision = 1
EDSVersion = 4.0
Description = CANopen IO Node
```

它描述的是：

> **这份 EDS 文件是谁、什么版本、什么时候生成的。**

不是描述 STM32 的 CANopen 对象。

所以：

```
FileInfo
```

和：

```
DeviceInfo
```

要分开。





## 4：DeviceInfo

你的：

```c
DeviceInfo
    VendorName/Number
    ProductName/Number
    RevisionNumber
    BaudRate_500
    SimpleBootUpSlave
    NrOfRXPDO
    NrOfTXPDO
```

描述的是：

> **这个 CANopen 设备本身是谁。**

例如：

```
ProductName = CANopen IO Node
BaudRate_500 = 支持 500 kbit/s
NrOfRXPDO = 1
NrOfTXPDO = 2
```

这里就和你前面确定的项目基线对应起来了：

```
STM32F429
CAN 500 kbit/s
1 RPDO
2 TPDO
```





## 5：为什么 Vendor-ID 不能乱写？

你这里特别写：

> Vendor-ID 未确认，不能冒用已分配的厂商号。

这个非常重要。

因为 Vendor ID 不是：

```
我随便写个 1
```

这么简单。

它属于 CANopen 设备身份体系的一部分。

你现在是自己的项目：

```
CANopen IO Node
```

但还没有正式确定厂商身份，所以：

> **本版填写0并注明开发占位，不冒用已分配厂商号。**

这属于 EDS 身份信息，不应该为了让 EDS “看起来完整”而乱填。





## 6：MandatoryObjects 是什么？

这是你这份草案里比较值得理解的一部分。

你写：

```
MandatoryObjects

SupportedObjects
    1 = 0x1000
    2 = 0x1001
    3 = 0x1018
```

它表达的意思是：

> **这个设备的对象字典里面有哪些对象。**

这里：

```
SupportedObjects = 3
```

表示：

> 有 3 个对象。

然后：

```
1 = 0x1000
2 = 0x1001
3 = 0x1018
```

表示：

```
第1个对象 → 0x1000
第2个对象 → 0x1001
第3个对象 → 0x1018
```

所以你特别强调：

> 不能写成 `0x1000=1`

是正确的。

因为这里的字段语义是：

```
序号 → 对象索引
```

而不是：

```
对象索引 → 序号
```



## 7： 为什么 0x1000、0x1001、0x1018 是 Mandatory？

这三个属于 CANopen 基础设备信息：

```
0x1000
Device Type

0x1001
Error Register

0x1018
Identity Object
```

可以粗略理解：



### 0x1000：设备类型

告诉主站：

> “我是什么类型的 CANopen 设备？”

------



### 0x1001：错误寄存器

提供一个基础错误状态信息。

例如某些错误状态可以通过这里反映。

------



### 0x1018：Identity

这是设备身份信息：

```
Vendor-ID
Product Code
Revision Number
Serial Number
```

所以它实际上就是：

> **设备身份证。**

你前面特别强调：

```
0x1018:00 是 UNSIGNED8 计数
```

这个也是在避免一个很容易犯的错误：

```
0x1018
    └── 00
```

这里：

```
0x1018
```

是父对象：

```
RECORD
```

而：

```
0x1018:00
```

才是一个具体子索引。

因此不能因为父对象是 `RECORD`，就把 `0x1018:00` 的 DataType 也写成 RECORD。

------



## 8.：OptionalObjects 又是什么？

你列：

```
0x1017
PDO 通信/映射对象
0x6000
0x6200
0x6401
```

意思是：

> 这些不是所有 CANopen 设备都必须实现的基础对象，而是**你的这个具体设备选择实现的对象**。

例如：

```
0x1017
Heartbeat Producer Time
```

项目规划实现Heartbeat，因此阶段2先建立其周期对象；阶段4再实现定时发送。

------





## 9.：PDO 为什么会出现一堆对象？

这是理解 EDS 最关键的地方之一。

你已经知道：

```
TPDO1
TPDO2
RPDO1
```

但实际上 CANopen 不只是一个：

```
0x181
```

这么简单。

一个 PDO 通常有两套对象：

```
通信参数
Communication Parameter

        +

映射参数
Mapping Parameter
```

例如 TPDO1：

```
0x1800
TPDO1 Communication Parameter

        +
        
0x1A00
TPDO1 Mapping Parameter
```

所以你的 EDS 需要描述：

```
0x1800
0x1A00
```

而不是只写：

```
TPDO1 = 0x181
```

------





## 10： 通信参数和映射参数到底区别在哪？

这个你之前刚好在学 PDO，所以这里可以串起来。

假设 TPDO1：

```
COB-ID参数 = 0x40000181（总线CAN-ID为0x181）
```

它属于：

```
0x1800
```

这是：

> **“这个 PDO 怎么发？”**

比如：

```
COB-ID
Transmission Type
Inhibit Time
Event Timer
```

而：

```
0x1A00
```

是：

> **“这个 PDO 里面装什么？”**

比如：

```
本项目TPDO1：0x6000:01
本项目TPDO2：0x6401:01、0x6401:02
本项目没有0x6000:02
```

于是：

```
0x1800
    ↓
TPDO1 怎么发送

0x1A00
    ↓
TPDO1 里面装什么
```

这就是为什么你的 EDS 需要同时描述：

```
PDO 通信对象
PDO 映射对象
```

------





## 11.：你写的“PDO 映射固定只读”是什么意思？

这是一个很重要的设计决定。

比如：

```
TPDO1
```

你规定它固定映射：

```
TPDO1
└── DI 数据
```

那么主站不能通过 SDO 随意修改：

```
0x1A00
```

的映射。

也就是说：

```
0x1A00的各实现子项
AccessType = const
```

或者对应的固定映射属性。

这样你的设备行为更加确定：

```
TPDO1
一定发送这些数据
```

而不是：

```
主站今天把 TPDO1 映射成 DI
明天改成 AI
后天又改成别的东西
```

对于你这个第一版工业 IO Node，**固定 PDO 映射是一个很合理的简化设计**。

------





## 12： “TPDO 事件周期在 Pre-operational 可写”又是什么？

这个和你之前学的 CANopen 状态机直接联系起来。

你规定：

```
TPDO事件周期0x1800:05、0x1801:05（抑制时间不可写）
```

可以在：

```
Pre-operational
```

状态修改。

但进入：

```
Operational
```

以后，就按照运行状态限制访问。

所以：

```
EDS
```

负责描述：

> **这个对象是什么、类型是什么、权限是什么。**

而：

```
CANopen 状态机文档
```

负责进一步说明：

> **什么状态下允许访问。**

你这里写：

> 状态限制另在文档说明

是合理的。

不要试图把所有状态机行为都硬塞进一个普通 EDS 字段里。

------





## 13：各对象及子索引字段是 EDS 的核心

你最后这行：

```
各对象及子索引

ParameterName
ObjectType
DataType
AccessType
DefaultValue
PDOMapping
```

实际上就是：

> **把你的对象字典翻译成 EDS。**

例如假设：

```
0x1017:00
```

那么 EDS 就需要描述：

```
ParameterName = Heartbeat Producer Time
ObjectType = VAR
DataType = UNSIGNED16
AccessType = rw
DefaultValue = ...
PDOMapping = 0
```

而另一个对象：

```
0x6000:01
```

可能是：

```
ParameterName = Digital inputs 1 to 4
ObjectType = VAR（实际EDS填7）
DataType = UNSIGNED8（实际EDS填0x0005）
AccessType = ro
PDOMapping = 1
```

这里的核心关系就是：

```
对象字典
   ↓
对象索引
   ↓
子索引
   ↓
数据类型
   ↓
访问权限
   ↓
默认值
   ↓
PDO 能否映射
```

这些最终都要和你的 STM32 实际实现一致。

------



## 14.：为什么你最后的“验收标准”非常重要？

你写：

> 工具能导入，能读出对象和映射；实际 SDO 类型、长度、权限、默认值和 PDO 帧与 EDS 一致。

这个其实就是整个 EDS 阶段最核心的验收逻辑。

因为最危险的情况不是：

```
EDS 导入失败
```

而是：

```
EDS 成功导入
        ↓
但是 EDS 写错了
        ↓
工具以为 0x1017 是 UNSIGNED32
实际上 STM32 是 UNSIGNED16
```

这就会出现：

> **“文件看起来没问题，但实际设备通信出问题。”**

所以你的验收必须是：

```
EDS
 │
 ├── ObjectType
 ├── DataType
 ├── AccessType
 ├── DefaultValue
 └── PDO Mapping
          │
          ▼
       STM32实际实现
```

两边必须一致。

------





## 15： 你现在这份草案可以看成三层

我建议你把整个 EDS 理解成下面三层：

```
第一层：设备身份
────────────────────
FileInfo
DeviceInfo

“我是谁？”
```

↓

```
第二层：我有哪些对象
────────────────────
MandatoryObjects
OptionalObjects
ManufacturerObjects

“我提供哪些 CANopen 数据接口？”
```

↓

```
第三层：每个对象具体是什么
────────────────────
Object
 ├── ParameterName
 ├── ObjectType
 ├── DataType
 ├── AccessType
 ├── DefaultValue
 └── PDOMapping

“这个接口具体怎么用？”
```

这三层理解了，EDS 就不会再显得很杂。

------





## 16： 和你现在整个 CANopen 项目的关系

你目前实际上是在逐层搭：

```
CANopen IO Node
│
├── CAN 总线基线
│     ├── 500 kbit/s
│     ├── 11-bit CAN
│     └── DLC ≤ 8
│
├── NMT / 状态机
│
├── Heartbeat
│
├── Object Dictionary
│     ├── 0x1000
│     ├── 0x1001
│     ├── 0x1018
│     ├── 0x1017
│     ├── PDO 参数
│     ├── 0x6000
│     ├── 0x6200
│     └── 0x6401
│
├── SDO
│     └── 通过索引/子索引访问对象
│
├── PDO
│     └── 根据 Mapping 快速传输过程数据
│
└── EDS
      └── 把上面的设备信息告诉 PC/CANopen 工具
```

所以你现在做 EDS **不是在额外学习一个新的通信协议**。

而是在把你前面已经设计好的：

> **对象字典 + SDO + PDO + 设备身份**

整理成一个标准化的“设备描述文件”。

------

### 最后给你一个非常直观的类比

可以把你的 STM32 CANopen 节点想成一个人：

```
0x1018
↓
身份证

0x1000
↓
职业/设备类型

0x1017
↓
心跳周期

0x6000
↓
我当前看到的输入

0x6200
↓
我要控制的输出

0x6401
↓
模拟量数据

SDO
↓
“你告诉我你的某项信息”

PDO
↓
“我直接把实时数据发给你”

EDS
↓
“这是我的产品说明书，里面告诉电脑：
  我是谁、有哪些数据、数据是什么类型、怎么访问。”
```

所以你现在这份草案的定位非常清楚：

> 阶段0确定EDS内容；阶段2已生成并冻结EDS v1.1、完成第三方离线导入；后续集成阶段验证GUI主站导入及真实CAN通信。



## 总览：

这一步其实是在确认两件事：

1：**我们的“设备说明书”别人能不能读懂，**

2：**以及代码和说明书能不能确定为同一个版本。**

先把三个东西分开：

```c
三份规范 PDF
    └── 写 CANopen 设备时要遵守的规则

我们的 C 代码
    └── 设备实际有哪些对象、数据怎么保存和读写

我们的 EDS 文件
    └── 给上位机软件看的“设备说明书”
```

**① EDS 是给谁看的？**

给支持 CANopen 的上位机或配置工具看。

你的 STM32 里有这些对象：

```
0x6000:01 → 四路 DI
0x6200:01 → 四路 DO
0x6401:01 → AI1
0x6401:02 → AI2
0x1017:00 → 心跳周期
```

上位机刚接触这台设备时，并不知道这些对象分别是什么、占几字节、能不能写。

EDS 就把这些信息告诉它。例如：

```
对象：0x6200:01
名称：数字输出
类型：UNSIGNED8
权限：可读写
范围：0～15
```

支持 EDS 的工具导入后，就可以据此展示对象列表、识别数据类型、辅助配置。**EDS 本身不会执行读写；真正通信仍需要后面的 SDO/PDO 服务。**

而且普通 CAN 收发软件不一定支持 EDS，这取决于工具功能。



**② “第三方离线 EDS 导入”是在验证什么？**

就是把我们写的 EDS，交给**别人开发的 CANopen 软件库**读取，看它是否能理解。

这次用的是 `python-canopen`：

```
我们写的 EDS
    ↓
第三方 python-canopen 读取
    ↓
得到对象列表、类型、权限、默认值等
    ↓
与我们的 C 程序实际生成的对象表比较
```

例如两边都应该得到：

```
                    C代码实际对象表      EDS导入结果
0x6200:01 类型       UNSIGNED8           UNSIGNED8
          权限       可读写               可读写
          默认值     0                    0
          最大值     15                   15
```

这样能发现“**代码改了，EDS 忘了改**”之类的问题，也能避免 EDS 只有我们自己的解析程序才读得懂。

这里叫“离线”，是因为**没有连接 STM32，也没有发送 CAN 报文，只在电脑上加载和比较文件**。

所以：

> 导入通过，表示第三方软件能读取这份说明书，而且我们检查的字段与代码一致；还不能证明实机通信已经正常。



**③ 你提供的三个 PDF 起了什么作用？**

它们帮助我们判断：**即使代码和 EDS 两边写得一样，这个写法是否符合规范。**

举个这次真实发现的例子：

```
原来 EDS 写：Granularity=8
第三方工具：能正常导入
    ↓
对照规范正文
    ↓
发现固定、不可修改的 PDO 映射应该声明 Granularity=0
```

所以这是两个不同角度的检查：

| 检查             | 解决的问题                                      |
| ---------------- | ----------------------------------------------- |
| 对照规范 PDF     | 我们采用的规则、字段和默认配置是否有依据？      |
| 第三方导入并比较 | 别人的软件能否读懂 EDS？EDS 与 C 代码是否一致？ |

**不是下载完 PDF 就自动实现了功能。** 我是依据 PDF 核对已有代码和 EDS，修正发现的差异，再重新跑测试。



**④ “v1.1 基线冻结”又是什么？**

你可以理解为：

> **把这次核对并测试通过的代码和设备说明书，确定为后续开发共同使用的版本。**

```
阶段2确定的对象表和EDS v1.1
    │
    ├── 阶段3：使用其中的节点与对象基础
    ├── 阶段4：使用心跳周期对象
    ├── 阶段5：SDO按这张表查找、读写
    └── 阶段6：PDO按确定的映射搬运数据
```

“冻结”不是永远不能修改，也不是获得了官方认证。它是说：**以后改对象、类型、默认值或映射时，要有记录，并同步修改代码、EDS 和测试，必要时升版本。**

对你当前学习最直接的作用就是：**阶段 2 已经有一套确定的对象表，后面的 SDO/PDO 可以以它为基础继续写，不用一边写服务，一边反复猜对象应该怎么定义**
