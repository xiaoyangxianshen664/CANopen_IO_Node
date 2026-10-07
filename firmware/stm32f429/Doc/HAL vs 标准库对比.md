# HAL 库 vs 标准库 — 外设对比

> 标准库（SPL/StdPeriph）≈ 野火 F1 例程用的；HAL 库 ≈ 你 F429 现在用的。
> 核心差异：标准库是直接操作寄存器结构体，HAL 库多了一层句柄（Handle）封装。

---

## 1. GPIO

| | 标准库 | HAL 库 |
|------|--------|--------|
| 初始化函数 | `GPIO_Init(GPIOx, &init)` | `HAL_GPIO_Init(GPIOx, &init)` |
| 结构体 | `GPIO_InitTypeDef`（同 HAL） | `GPIO_InitTypeDef` |
| 成员 | `GPIO_Pin, GPIO_Mode, GPIO_Speed` | `Pin, Mode, Pull, Speed`（多了 Pull，Mode 取值不同） |
| 模式 | `GPIO_Mode_Out_PP` 等 | `GPIO_MODE_OUTPUT_PP` 等 |
| 写引脚 | `GPIO_SetBits()` / `GPIO_ResetBits()` | `HAL_GPIO_WritePin(port, pin, state)` |
| 读引脚 | `GPIO_ReadInputDataBit()` | `HAL_GPIO_ReadPin(port, pin)` |
| 翻转 | `无内置` | `HAL_GPIO_TogglePin(port, pin)` |
| 锁存 | `GPIO_PinLockConfig()` | `HAL_GPIO_LockPin()` |
| 时钟 | `RCC_APB2PeriphClockCmd(GPIOx, ENABLE)` | `__HAL_RCC_GPIOx_CLK_ENABLE()` 宏 |

**关键变化：** 标准库的 GPIO 操作按"引脚组 + 位"掩码；HAL 统一成 `(port, pin, state)` 三参数。多了内置翻转、多了 Pull 上下拉配置。

---

## 2. EXTI

| | 标准库 | HAL 库 |
|------|--------|--------|
| 配置方式 | `GPIO_EXTILineConfig()` + `EXTI_Init()` | `HAL_GPIO_Init()` 里设 `Mode = GPIO_MODE_IT_xxx` |
| 触发模式 | `EXTI_Trigger_Rising/Falling/Rising_Falling` | `GPIO_MODE_IT_RISING/FALLING/RISING_FALLING` |
| NVIC | 手动 `NVIC_InitTypeDef` + `NVIC_Init()` | `HAL_NVIC_SetPriority()` + `HAL_NVIC_EnableIRQ()` |
| ISR 名称 | 手动写 `void EXTI0_IRQHandler(void)` | 同（名字一样） |
| 中断标志 | `EXTI_GetITStatus()` / `EXTI_ClearITPendingBit()` | `__HAL_GPIO_EXTI_GET_IT()` / `__HAL_GPIO_EXTI_CLEAR_IT()` 宏 |
| ISR 回调 | 无 | 可选 `HAL_GPIO_EXTI_Callback()` 统一入口 |

**关键变化：** 标准库分两步（配 GPIO→EXTI 线 + 配 EXTI 参数），HAL 库一步完成（在 GPIO Init 里把 Mode 设为中断模式就行）。HAL 多了一个回调机制，多个引脚可以走同一个 callback。

---

## 3. 定时器 TIM

| | 标准库 | HAL 库 |
|------|--------|--------|
| 句柄 | 无（全局配置） | `TIM_HandleTypeDef` 必须存在 |
| 配置结构 | `TIM_TimeBaseInitTypeDef` | 填到 `htim.Init` 成员里 |
| 时基参数 | `TIM_Prescaler, TIM_Period, TIM_ClockDivision, TIM_CounterMode` | 同名，在 `htim.Init.xxx` 里 |
| 初始化 | `TIM_TimeBaseInit(TIMx, &init)` | `HAL_TIM_Base_Init(&htim)` |
| 启动 | `TIM_Cmd(TIMx, ENABLE)` | `HAL_TIM_Base_Start(&htim)` |
| 中断启动 | `TIM_ITConfig()` + NVIC 配合 | `HAL_TIM_Base_Start_IT(&htim)` |
| PWM | `TIM_OCInitTypeDef` → `TIM_OCxInit()` | 填 `sConfigOC` → `HAL_TIM_PWM_ConfigChannel()` |
| 中断回调 | 无（ISR 里手动判断源） | `HAL_TIM_PeriodElapsedCallback(&htim)` 统一回调 |
| 句柄实例 | 不需要 | 必须 `static` 或全局，中断里用它区分定时器 |

