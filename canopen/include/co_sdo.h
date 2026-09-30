/* Stage 5: expedited CANopen SDO server. */
#ifndef CO_SDO_H
#define CO_SDO_H

#include "co_core.h"
#include "co_device_od.h"

#define CO_COB_SDO_TX_BASE UINT32_C(0x580)

#define CO_SDO_CMD_UPLOAD       0x40u
#define CO_SDO_CMD_DOWNLOAD_1   0x2Fu
#define CO_SDO_CMD_DOWNLOAD_2   0x2Bu
#define CO_SDO_CMD_DOWNLOAD_3   0x27u
#define CO_SDO_CMD_DOWNLOAD_4   0x23u
#define CO_SDO_CMD_UPLOAD_1     0x4Fu
#define CO_SDO_CMD_UPLOAD_2     0x4Bu
#define CO_SDO_CMD_UPLOAD_3     0x47u
#define CO_SDO_CMD_UPLOAD_4     0x43u
#define CO_SDO_CMD_DOWNLOAD_OK  0x60u
#define CO_SDO_CMD_ABORT        0x80u

typedef struct
{
    co_context_t *node;
    co_device_od_t *device;
} co_sdo_t;

co_status_t co_sdo_init(co_sdo_t *sdo, co_context_t *node,
                        co_device_od_t *device);
/* Process one fixed-DLC expedited SDO request and send one response when
 * the node is in Pre-operational or Operational state. */
co_status_t co_sdo_receive(co_sdo_t *sdo, const can_frame_t *request);

#endif
