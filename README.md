# CANopen IO Node

基于 STM32F429ZGT6 与 FreeRTOS 的 CANopen IO 节点项目，已完成自制 PCB 焊接和实板验证。

## 项目总览

![CANopen IO Node 项目总览](images/3：项目总览图/项目2总览图.png)

完整总览图：[项目2总览图2](images/3：项目总览图/项目2总览图2.png)

## 图片资料

- [PCB 设计与焊接过程](images/1：pcb及焊接/)
- [软件代码与板上测试记录](images/2：软件代码测试/)


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
