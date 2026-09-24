#include "co_nmt.h"
#include <stddef.h>

static co_status_t validate(const co_nmt_t *nmt)
{
    if (nmt == NULL || nmt->node == NULL || nmt->node->tx == NULL ||
        nmt->device == NULL || nmt->device->table.entries != nmt->device->entries ||
        nmt->device->table.count != CO_DEVICE_OD_COUNT)
        return CO_ERR_ARGUMENT;
    if (nmt->node->node_id == 0 || nmt->node->node_id > CO_NODE_ID_MAX)
        return CO_ERR_NODE_ID;
    switch (nmt->node->state)
    {
    case CO_NMT_INITIALIZATION:
    case CO_NMT_PRE_OPERATIONAL:
    case CO_NMT_OPERATIONAL:
    case CO_NMT_STOPPED:
        return CO_OK;
    default:
        return CO_ERR_ARGUMENT;
    }
}

static void change_state(co_nmt_t *nmt, co_nmt_state_t state)
{
    if (nmt->node->state == state)
        return;
    nmt->node->state = state;
    if (nmt->notify != NULL)
        nmt->notify(nmt->user, state);
}

co_status_t co_nmt_init(co_nmt_t *nmt, co_context_t *node,
                        co_device_od_t *device, co_nmt_notify_fn notify, void *user)
{
    co_nmt_t candidate = {node, device, notify, user};
    co_status_t status;
    if (nmt == NULL)
        return CO_ERR_ARGUMENT;
    status = validate(&candidate);
    if (status != CO_OK)
        return status;
    if (node->state != CO_NMT_INITIALIZATION)
        return CO_ERR_ARGUMENT;
    *nmt = candidate;
    if (notify != NULL)
        notify(user, CO_NMT_INITIALIZATION);
    return CO_OK;
}

co_status_t co_nmt_bootup(co_nmt_t *nmt)
{
    can_frame_t frame = {0};
    co_status_t status = validate(nmt);
    if (status != CO_OK)
        return status;
    if (nmt->node->state != CO_NMT_INITIALIZATION)
        return CO_IGNORED;
    frame.id = UINT32_C(0x700) + nmt->node->node_id;
    frame.dlc = 1;
    status = co_send(nmt->node, &frame);
    if (status == CO_OK)
        change_state(nmt, CO_NMT_PRE_OPERATIONAL);
    return status;
}

co_status_t co_nmt_receive(co_nmt_t *nmt, const can_frame_t *frame)
{
    co_rx_kind_t kind;
    co_status_t status = validate(nmt);
    if (status != CO_OK)
        return status;
    status = co_classify_rx(nmt->node, frame, &kind);
    if (status != CO_OK)
        return status;
    if (kind != CO_RX_NMT || nmt->node->state == CO_NMT_INITIALIZATION)
        return CO_IGNORED;
    switch (frame->data[0])
    {
    case CO_NMT_START:
        change_state(nmt, CO_NMT_OPERATIONAL);
        break;
    case CO_NMT_STOP:
        change_state(nmt, CO_NMT_STOPPED);
        break;
    case CO_NMT_ENTER_PREOP:
        change_state(nmt, CO_NMT_PRE_OPERATIONAL);
        break;
    case CO_NMT_RESET_NODE:
    case CO_NMT_RESET_COMMUNICATION:
        /* Apply safe output/service notification before restoring parameters. */
        change_state(nmt, CO_NMT_INITIALIZATION);
        status = co_device_od_reset(nmt->device,
                                    (uint8_t)(frame->data[0] == CO_NMT_RESET_COMMUNICATION));
        if (status != CO_OK)
            return status;
        return co_nmt_bootup(nmt);
    default:
        return CO_IGNORED; // 未知命令
    }
    return CO_OK;
}
