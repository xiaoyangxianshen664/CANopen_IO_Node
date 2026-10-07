/*
    FreeRTOS V9.0.0 - Copyright (C) 2016 Real Time Engineers Ltd.
    All rights reserved

    VISIT http://www.FreeRTOS.org TO ENSURE YOU ARE USING THE LATEST VERSION.

    This file is part of the FreeRTOS distribution.

    FreeRTOS is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License (version 2) as published by the
    Free Software Foundation >>>> AND MODIFIED BY <<<< the FreeRTOS exception.

    ***************************************************************************
    >>!   NOTE: The modification to the GPL is included to allow you to     !<<
    >>!   distribute a combined work that includes FreeRTOS without being   !<<
    >>!   obliged to provide the source code for proprietary components     !<<
    >>!   outside of the FreeRTOS kernel.                                   !<<
    ***************************************************************************

    FreeRTOS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.  Full license text is available on the following
    link: http://www.freertos.org/a00114.html

    ***************************************************************************
     *                                                                       *
     *    FreeRTOS provides completely free yet professionally developed,    *
     *    robust, strictly quality controlled, supported, and cross          *
     *    platform software that is more than just the market leader, it     *
     *    is the industry's de facto standard.                               *
     *                                                                       *
     *    Help yourself get started quickly while simultaneously helping     *
     *    to support the FreeRTOS project by purchasing a FreeRTOS           *
     *    tutorial book, reference manual, or both:                          *
     *    http://www.FreeRTOS.org/Documentation                              *
     *                                                                       *
    ***************************************************************************

    http://www.FreeRTOS.org/FAQHelp.html - Having a problem?  Start by reading
    the FAQ page "My application does not run, what could be wrong?".  Have you
    defined configASSERT()?

    http://www.FreeRTOS.org/support - In return for receiving this top quality
    embedded software for free we request you assist our global community by
    participating in the support forum.

    http://www.FreeRTOS.org/training - Investing in training allows your team to
    be as productive as possible as early as possible.  Now you can receive
    FreeRTOS training directly from Richard Barry, CEO of Real Time Engineers
    Ltd, and the world's leading authority on the world's leading RTOS.

    http://www.FreeRTOS.org/plus - A selection of FreeRTOS ecosystem products,
    including FreeRTOS+Trace - an indispensable productivity tool, a DOS
    compatible FAT file system, and our tiny thread aware UDP/IP stack.

    http://www.FreeRTOS.org/labs - Where new FreeRTOS products go to incubate.
    Come and try FreeRTOS+TCP, our new open source TCP/IP stack for FreeRTOS.

    http://www.OpenRTOS.com - Real Time Engineers ltd. license FreeRTOS to High
    Integrity Systems ltd. to sell under the OpenRTOS brand.  Low cost OpenRTOS
    licenses offer ticketed support, indemnification and commercial middleware.

    http://www.SafeRTOS.com - High Integrity Systems also provide a safety
    engineered and independently SIL3 certified version for use in safety and
    mission critical applications that require provable dependability.

*/

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "stm32f4xx.h"
#include "./Usart/Usart.h"

// 锟斤拷圆锟酵锟侥憋拷锟斤拷锟斤拷锟斤拷锟矫诧拷同锟斤拷stdint.h锟侥硷拷
#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
#include <stdint.h>
extern uint32_t SystemCoreClock;
#endif

// 锟斤拷锟斤拷
#define vAssertCalled(char, int) printf("Error:%s,%d\r\n", char, int)
#define configASSERT(x)           \
    if ((x) == 0)                 \
    {                             \
        taskDISABLE_INTERRUPTS(); \
        for (;;)                  \
            ;                     \
    }

/************************************************************************
 *               FreeRTOS锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷选锟斤拷
 *********************************************************************/
/* 锟斤拷1锟斤拷RTOS使锟斤拷锟斤拷占式锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷0锟斤拷RTOS使锟斤拷协锟斤拷式锟斤拷锟斤拷锟斤拷锟斤拷时锟斤拷片锟斤拷
 *
 * 注锟斤拷锟节讹拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷希锟斤拷锟斤拷锟较低筹拷锟斤拷苑锟轿锟斤拷占式锟斤拷协锟斤拷式锟斤拷锟街★拷
 * 协锟斤拷式锟斤拷锟斤拷系统锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟酵凤拷CPU锟斤拷锟叫伙拷锟斤拷锟斤拷一锟斤拷锟斤拷锟斤拷
 * 锟斤拷锟斤拷锟叫伙拷锟斤拷时锟斤拷锟斤拷全取锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟叫碉拷锟斤拷锟斤拷
 */
