# CANopen IO Node

基于 STM32 的 CANopen IO 节点项目。

## 目录结构

- `canopen/`：CANopen 协议栈及对象字典相关代码
- `firmware/`：设备应用固件、板级支持和工程文件
- `tests/`：协议和应用层测试
- `docs/`：设计文档、接口说明和调试记录
- `images/`：原理图、波形和文档图片

## 开发状态

项目骨架已建立，硬件基线为野火挑战者 STM32F429IGT6 开发板；CAN 引脚和低压 I/O 方案已完成阶段 0 冻结。

阶段 1 基础层、阶段 2 通用对象字典及 36 条实际对象、阶段 3 NMT/Boot-up、阶段 4 Heartbeat Producer、阶段 5 expedited SDO Server 和阶段 6 固定 PDO 已实现；当前全部 PC 测试通过。DI/DO/AI、Heartbeat 周期和固定 PDO 参数已绑定；STM32 bxCAN 硬件适配仍待后续阶段实现。

提供 [项目基线 EDS v1.1](eds/CANopen_IO_Node.eds) 和 [对象表与复核记录](docs/阶段2对象表与规范复核.md)。PDO 的 COB-ID、传输类型、映射固定只读；抑制时间固定0，事件周期默认0且可在 Pre-operational 修改。AI 使用 INTEGER16 表示原始ADC乘8（0～32760）；6423控制模拟量变化触发。Vendor-ID=0 为未分配的开发占位。阶段2对象模型原文核验与第三方离线EDS导入已完成；实机互通尚待完成，不宣称完整 CiA 401 实现或认证。

## 后续路线

阶段7接入 STM32 bxCAN，阶段8接入 FreeRTOS 与真实 I/O，阶段9补充 EMCY 与可靠性，阶段10完成开发板系统验收和项目材料。阶段11设计并制作适配本项目的 STM32F429 专用 PCB，完成硬件适配、实物复验和最终交付。

## PC 构建与测试

需要 C11 编译器、CMake 3.20+、Ninja 和 Python 3（EDS 测试只用标准库），在项目根目录执行：

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

接口说明见 `canopen/README.md`，测试证据见 `docs/阶段1测试记录.md`、`docs/阶段2测试记录.md`、`docs/阶段3测试记录.md`。

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






当前设计与后续约束见 [阶段2冻结基线](docs/阶段2冻结基线-v1.1.md)。
