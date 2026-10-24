# CANopen IO Node

基于 STM32F429ZGT6 与 FreeRTOS 的 CANopen IO 节点项目，已完成自制 PCB 焊接和实板验证。

## 项目总览

![CANopen IO Node 项目总览](images/3：项目总览图/项目2总览图.png)

![CANopen IO Node 系统总览](images/3：项目总览图/项目2总览图2.png)

## PCB 设计与焊接

![原理图及 PCB 设计](images/1：pcb及焊接/1：原理图及pcb设计.png)

![PCB 3D 视图](images/1：pcb及焊接/2：pcb对应3D视图.png)

![PCB 裸板](images/1：pcb及焊接/3：实物裸板.png)

![焊接电源及芯片](images/1：pcb及焊接/4：焊接电源及芯片.png)

![焊接完成并开始测试](images/1：pcb及焊接/5：焊接完成开始测试.png)

## 软件代码测试

### 启动与 CANopen 状态

![设备正常启动](images/2：软件代码测试/1：设备正常启动.png)

![节点进入 Operational](images/2：软件代码测试/2：节点已进入 Operationa.png)

### PDO：数字输出与数字输入

![控制 LED1 亮](images/2：软件代码测试/3.1控制灯led1亮.png)

![LED1 点亮实物](images/2：软件代码测试/3.1对应灯led1亮的实物图.png)

![控制 LED2 亮](images/2：软件代码测试/3.2控制灯led2亮.png)

![LED2 点亮实物](images/2：软件代码测试/3.2对应灯led2亮的实物图.png)

![LED 全灭实物](images/2：软件代码测试/3.2对应led全灭实物图.png)

![控制 LED3 亮](images/2：软件代码测试/3.3控制灯led3亮.png)

![LED3 点亮实物](images/2：软件代码测试/3.3对应灯led3亮的实物图.png)

![控制 LED4 亮](images/2：软件代码测试/3.4对应led4亮.png)

![LED4 点亮实物](images/2：软件代码测试/3.4对应灯led4亮的实物图.png)

![发送 00 关闭所有 LED](images/2：软件代码测试/3.5发送00控制所有灯灭.png)

![按下 KEY1](images/2：软件代码测试/4.1：按下key1.png)

![按下 KEY2](images/2：软件代码测试/4.2：按下key2.png)

![按下 KEY3](images/2：软件代码测试/4.3：按下key3.png)

![按下 KEY4](images/2：软件代码测试/4.4：按下key4.png)

![组合按下 KEY2 和 KEY3](images/2：软件代码测试/4.5组合按下key2和key3.png)

### SDO：ADC 读取与 DO 写入

![SDO 读取 ADC1 并观察电位器变化](images/2：软件代码测试/5.1：SDO读取ADC1采样值及拧动后的采样值变化有效读取到.png)

![SDO 读取 ADC2 并观察电位器变化](images/2：软件代码测试/5.2：SDO读取ADC2采样值及拧动后的采样值变化有效读取到.png)

![SDO 写入 DO 点亮 LED 报文](images/2：软件代码测试/5.3：SDO%20写入控制%20DO%20点亮%20LED.png)

![SDO 写入 DO 点亮 LED 实物](images/2：软件代码测试/5.3：SDO写入控制DO点亮led实物图.png)

![SDO 写入 DO 熄灭 LED 报文](images/2：软件代码测试/5.4：SDO%20写入控制%20DO%20熄灭%20LED.png)

![SDO 写入 DO 熄灭 LED 实物](images/2：软件代码测试/5.4：SDO写入控制DO熄灭led实物图.png)

### NMT：节点状态控制

![NMT Stop 测试准备](images/2：软件代码测试/6.1：测试NMT的stop状态（先SDO打开led灯）.png)

![NMT 切换到 Stop](images/2：软件代码测试/6.2：切换NMT状态至STOP状态（此时灯灭）.png)

![Heartbeat 上报 NMT Stopped 状态](images/2：软件代码测试/6.3：系统心跳上报nmt状态至04.png)

![NMT Reset Node 测试](images/2：软件代码测试/7.1：测 Reset Node.png)

## 目录结构

- `canopen/`：CANopen 协议栈及对象字典相关代码
- `firmware/`：设备应用固件、板级支持和工程文件
- `tests/`：协议和应用层测试
- `docs/`：设计文档、接口说明和调试记录
- `images/`：原理图、波形和文档图片

## 开发状态

本项目经历了协议层开发、阶段性验证，以及最终自制 STM32F429ZGT6 四层 PCB 的焊接和上板测试。SDRAM 因 PCB 网络标记问题未焊接，本轮不纳入验收。

协议层及 PC 测试已完成；自制板实测覆盖 CANopen 启动与心跳、NMT Stop/Start/Reset、PDO 控制四路 LED 与读取四路按键、SDO 读写及 ADC1/ADC2 输入变化。板级外设测试工程也已验证 SD 卡 FatFs、W25Q256 SPI Flash 和 RTC。

系统固件包含 FreeRTOS 任务、CAN 接收队列、ADC DMA 采样、真实按键 DI、四路 LED DO，以及输入同步到对象字典的链路；协议层支持 NMT、Heartbeat、SDO、PDO 和 EMCY。下方依次展示项目总览、自制 PCB 与焊接过程、以及软件和板上测试记录。

提供 [项目基线 EDS v1.1](eds/CANopen_IO_Node.eds) 和 [对象字典草案](docs/对象字典草案.md)。PDO 的 COB-ID、传输类型、映射固定只读；抑制时间固定0，事件周期默认0且可在 Pre-operational 修改。AI 使用 INTEGER16 表示原始ADC乘8（0～32760）；6423控制模拟量变化触发。Vendor-ID=0 为未分配的开发占位。阶段2对象模型原文核验、第三方离线 EDS 导入和实机互通均已完成；项目不宣称完整 CiA 401 一致性认证。

## PC 构建与测试

需要 C11 编译器、CMake 3.20+、Ninja 和 Python 3（EDS 测试只用标准库），在项目根目录执行：

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

接口说明见 `canopen/README.md`，测试源码位于 `tests/`；协议设计文档见 `docs/COB-ID与PDO映射.md`、`docs/EDS草案.md`、`docs/NMT与Heartbeat状态图.md`、`docs/SDO-abort码表.md` 和 `docs/对象字典草案.md`。

## GitHub 提交流程

本项目使用独立 Git 仓库管理，正式工程路径为 `C:\Users\lolbo\CANopen_IO_Node`，远程仓库为：

`https://github.com/xiaoyangxianshen664/CANopen_IO_Node.git`

首次提交前进入项目根目录：

```bat
cd /d C:\Users\lolbo\CANopen_IO_Node
git status --short
git add .gitignore README.md canopen firmware docs images tests
git diff --cached --stat
git commit -m "Initialize CANopen IO Node project"
git remote add origin https://github.com/xiaoyangxianshen664/CANopen_IO_Node.git
git branch -M main
git push -u origin main
```

如果远程仓库已经存在 `origin`，先执行 `git remote -v`；地址不正确时执行：

```bat
git remote set-url origin https://github.com/xiaoyangxianshen664/CANopen_IO_Node.git
```

后续修改按以下顺序提交：

```bat
git status --short
git add <明确路径>
git diff --cached --stat
git commit -m "Describe the change"
git push
```






当前设计与后续约束见 [对象字典草案](docs/对象字典草案.md)。
