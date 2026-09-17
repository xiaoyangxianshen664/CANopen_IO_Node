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

阶段 1 的纯 C 帧抽象、发送回调和节点地址分类已完成，PC 测试 4/4 通过。对象字典及各协议服务尚未实现，未进行本阶段硬件验证。

## PC 构建与测试

需要 C11 编译器、CMake 3.20+ 和 Ninja，在项目根目录执行：

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

接口说明见 `canopen/README.md`，测试证据见 `docs/阶段1测试记录.md`。

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
git push origin main
```