#define configUSE_PREEMPTION 1

// 1使锟斤拷时锟斤拷片锟斤拷锟斤拷(默锟斤拷式使锟杰碉拷)
#define configUSE_TIME_SLICING 1

/* 某些锟斤拷锟斤拷FreeRTOS锟斤拷硬锟斤拷锟斤拷锟斤拷锟街凤拷锟斤拷选锟斤拷锟斤拷一锟斤拷要执锟叫碉拷锟斤拷锟斤拷
 * 通锟矫凤拷锟斤拷锟斤拷锟截讹拷锟斤拷硬锟斤拷锟侥凤拷锟斤拷锟斤拷锟斤拷锟铰硷拷啤锟斤拷锟斤拷夥斤拷锟斤拷锟斤拷锟斤拷锟
 *
 * 通锟矫凤拷锟斤拷锟斤拷
 *      1.configUSE_PORT_OPTIMISED_TASK_SELECTION 为 0 锟斤拷锟斤拷硬锟斤拷锟斤拷支锟斤拷锟斤拷锟斤拷锟斤拷锟解方锟斤拷锟斤拷
 *      2.锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷FreeRTOS支锟街碉拷硬锟斤拷
 *      3.锟斤拷全锟斤拷C实锟街ｏ拷效锟斤拷锟皆碉拷锟斤拷锟斤拷锟解方锟斤拷锟斤拷
 *      4.锟斤拷强锟斤拷要锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟饺硷拷锟斤拷目
 * 锟斤拷锟解方锟斤拷锟斤拷
 *      1.锟斤拷锟诫将configUSE_PORT_OPTIMISED_TASK_SELECTION锟斤拷锟斤拷为1锟斤拷
 *      2.锟斤拷锟斤拷一锟斤拷锟斤拷锟斤拷锟截讹拷锟杰癸拷锟侥伙拷锟街革拷睿ㄒ伙拷锟斤拷锟斤拷锟斤拷萍锟斤拷锟角帮拷锟斤拷锟絒CLZ]指锟筋）锟斤拷
 *      3.锟斤拷通锟矫凤拷锟斤拷锟斤拷锟斤拷效
 *      4.一锟斤拷强锟斤拷锟睫讹拷锟斤拷锟斤拷锟斤拷锟斤拷锟饺硷拷锟斤拷目为32
 * 一锟斤拷锟斤拷硬锟斤拷锟斤拷锟斤拷前锟斤拷锟斤拷指锟筋，锟斤拷锟斤拷锟绞癸拷玫模锟組CU没锟斤拷锟斤拷些硬锟斤拷指锟斤拷幕锟斤拷撕锟接︼拷锟斤拷锟斤拷锟轿0锟斤拷
 */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1

/* 锟斤拷1锟斤拷使锟杰低癸拷锟斤拷tickless模式锟斤拷锟斤拷0锟斤拷锟斤拷锟斤拷系统锟斤拷锟侥ｏ拷tick锟斤拷锟叫讹拷一直锟斤拷锟斤拷
 * 锟斤拷锟借开锟斤拷锟酵癸拷锟侥的伙拷锟斤拷锟杰会导锟斤拷锟斤拷锟截筹拷锟斤拷锟斤拷锟解，锟斤拷为锟斤拷锟斤拷锟斤拷睡锟斤拷锟斤拷,锟斤拷锟斤拷锟斤拷锟铰办法锟斤拷锟
 *
 * 锟斤拷锟截凤拷锟斤拷锟斤拷
 *      1.锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟接猴拷
 *      2.锟斤拷住锟斤拷位锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟剿诧拷锟斤拷煽锟斤拷锟轿伙拷锟斤拷锟
 *
 *      1.通锟斤拷锟斤拷锟斤拷帽锟斤拷 BOOT 0 锟接高碉拷平(3.3V)
 *      2.锟斤拷锟斤拷锟较电，锟斤拷锟斤拷
 *
 * 			1.使锟斤拷FlyMcu锟斤拷锟斤拷一锟斤拷芯片锟斤拷然锟斤拷锟斤拷锟斤拷锟斤拷锟
 *			STMISP -> 锟斤拷锟叫酒(z)
 */
