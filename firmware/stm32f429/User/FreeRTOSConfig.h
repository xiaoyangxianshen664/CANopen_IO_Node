#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "stm32f4xx.h"
#include "./Usart/Usart.h"

/* 针对不同编译器，包含不同的 stdint.h 文件 */
#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
#include <stdint.h>
extern uint32_t SystemCoreClock;
#endif

/* 断言：失败时关中断死循环，不用 printf 避免头文件依赖 */
#define configASSERT(x)           \
    if ((x) == 0)                 \
    {                             \
        taskDISABLE_INTERRUPTS(); \
        for (;;)                  \
            ;                     \
    }

/* ──────────────────────────────────────────────────────────────
 *          FreeRTOS 基础配置选项
 * ────────────────────────────────────────────────────────────── */

/* 1 = 抢占式调度，0 = 协作式调度（没有时间片，任务主动释放 CPU） */
#define configUSE_PREEMPTION 1

/* 1 = 同优先级任务时间片轮转 */
#define configUSE_TIME_SLICING 1

/* 1 = 使用硬件前导零指令 [CLZ] 优化任务选择，M4 支持，开 */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1

/* 1 = 开启低功耗 tickless 模式（空闲时停 SysTick 省电）
 * 注意：tickless 可能导致下载失败，因为 MCU 进入睡眠后调试器连不上
 * 恢复方法：
 *    1. BOOT0 接高电平(3.3V) → 上电 → 擦除芯片 → 重新下载
 *    2. 或用 FlyMcu 擦除芯片：STMISP → 擦除芯片 */
#define configUSE_TICKLESS_IDLE 0

/* CPU 内核时钟频率，即 HCLK = 180MHz */
#define configCPU_CLOCK_HZ (SystemCoreClock)

/* RTOS 心跳频率 Hz，1000 = 每 1ms 一次 SysTick 中断 */
#define configTICK_RATE_HZ ((TickType_t)1000)

/* 最大优先级数，0~31 共 32 级 */
#define configMAX_PRIORITIES (32)

/* 空闲任务的最小栈大小，单位 word（4 字节） */
#define configMINIMAL_STACK_SIZE ((unsigned short)128)

/* 任务名最大长度（含 '\0'） */
#define configMAX_TASK_NAME_LEN (16)

/* 1 = 使用 16 位 TickType_t，0 = 32 位 */
#define configUSE_16_BIT_TICKS 0

/* 1 = 空闲任务主动让出 CPU 给同优先级任务 */
#define configIDLE_SHOULD_YIELD 1

/* 开启队列集 */
#define configUSE_QUEUE_SETS 0

/* 开启任务通知功能 */
#define configUSE_TASK_NOTIFICATIONS 1

/* 开启互斥信号量 */
#define configUSE_MUTEXES 0

/* 开启递归互斥信号量 */
#define configUSE_RECURSIVE_MUTEXES 0

/* 开启计数信号量 */
#define configUSE_COUNTING_SEMAPHORES 0

/* 注册的队列和信号量数量上限（调试用） */
#define configQUEUE_REGISTRY_SIZE 10

#define configUSE_APPLICATION_TASK_TAG 0

/* ──────────────────────────────────────────────────────────────
 *          FreeRTOS 内存管理配置
 * ────────────────────────────────────────────────────────────── */
/* 支持动态内存分配（xTaskCreate 需要） */
#define configSUPPORT_DYNAMIC_ALLOCATION 1
/* 支持静态内存分配 */
#define configSUPPORT_STATIC_ALLOCATION 0
/* FreeRTOS 堆总大小，36KB */
#define configTOTAL_HEAP_SIZE ((size_t)(36 * 1024))

/* ──────────────────────────────────────────────────────────────
 *          FreeRTOS 钩子函数
 * ────────────────────────────────────────────────────────────── */
/* 1 = 使能空闲钩子 vApplicationIdleHook()
 * 空闲任务每轮循环都会调用此函数，可用于：
 *    - 释放被删除任务的栈内存
 *    - 进入 CPU 低功耗模式
 *    注意：不能在里面调用会引起阻塞的 API */
#define configUSE_IDLE_HOOK 0

/* 1 = 使能 Tick 钩子 vApplicationTickHook()
 * 在每个 SysTick 中断末尾被调用，必须极短、不能阻塞 */
#define configUSE_TICK_HOOK 0

/* 使能内存申请失败钩子 vApplicationMallocFailedHook() */
#define configUSE_MALLOC_FAILED_HOOK 0

/* 栈溢出检测：0=关，1=方法一，2=方法二 */
#define configCHECK_FOR_STACK_OVERFLOW 0

/* ──────────────────────────────────────────────────────────────
 *          FreeRTOS 运行状态与调试
 * ────────────────────────────────────────────────────────────── */
/* 开启运行时统计（需配合定时器提供时基） */
#define configGENERATE_RUN_TIME_STATS 0
/* 开启可视化追踪 */
#define configUSE_TRACE_FACILITY 0
/* 配合 configUSE_TRACE_FACILITY=1，开启 vTaskList() 等格式化函数 */
#define configUSE_STATS_FORMATTING_FUNCTIONS 1

/* ──────────────────────────────────────────────────────────────
 *          FreeRTOS 协程（一般不用）
 * ────────────────────────────────────────────────────────────── */
#define configUSE_CO_ROUTINES 0
#define configMAX_CO_ROUTINE_PRIORITIES (2)

/* ──────────────────────────────────────────────────────────────
 *          FreeRTOS 软件定时器
 * ────────────────────────────────────────────────────────────── */
#define configUSE_TIMERS 0
#define configTIMER_TASK_PRIORITY (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_STACK_DEPTH (configMINIMAL_STACK_SIZE * 2)

/* ──────────────────────────────────────────────────────────────
 *          FreeRTOS 可选 API 函数（按需开启以省 ROM）
 * ────────────────────────────────────────────────────────────── */
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskCleanUpResources 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_eTaskGetState 1
#define INCLUDE_xTimerPendFunctionCall 0

/* ──────────────────────────────────────────────────────────────
 *          FreeRTOS 中断配置
 * ────────────────────────────────────────────────────────────── */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS __NVIC_PRIO_BITS
#else
#define configPRIO_BITS 4
#endif

/* 最低中断优先级（0 最高，15 最低） */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15

/* 系统可管理的中断最大优先级（5~15 的 ISR 可调 FreeRTOS API，0~4 不可调） */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* 硬编码为 0x50 = 80，绕过 ARMCC V5 内联汇编常量展开 bug */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 0x50

/* ──────────────────────────────────────────────────────────────
 *          FreeRTOS 中断服务函数映射
 * ────────────────────────────────────────────────────────────── */
#define xPortPendSVHandler PendSV_Handler
#define vPortSVCHandler SVC_Handler

/* Tracealyzer 追踪（默认关） */
#if (configUSE_TRACE_FACILITY == 1)
#include "trcRecorder.h"
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#endif

#endif /* FREERTOS_CONFIG_H */
