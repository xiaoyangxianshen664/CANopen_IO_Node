# GitHub 交付记录

## 阶段6：PDO 和固定映射

日期：2026-10-01

本次交付包含：

- 固定 RPDO1、TPDO1、TPDO2 的纯 C 协议实现；
- PDO 对象字典读取、状态门控、输入变化触发和事件周期处理；
- PC 端 PDO 测试和 CMake/CTest 构建接入；
- 阶段6测试记录。

验证命令：

```text
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

验证结果：

- 构建通过；
- 15/15 CTest 通过；
- `git diff --check` 通过。

当前限制：尚未接入 STM32 bxCAN、真实 CAN 总线、硬件 I/O、动态 PDO 重映射、RPDO2、SYNC 和抑制时间配置。下一阶段为阶段7 STM32 bxCAN 传输层。
# GitHub 交付记录

## 阶段7：STM32F429 CAN BSP（2026-10-02）

- 分支：`stage7-can`
- 内容：`BSP/CAN/bsp_can.c`、`BSP/CAN/bsp_can.h` 和阶段7交接文档。
- 范围：CAN1、500 kbit/s、标准帧、过滤器、FIFO0 接收中断、发送邮箱、软件接收队列和错误回调。
- 验证：STM32F429 Keil 工程此前本地编译通过；PC 协议测试为 15/15 通过。
- 当前限制：本次只交付 CAN BSP 和文档，不上传完整 HAL、CMSIS、FreeRTOS 或 `main.c` 工程；完整工程待阶段7硬件传输层验证完成后再交付。

## 阶段8：FreeRTOS 与真实 I/O（2026-10-05，待提交）

阶段8已完成代码学习、真实 I/O 接入和重新烧录后的 USB-CAN 回归测试。GitHub 交付应按功能范围精确选择文件，不把当前工作区中的完整第三方库、副本目录和临时文件全部上传。

### 建议纳入本次提交的内容

- `firmware/stm32f429/User/freertos_demo.c/.h`：四个 FreeRTOS 任务、队列、任务通知、任务创建和调度器启动；
- `firmware/stm32f429/BSP/ADC/ADC_Multi.c/.h`：ADC1 双通道扫描、TIM3 触发、DMA 循环缓冲和半区通知；
- `firmware/stm32f429/User/canopen_app.c/.h`：任务层使用的 CANopen 应用封装接口；
- 阶段8需要的 Key、LED、GPIO/中断和定时器 BSP 文件（提交前逐项确认是否为本项目实际依赖）；
- 阶段8测试笔记、阶段交接、阶段进度、源码总梳理和本交付记录；
- 阶段8涉及的对象字典/PDO/任务架构说明，确保其他人能够理解 `0x201`、`0x181`、`0x281` 和 `0x6000/0x6200/0x6401` 的关系。

本次“项目代码与文档”提交可优先按以下精确路径暂存：

```text
firmware/stm32f429/BSP/ADC/ADC_Multi.c
firmware/stm32f429/BSP/ADC/ADC_Multi.h
firmware/stm32f429/BSP/Key/Key.c
firmware/stm32f429/BSP/Key/Key.h
firmware/stm32f429/BSP/LED/LED.c
firmware/stm32f429/BSP/LED/LED.h
firmware/stm32f429/BSP/Exti/Exti.c
firmware/stm32f429/BSP/Exti/Exti.h
firmware/stm32f429/BSP/TIM6/TIM6.c
firmware/stm32f429/BSP/TIM6/TIM6.h
firmware/stm32f429/BSP/TIM7/TIM7.c
firmware/stm32f429/BSP/TIM7/TIM7.h
firmware/stm32f429/BSP/Usart/Usart.c
firmware/stm32f429/BSP/Usart/Usart.h
firmware/stm32f429/BSP/SysTick/SysTick.h
firmware/stm32f429/User/freertos_demo.c
firmware/stm32f429/User/freertos_demo.h
firmware/stm32f429/User/canopen_app.c
firmware/stm32f429/User/canopen_app.h
firmware/stm32f429/User/main.c
firmware/stm32f429/User/stm32f4xx_it.c
firmware/stm32f429/User/stm32f4xx_hal_conf.h
firmware/stm32f429/User/FreeRTOSConfig.h
docs/github交付记录.md
docs/项目2阶段进度.md
docs/项目阶段交接文件.md
项目完成各阶段对应笔记/梳理各阶段（.c和.h）.md
项目完成各阶段对应笔记/阶段8/
```

上面这组路径表达“项目代码和学习交付范围”，不代表已经完成完整 Keil 独立构建。若要让 GitHub 克隆后直接用 Keil 编译，还需另外决定是否提交 `FreeRTOS/`、`Library/` 和 `Project/` 中的第三方源码与工程配置；这三部分不应在未检查许可证和文件范围前直接加入阶段8提交。

### 不应随本次阶段提交的内容

- `firmware/stm32f429 - 副本/` 等完整工程副本目录；
- `pelican-bicycle-animation.html`、`tage 7 CANopen hardware validation…` 等与项目无关或来源不明的临时文件；
- `build/`、`Debug/`、`Release/`、`*.axf`、`*.hex`、`*.bin`、`*.map` 等构建产物；
- 未经确认的完整 CMSIS、HAL、FreeRTOS 第三方库和 Keil 用户配置文件。若需要把完整 Keil 工程作为可复现附件交付，应单独确认范围和许可证后再提交。

### 阶段8验证结果

- 重新烧录后 `0x000: 01 01` 可进入 Operational，Heartbeat `0x701` 为 `05`；
- SDO 读取 DI、AI1、AI2、DO 正常；
- RPDO1 `0x201` 的 `00/01/02/04/05` 控制 RGB 灯正常；
- TPDO1 `0x181`、TPDO2 `0x281` 配置 500 ms 自动上报正常；
- KEY1、KEY2、电位器和温度传感器变化上报正常。

### 当前交付状态

本节只记录阶段8的 GitHub 交付范围和验证证据，当前尚未执行 `git add`、提交或推送。正式提交前需先使用精确路径暂存，确认 `git diff --cached` 后再创建阶段8提交；不得使用 `git add -A` 把无关副本和临时文件带入仓库。
