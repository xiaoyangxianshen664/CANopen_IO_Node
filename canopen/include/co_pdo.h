#ifndef CO_PDO_H
#define CO_PDO_H

#include "co_core.h"
#include "co_device_od.h"

typedef struct
{
    co_context_t *node;
    co_device_od_t *device;
    uint32_t elapsed_tpdo1;
    uint32_t elapsed_tpdo2;
    uint32_t observed_tpdo1_writes;
    uint32_t observed_tpdo2_writes;
    uint8_t last_di;
    uint16_t last_ai1;
    uint16_t last_ai2;
    uint8_t have_sample;
    uint8_t previous_operational;
    uint8_t pending_tpdo1;
    uint8_t pending_tpdo2;
} co_pdo_t;

co_status_t co_pdo_init(co_pdo_t *pdo, co_context_t *node,
                        co_device_od_t *device);
co_status_t co_pdo_receive(co_pdo_t *pdo, const can_frame_t *frame);
co_status_t co_pdo_process(co_pdo_t *pdo, uint32_t elapsed_ms);

#endif
