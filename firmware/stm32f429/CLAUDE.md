# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

- **芯片**：STM32F429IGTx（Cortex-M4，野火挑战者 F429 开发板）
- **工具链**：Keil MDK（`.uvprojx`），CMSIS-DAP 调试器
- **HAL 库**：STM32F4xx HAL Driver V1.27.0，手动移植（非 CubeMX 生成）
- **系统**：裸机，无 RTOS；HSE = 25 MHz；NVIC Priority Group 4（仅抢占优先级 0~15）

## 目录结构

```
hal_first_project/
├── BSP/                  # 板级支持包 — 每个外设一个文件夹，含 .h + .c
│   ├── LED/              #   PH10(R) / PH11(G) / PH12(B)，低电平亮
│   ├── Key/              #   KEY1(PA0) / KEY2(PC13)，高电平有效，外部下拉
│   └── Exti/             #   外部中断初始化（上升沿触发）
├── Library/
│   ├── CMSIS/            #   CMSIS Core + Device（启动文件、系统时钟配置）
│   └── STM32F4xx_HAL_Driver/  # HAL 库源码（Inc/ + Src/）
├── User/
│   ├── main.c            #   入口：HAL_Init → BSP_Init → while(1)
│   ├── stm32f4xx_it.c    #   中断服务函数（SysTick / HardFault / EXTI）
│   └── stm32f4xx_hal_conf.h  # HAL 模块裁剪开关
├── Project/              # Keil 工程文件 (.uvprojx) + 编译输出
└── Doc/                  # 文档（当前为空）
```

## 已启用的 HAL 模块

在 `User/stm32f4xx_hal_conf.h` 中通过宏裁剪，当前只开了 6 个：
`HAL_GPIO_MODULE_ENABLED`, `HAL_EXTI_MODULE_ENABLED`, `HAL_DMA_MODULE_ENABLED`,
`HAL_RCC_MODULE_ENABLED`, `HAL_FLASH_MODULE_ENABLED`, `HAL_PWR_MODULE_ENABLED`, `HAL_CORTEX_MODULE_ENABLED`

其余模块（UART/SPI/TIM/I2C 等）均为注释状态，用到时需要先取消注释对应的 `#define HAL_xxx_MODULE_ENABLED`。

## 代码规范（BSP 层）

新增外设时遵循现有模式：

```c
// === xxx.h ===
#ifndef __XXX_H
#define __XXX_H
#include "stm32f4xx.h"

#define XXX_PIN         GPIO_PIN_x
#define XXX_GPIO_PORT   GPIOx
#define XXX_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOx_CLK_ENABLE()

void Xxx_Init(void);
#endif

// === xxx.c ===
#include "./Xxx/Xxx.h"     // BSP 内部用相对路径 include

void Xxx_Init(void)
{
    XXX_GPIO_CLK_ENABLE();
    GPIO_InitTypeDef init = {0};
    init.Pin   = XXX_PIN;
    init.Mode  = GPIO_MODE_xxx;
    init.Pull  = GPIO_xxx;
    init.Speed = GPIO_SPEED_FREQ_xxx;
    HAL_GPIO_Init(XXX_GPIO_PORT, &init);
}
```

要点：
- **LED 宏风格**：`LED_R(0)` 亮 / `LED_R(1)` 灭（低电平有效）
- **ISR 内不调用 HAL_Delay**，用 `for(volatile ...)` 做简单消抖
- **中断服务函数末尾清标志**，不提前清
- **main 入口固定流程**：`HAL_Init()` → 各外设 `_Init()` → `while(1)`

## 添加新 HAL 模块的步骤

1. 在 `User/stm32f4xx_hal_conf.h` 中取消对应模块的注释
2. 在 Keil 工程中把对应 HAL 驱动 `.c` 加入工程组
3. 如涉及中断，在 `User/stm32f4xx_it.c` 中写 `xxx_IRQHandler` 并调用 `HAL_xxx_IRQHandler()`

## 编译

在 Keil MDK 中打开 `Project/hal_429.uvprojx`，直接 Build（F7）。无命令行编译脚本。

## 外设驱动生成规则

用户在工程里说"加一个外设驱动"时，自动按以下规则生成 BSP 文件：

1. **文件放在 `BSP/` 下**，一个外设一个文件夹，含 `.h` + `.c`
2. **`.h` 必须包含**：`#ifndef __XXX_H` 头保护、引脚/外设宏定义、时钟使能宏、初始化函数声明。参照 `BSP/LED/LED.h` 的格式
3. **`.c` 必须包含**：相对路径 include、完整的外设初始化函数（含 GPIO 配置、时钟使能、外设参数配置）。参照 `BSP/LED/LED.c` 的格式
4. **注释全部用中文**
5. **初始化结构体先清零**：`GPIO_InitTypeDef init = {0};`（HAL 库所有 InitTypeDef 都按这个来）
6. **ISR 内不调 HAL_Delay()**：SysTick 优先级低于外设中断，ISR 里调用会卡死。ISR 内用 `for(volatile ...)` 做延时/消抖；普通代码正常用 HAL_Delay
7. **生成代码后告知用户**：需要在 `hal_conf.h` 里开哪个模块宏、在 `it.c` 里加什么中断、需不需要在 Keil 工程里加 `.c` 文件