#define configUSE_TICKLESS_IDLE 0

/*
 * 写锟斤拷实锟绞碉拷CPU锟节猴拷时锟斤拷频锟绞ｏ拷也锟斤拷锟斤拷CPU指锟斤拷执锟斤拷频锟绞ｏ拷通锟斤拷锟斤拷为Fclk
 * Fclk为锟斤拷锟斤拷CPU锟节核碉拷时锟斤拷锟脚号ｏ拷锟斤拷锟斤拷锟斤拷说锟斤拷cpu锟斤拷频为 XX MHz锟斤拷
 * 锟斤拷锟斤拷指锟斤拷锟斤拷锟绞憋拷锟斤拷藕牛锟斤拷锟接︼拷模锟1/Fclk锟斤拷为cpu时锟斤拷锟斤拷锟节ｏ拷
 */
#define configCPU_CLOCK_HZ (SystemCoreClock)

// RTOS系统锟斤拷锟斤拷锟叫断碉拷频锟绞★拷锟斤拷一锟斤拷锟叫断的达拷锟斤拷锟斤拷每锟斤拷锟叫讹拷RTOS锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷
#define configTICK_RATE_HZ ((TickType_t)1000)

// 锟斤拷使锟矫碉拷锟斤拷锟斤拷锟斤拷燃锟
#define configMAX_PRIORITIES (32)

// 锟斤拷锟斤拷锟斤拷锟斤拷使锟矫的讹拷栈锟斤拷小
#define configMINIMAL_STACK_SIZE ((unsigned short)128)

// 锟斤拷锟斤拷锟斤拷锟斤拷锟街凤拷锟斤拷锟斤拷锟斤拷
#define configMAX_TASK_NAME_LEN (16)

// 系统锟斤拷锟侥硷拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟酵ｏ拷1锟斤拷示为16位锟睫凤拷锟斤拷锟斤拷锟轿ｏ拷0锟斤拷示为32位锟睫凤拷锟斤拷锟斤拷锟斤拷
#define configUSE_16_BIT_TICKS 0

// 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟紺PU使锟斤拷权锟斤拷锟斤拷锟斤拷同锟斤拷锟饺硷拷锟斤拷锟矫伙拷锟斤拷锟斤拷
#define configIDLE_SHOULD_YIELD 1

// 锟斤拷锟矫讹拷锟斤拷
#define configUSE_QUEUE_SETS 0

// 锟斤拷锟斤拷锟斤拷锟斤拷通知锟斤拷锟杰ｏ拷默锟较匡拷锟斤拷
#define configUSE_TASK_NOTIFICATIONS 1

// 使锟矫伙拷锟斤拷锟脚猴拷锟斤拷
#define configUSE_MUTEXES 0

// 使锟矫递归互锟斤拷锟脚猴拷锟斤拷
#define configUSE_RECURSIVE_MUTEXES 0

// 为1时使锟矫硷拷锟斤拷锟脚猴拷锟斤拷
#define configUSE_COUNTING_SEMAPHORES 0

/* 锟斤拷锟矫匡拷锟斤拷注锟斤拷锟斤拷藕锟斤拷锟斤拷锟斤拷锟较锟斤拷锟叫革拷锟斤拷 */
#define configQUEUE_REGISTRY_SIZE 10

#define configUSE_APPLICATION_TASK_TAG 0

/*****************************************************************
              FreeRTOS锟斤拷锟节达拷锟斤拷锟斤拷锟叫癸拷锟斤拷锟斤拷选锟斤拷
*****************************************************************/
// 支锟街讹拷态锟节达拷锟斤拷锟斤拷
#define configSUPPORT_DYNAMIC_ALLOCATION 1
// 支锟街撅拷态锟节达拷
#define configSUPPORT_STATIC_ALLOCATION 0
// 系统锟斤拷锟斤拷锟杰的堆达拷小
#define configTOTAL_HEAP_SIZE ((size_t)(36 * 1024))