**关键变化：** 这个变化最大。标准库无状态，所有参数直接给；HAL 库把一切装进一个 Handle（句柄），所有操作都通过它。好处是一个 Handle 描述一个定时器，但多了句柄管理的心智负担。

---

## 4. UART/USART

| | 标准库 | HAL 库 |
|------|--------|--------|
| 句柄 | 无 | `UART_HandleTypeDef` 必须存在 |
| 配置 | `USART_InitTypeDef` → `USART_Init()` | 填 `huart.Init.xxx` → `HAL_UART_Init(&huart)` |
| 波特率 | `USART_InitStructure.USART_BaudRate` | `huart.Init.BaudRate` |
| 发送字节 | `USART_SendData(USARTx, data)` + 等 TC | `HAL_UART_Transmit(&huart, &data, 1, timeout)` |
| 接收 | `data = USART_ReceiveData(USARTx)` | `HAL_UART_Receive(&huart, &data, 1, timeout)` |
| 发送字符串 | 自己写循环 | `HAL_UART_Transmit(&huart, buf, len, timeout)` 一步到位 |
| 中断收发 | 手动配 `USART_ITConfig()` + ISR 里判断 | `HAL_UART_xxx_IT()` ，回调 `HAL_UART_RxCpltCallback` |
| printf 重定向 | 自己写 `fputc`，放 main.c 或 usart.c | 同，写法一样 |

**关键变化：** HAL 库的发送接收封装成了带 timeout 的阻塞函数，字符串也能一次性发送。标准库要自己写循环逐个字节发。中断模式 HAL 更统一（有回调）。

---

## 总结

| 维度 | 标准库 | HAL 库 |
|------|--------|--------|
| 抽象层级 | 薄封装，离寄存器近 | 厚封装，加了一层 Handle |
| 代码量 | 少但分散 | 多但统一 |
| 学习曲线 | 先难后易（需懂寄存器） | 先易后难（需懂 Handle 机制） |
| 中断处理 | 各外设自己的 ISR 逻辑 | 统一回调，弱函数覆盖 |
| API 命名 | `外设名_功能()` | `HAL_外设名_功能()` |
| 可移植性 | 同系列才通用 | 跨 STM32 全系列 |

---

## 实战踩坑：F1 标准库 → F4 HAL 库三个大坑

### 坑1：中断向量名变了

不是所有外设中断向量 F1→F4 名字一样，有的完全变了：

```c
// F1 标准库
#define BASIC_TIM_IRQn    TIM6_IRQn

// F4 HAL库（TIM6 和 DAC 共用一个中断向量！）
#define BASIC_TIM_IRQn    TIM6_DAC_IRQn       // ← 坑在这里
```

涉及的外设：TIM6/7 跟 DAC 共享、部分 USART 编号对齐方式也不同。

### 坑2：定时器时钟会自动 ×2

F4 上 APB 预分频器 ≠ 1 时，定时器时钟自动翻倍（F1 没有这个机制）：

| F1 | F4 |
|----|----|
| TIMxCLK = PCLK1（直通） | TIMxCLK = PCLK1 × 2（APB1 预分频 ≠ 1 时） |

```
当前工程: SYS_CLK=180MHz, APB1=180/4=45MHz → TIM6_CLK=45×2=90MHz
周期: T = (PSC+1) × (ARR+1) / TIM6_CLK = 9000×5000/90MHz = 0.5s
```

算错 PSC/ARR 直接导致定时周期不对。

### 坑3：F4 多了 AF（复用功能）

F1 多数外设引脚自动切换功能，F4 需要显式配置 GPIO Mode 为 `GPIO_MODE_AF_PP`：

```c
// F4 PWM 输出时必须设 AF
GPIO_InitStructure.Mode      = GPIO_MODE_AF_PP;    // ← F1 不需要这个
GPIO_InitStructure.Alternate = GPIO_AF1_TIM1;      // ← F4 独有的 Alternate 成员
```

普通推挽输出（点灯）和输入（按键）不需要 AF。只有 TIM PWM / UART / SPI / I2C 等外设引脚才需要。

---

## 怎么查：F1→F4 对比笔记放在哪？

五个 BSP 头文件顶部各有一段详细对比 + 代码示例，打开即看：

- `BSP/LED/LED.h` — GPIO 输出
- `BSP/Key/Key.h` — GPIO 输入（按键）
- `BSP/Exti/Exti.h` — EXTI 外部中断
- `BSP/Usart/Usart.h` — UART 串口
- `BSP/TIM6/TIM6.h` — 基本定时器
