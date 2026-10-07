#include "co_emcy.h"
#include "co_core.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                      \
    do                                                        \
    {                                                         \
        if (!(condition))                                     \
        {                                                     \
            fprintf(stderr, "check failed at line %d: %s\n", \
                    __LINE__, #condition);                    \
            return 1;                                         \
        }                                                     \
    } while (0)

typedef struct
{
    co_status_t next_status;
    uint32_t calls;
    can_frame_t last_frame;
} tx_mock_t;

static co_status_t mock_tx(void *user, const can_frame_t *frame)
{
    tx_mock_t *mock = user;
    mock->calls++;
    mock->last_frame = *frame;
    return mock->next_status;
}

static uint32_t read_od_value(const co_device_od_t *device, uint16_t index,
                             uint8_t subindex)
{
    const co_od_entry_t *entry = NULL;
    uint8_t bytes[4] = {0};
    uint32_t value = 0u;
    uint8_t i;

    if (co_od_find(&device->table, index, subindex, &entry) != CO_OK ||
        co_od_read(entry, bytes, entry->length) != CO_OK)
        return UINT32_MAX;
    for (i = 0u; i < entry->length; ++i)
        value |= (uint32_t)bytes[i] << (8u * i);
    return value;
}

static int test_report_and_clear(void)
{
    tx_mock_t mock = {CO_OK, 0u, {0}};
    co_context_t node;
    co_device_od_t device;
    co_emcy_t emcy;
    const uint8_t manufacturer[CO_EMCY_MANUFACTURER_LENGTH] = {1u, 2u, 3u, 4u, 5u};

    CHECK(co_device_od_init(&device, 1u, NULL) == CO_OK);
    CHECK(co_init(&node, 1u, mock_tx, &mock) == CO_OK);
    CHECK(co_emcy_init(&emcy, &node, &device) == CO_OK);

    CHECK(co_emcy_report(&emcy, CO_EMCY_ERROR_CAN_RX_OVERFLOW,
                         CO_EMCY_REGISTER_COMMUNICATION, manufacturer) == CO_OK);
    CHECK(mock.calls == 1u);
    CHECK(mock.last_frame.id == 0x081u && mock.last_frame.dlc == 8u);
    CHECK(mock.last_frame.data[0] == 0x01u && mock.last_frame.data[1] == 0xFFu);
    CHECK(mock.last_frame.data[2] == 0x11u);
    CHECK(memcmp(mock.last_frame.data + 3u, manufacturer, sizeof(manufacturer)) == 0);
    CHECK(read_od_value(&device, 0x1001u, 0u) == 0x11u);

    CHECK(co_emcy_report(&emcy, CO_EMCY_ERROR_CAN_RX_OVERFLOW,
                         CO_EMCY_REGISTER_COMMUNICATION, manufacturer) == CO_IGNORED);
    CHECK(mock.calls == 1u);

    CHECK(co_emcy_clear(&emcy) == CO_OK);
    CHECK(mock.calls == 2u);
    CHECK(mock.last_frame.id == 0x081u && mock.last_frame.dlc == 8u);
    CHECK(mock.last_frame.data[0] == 0u && mock.last_frame.data[1] == 0u);
    CHECK(mock.last_frame.data[2] == 0u);
    CHECK(mock.last_frame.data[3] == 0u && mock.last_frame.data[7] == 0u);
    CHECK(read_od_value(&device, 0x1001u, 0u) == 0u);
    CHECK(co_emcy_clear(&emcy) == CO_IGNORED);
    CHECK(mock.calls == 2u);
    return 0;
}

static int test_validation(void)
{
    tx_mock_t mock = {CO_OK, 0u, {0}};
    co_context_t node;
    co_device_od_t device;
    co_emcy_t emcy;

    CHECK(co_device_od_init(&device, 1u, NULL) == CO_OK);
    CHECK(co_init(&node, 1u, mock_tx, &mock) == CO_OK);
    CHECK(co_emcy_init(&emcy, &node, &device) == CO_OK);
    CHECK(co_emcy_report(&emcy, 0u, CO_EMCY_REGISTER_COMMUNICATION, NULL) == CO_ERR_OD_VALUE);
    CHECK(co_emcy_report(&emcy, CO_EMCY_ERROR_CAN, 0x20u, NULL) == CO_ERR_OD_VALUE);
    CHECK(co_emcy_report(&emcy, CO_EMCY_ERROR_CAN, 0x40u, NULL) == CO_ERR_OD_VALUE);
    CHECK(mock.calls == 0u);
    CHECK(read_od_value(&device, 0x1001u, 0u) == 0u);

    CHECK(co_emcy_init(NULL, &node, &device) == CO_ERR_ARGUMENT);
    CHECK(co_emcy_init(&emcy, NULL, &device) == CO_ERR_ARGUMENT);
    CHECK(co_emcy_init(&emcy, &node, NULL) == CO_ERR_ARGUMENT);
    return 0;
}

static int test_retry_and_latch(void)
{
    tx_mock_t mock = {CO_ERR_TX_BUSY, 0u, {0}};
    co_context_t node;
    co_device_od_t device;
    co_emcy_t emcy;
    const uint8_t manufacturer[CO_EMCY_MANUFACTURER_LENGTH] = {0u};

    CHECK(co_device_od_init(&device, 1u, NULL) == CO_OK);
    CHECK(co_init(&node, 1u, mock_tx, &mock) == CO_OK);
    CHECK(co_emcy_init(&emcy, &node, &device) == CO_OK);
    CHECK(co_emcy_report(&emcy, CO_EMCY_ERROR_BUS_OFF,
                         CO_EMCY_REGISTER_COMMUNICATION, manufacturer) == CO_ERR_TX_BUSY);
    CHECK(mock.calls == 1u && emcy.pending != 0u);
    CHECK(read_od_value(&device, 0x1001u, 0u) == 0x11u);
    CHECK(co_emcy_report(&emcy, CO_EMCY_ERROR_ADC,
                         CO_EMCY_REGISTER_VOLTAGE, manufacturer) == CO_ERR_TX_BUSY);
    CHECK(mock.calls == 1u);

    mock.next_status = CO_OK;
    CHECK(co_emcy_process(&emcy) == CO_OK);
    CHECK(mock.calls == 2u && emcy.pending == 0u && emcy.active != 0u);
    CHECK(mock.last_frame.data[0] == 0x02u && mock.last_frame.data[1] == 0xFFu);
    CHECK(co_emcy_report(&emcy, CO_EMCY_ERROR_BUS_OFF,
                         CO_EMCY_REGISTER_COMMUNICATION, manufacturer) == CO_IGNORED);
    CHECK(mock.calls == 2u);

    mock.next_status = CO_ERR_TX_BUSY;
    CHECK(co_emcy_clear(&emcy) == CO_ERR_TX_BUSY);
    CHECK(emcy.pending != 0u && emcy.pending_is_reset != 0u);
    CHECK(read_od_value(&device, 0x1001u, 0u) == 0u);
    mock.next_status = CO_OK;
    CHECK(co_emcy_process(&emcy) == CO_OK);
    CHECK(emcy.pending == 0u && emcy.active == 0u);
    CHECK(mock.last_frame.data[0] == 0u && mock.last_frame.data[1] == 0u);
    return 0;
}

static int test_node_id(void)
{
    tx_mock_t mock = {CO_OK, 0u, {0}};
    co_context_t node;
    co_device_od_t device;
    co_emcy_t emcy;

    CHECK(co_device_od_init(&device, 127u, NULL) == CO_OK);
    CHECK(co_init(&node, 127u, mock_tx, &mock) == CO_OK);
    CHECK(co_emcy_init(&emcy, &node, &device) == CO_OK);
    CHECK(co_emcy_report(&emcy, CO_EMCY_ERROR_GENERIC, 0u, NULL) == CO_OK);
    CHECK(mock.last_frame.id == 0x0FFu);
    CHECK(mock.last_frame.data[2] == CO_EMCY_REGISTER_GENERIC);
    return 0;
}

int main(void)
{
    CHECK(test_report_and_clear() == 0);
    CHECK(test_validation() == 0);
    CHECK(test_retry_and_latch() == 0);
    CHECK(test_node_id() == 0);
    return 0;
}