/***************************************************************
             FreeRTOS锟诫钩锟接猴拷锟斤拷锟叫关碉拷锟斤拷锟斤拷选锟斤拷
**************************************************************/
/* 锟斤拷1锟斤拷使锟矫匡拷锟叫癸拷锟接ｏ拷Idle Hook锟斤拷锟斤拷锟节回碉拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷0锟斤拷锟斤拷锟皆匡拷锟叫癸拷锟斤拷
 *
 * 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷一锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷没锟斤拷锟绞碉拷郑锟
 * FreeRTOS锟芥定锟剿猴拷锟斤拷锟斤拷锟斤拷锟街和诧拷锟斤拷锟斤拷void vApplicationIdleHook(void )锟斤拷
 * 锟斤拷锟斤拷锟斤拷锟斤拷锟矫匡拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷诙锟斤拷岜伙拷锟斤拷锟
 * 锟斤拷锟斤拷锟窖撅拷删锟斤拷锟斤拷RTOS锟斤拷锟今，匡拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷头欧锟斤拷锟斤拷锟斤拷锟角的讹拷栈锟节存。
 * 锟斤拷吮锟斤拷氡Ｖわ拷锟斤拷锟斤拷锟斤拷锟斤拷锟皆憋拷CPU执锟斤拷
 * 使锟矫匡拷锟叫癸拷锟接猴拷锟斤拷锟斤拷锟斤拷CPU锟斤拷锟斤拷省锟斤拷模式锟角很筹拷锟斤拷锟斤拷
 * 锟斤拷锟斤拷锟皆碉拷锟矫伙拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟紸PI锟斤拷锟斤拷
 */
#define configUSE_IDLE_HOOK 0

/* 锟斤拷1锟斤拷使锟斤拷时锟斤拷片锟斤拷锟接ｏ拷Tick Hook锟斤拷锟斤拷锟斤拷0锟斤拷锟斤拷锟斤拷时锟斤拷片锟斤拷锟斤拷
 *
 *
 * 时锟斤拷片锟斤拷锟斤拷锟斤拷一锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷没锟斤拷锟绞碉拷郑锟
 * FreeRTOS锟芥定锟剿猴拷锟斤拷锟斤拷锟斤拷锟街和诧拷锟斤拷锟斤拷void vApplicationTickHook(void )
 * 时锟斤拷片锟叫断匡拷锟斤拷锟斤拷锟斤拷锟皆的碉拷锟斤拷
 * 锟斤拷锟斤拷锟斤拷锟斤拷浅锟斤拷锟叫★拷锟斤拷锟斤拷艽锟斤拷锟绞癸拷枚锟秸伙拷锟
 * 锟斤拷锟杰碉拷锟斤拷锟皆★拷FromISR" 锟斤拷 "FROM_ISR锟斤拷锟斤拷尾锟斤拷API锟斤拷锟斤拷
 */
/*xTaskIncrementTick锟斤拷锟斤拷锟斤拷锟斤拷xPortSysTickHandler锟叫断猴拷锟斤拷锟叫憋拷锟斤拷锟矫的★拷锟斤拷耍锟絭ApplicationTickHook()锟斤拷锟斤拷执锟叫碉拷时锟斤拷锟斤拷锟杰短诧拷锟斤拷*/
#define configUSE_TICK_HOOK 0

// 使锟斤拷锟节达拷锟斤拷锟斤拷失锟杰癸拷锟接猴拷锟斤拷
#define configUSE_MALLOC_FAILED_HOOK 0

/*
 * 锟斤拷锟斤拷0时锟斤拷锟矫讹拷栈锟斤拷锟斤拷锟解功锟杰ｏ拷锟斤拷锟绞癸拷么斯锟斤拷锟
 * 锟矫伙拷锟斤拷锟斤拷锟结供一锟斤拷栈锟斤拷锟斤拷锟斤拷雍锟斤拷锟斤拷锟斤拷锟斤拷使锟矫的伙拷
 * 锟斤拷值锟斤拷锟斤拷为1锟斤拷锟斤拷2锟斤拷锟斤拷为锟斤拷锟斤拷锟斤拷栈锟斤拷锟斤拷锟解方锟斤拷 */
#define configCHECK_FOR_STACK_OVERFLOW 0

