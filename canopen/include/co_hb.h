/* Stage 4: CANopen Heartbeat Producer. */
#ifndef CO_HB_H
#define CO_HB_H

#include "co_core.h"
#include "co_device_od.h"

typedef struct
{
    co_context_t *node;
    co_device_od_t *device;
    uint32_t elapsed_ms;
    uint32_t observed_writes;
} co_hb_t;

/* Bind initialized objects. Does not send a frame or change the NMT state. */
co_status_t co_hb_init(co_hb_t *hb, co_context_t *node,
                       co_device_od_t *device);

/* Advance the producer by elapsed_ms and send at most one due heartbeat.
 * CO_IGNORED means disabled or not due; transport errors are returned as-is.
 * A successful frame contains 0x700 + Node-ID, DLC=1, and node->state. */
co_status_t co_hb_process(co_hb_t *hb, uint32_t elapsed_ms);

#endif
