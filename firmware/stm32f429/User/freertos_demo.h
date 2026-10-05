#ifndef __FREERTOS_DEMO_H
#define __FREERTOS_DEMO_H

#include "co_types.h"

/* 创建当前工程的 FreeRTOS 任务并启动调度器。 */
void freertos_demo(void);

/* 供调试器和后续诊断任务读取的任务层健康快照。 */
extern volatile uint32_t freertos_io_sample_count;
extern volatile uint32_t freertos_monitor_tick_count;
extern volatile co_status_t freertos_diagnostics_last_error;
extern volatile uint32_t freertos_diagnostics_rx_overflow;
extern volatile uint32_t freertos_diagnostics_tx_busy;
extern volatile uint32_t freertos_diagnostics_can_error;
extern volatile uint32_t freertos_diagnostics_bus_off;

#endif