/********************************************************************
          FreeRTOS锟斤拷锟斤拷锟斤拷时锟斤拷锟斤拷锟斤拷锟阶刺锟秸硷拷锟叫关碉拷锟斤拷锟斤拷选锟斤拷
**********************************************************************/
// 锟斤拷锟斤拷锟斤拷锟斤拷时锟斤拷统锟狡癸拷锟斤拷
#define configGENERATE_RUN_TIME_STATS 0
// 锟斤拷锟矫匡拷锟接伙拷锟斤拷锟劫碉拷锟斤拷
#define configUSE_TRACE_FACILITY 0
/* 锟斤拷锟絚onfigUSE_TRACE_FACILITY同时为1时锟斤拷锟斤拷锟斤拷锟斤拷锟3锟斤拷锟斤拷锟斤拷
 * prvWriteNameToBuffer()
 * vTaskList(),
 * vTaskGetRunTimeStats()
 */
#define configUSE_STATS_FORMATTING_FUNCTIONS 1

/********************************************************************
                FreeRTOS锟斤拷协锟斤拷锟叫关碉拷锟斤拷锟斤拷选锟斤拷
*********************************************************************/
// 锟斤拷锟斤拷协锟教ｏ拷锟斤拷锟斤拷协锟斤拷锟皆猴拷锟斤拷锟斤拷锟斤拷锟斤拷募锟絚routine.c
#define configUSE_CO_ROUTINES 0
// 协锟教碉拷锟斤拷效锟斤拷锟饺硷拷锟斤拷目
#define configMAX_CO_ROUTINE_PRIORITIES (2)

/***********************************************************************
                FreeRTOS锟斤拷锟斤拷锟斤拷锟斤拷时锟斤拷锟叫关碉拷锟斤拷锟斤拷选锟斤拷
**********************************************************************/
// 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷时锟斤拷
#define configUSE_TIMERS 0
// 锟斤拷锟斤拷锟斤拷时锟斤拷锟斤拷锟饺硷拷
#define configTIMER_TASK_PRIORITY (configMAX_PRIORITIES - 1)
// 锟斤拷锟斤拷锟斤拷时锟斤拷锟斤拷锟叫筹拷锟斤拷
#define configTIMER_QUEUE_LENGTH 10
// 锟斤拷锟斤拷锟斤拷时锟斤拷锟斤拷锟斤拷锟秸伙拷锟叫
#define configTIMER_TASK_STACK_DEPTH (configMINIMAL_STACK_SIZE * 2)

/************************************************************
            FreeRTOS锟斤拷选锟斤拷锟斤拷锟斤拷锟斤拷选锟斤拷
************************************************************/
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
// #define INCLUDE_xTaskGetCurrentTaskHandle       1
// #define INCLUDE_uxTaskGetStackHighWaterMark     0
// #define INCLUDE_xTaskGetIdleTaskHandle          0

/******************************************************************
            FreeRTOS锟斤拷锟叫讹拷锟叫关碉拷锟斤拷锟斤拷选锟斤拷
******************************************************************/
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS __NVIC_PRIO_BITS
#else
#define configPRIO_BITS 4
#endif
// 锟叫讹拷锟斤拷锟斤拷锟斤拷燃锟
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15

// 系统锟缴癸拷锟斤拷锟斤拷锟斤拷锟斤拷卸锟斤拷锟斤拷燃锟
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS)) /* 240 */

#define configMAX_SYSCALL_INTERRUPT_PRIORITY    0x50    /* 80 = 5 << (8-4) */

/****************************************************************
            FreeRTOS锟斤拷锟叫断凤拷锟斤拷锟斤拷锟叫关碉拷锟斤拷锟斤拷选锟斤拷
****************************************************************/
#define xPortPendSVHandler PendSV_Handler
#define vPortSVCHandler SVC_Handler

/* 锟斤拷锟斤拷为使锟斤拷Percepio Tracealyzer锟斤拷要锟侥讹拷锟斤拷锟斤拷锟斤拷锟斤拷要时锟斤拷 configUSE_TRACE_FACILITY 锟斤拷锟斤拷为 0 */
#if (configUSE_TRACE_FACILITY == 1)
#include "trcRecorder.h"
#define INCLUDE_xTaskGetCurrentTaskHandle 1 // 锟斤拷锟斤拷一锟斤拷锟斤拷选锟斤拷锟斤拷锟斤拷锟矫猴拷锟斤拷锟斤拷 Trace源锟斤拷使锟矫ｏ拷默锟较革拷值为0 锟斤拷示锟斤拷锟矫ｏ拷
#endif

#endif /* FREERTOS_CONFIG_H */
