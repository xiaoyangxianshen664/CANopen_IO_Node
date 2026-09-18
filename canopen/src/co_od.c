/* 阶段 2 对象字典基础层：中文学习注释；文件编码 GB2312（代码页 936）。 */
#include "co_od.h"
#include <stddef.h> /* 提供 NULL */

static uint8_t co_od_type_length(co_od_type_t type)
{
    switch (type) {
    case CO_OD_UNSIGNED8:
        return 1u;
    case CO_OD_UNSIGNED16:
        return 2u;
    case CO_OD_UNSIGNED32:
        return 4u;
    default:
        return 0u;
    }
}

static uint32_t co_od_decode_le(const uint8_t *data, uint8_t length)
{
    uint32_t value = 0u;
    uint8_t i;

    for (i = 0u; i < length; ++i) {
        value |= ((uint32_t)data[i]) << (8u * i);
    }
    return value;
}

co_status_t co_od_find(const co_od_table_t *table, uint16_t index,
                       uint8_t subindex, const co_od_entry_t **entry)
{
    size_t i;

    if (table == NULL || entry == NULL) {
        return CO_ERR_ARGUMENT;
    }
    *entry = NULL;
    if (table->entries == NULL && table->count != 0u) {
        return CO_ERR_ARGUMENT;
    }
    for (i = 0u; i < table->count; ++i) {
        if (table->entries[i].index == index &&
            table->entries[i].subindex == subindex) {
            *entry = &table->entries[i];
            return CO_OK;
        }
    }
    return CO_ERR_OD_NOT_FOUND;
}

co_status_t co_od_read(const co_od_entry_t *entry, uint8_t *data,
                       uint8_t data_length)
{
    uint8_t expected_length;

    if (entry == NULL || data == NULL) {
        return CO_ERR_ARGUMENT;
    }
    expected_length = co_od_type_length(entry->type);
    if (expected_length == 0u || entry->length != expected_length ||
        data_length != entry->length) {
        return CO_ERR_OD_LENGTH;
    }
    if (entry->read == NULL) {
        return CO_ERR_OD_CALLBACK;
    }
    return entry->read(entry->user, data, data_length);
}

co_status_t co_od_write(const co_od_entry_t *entry, co_nmt_state_t state,
                        const uint8_t *data, uint8_t data_length)
{
    uint8_t expected_length;
    uint32_t value;

    if (entry == NULL || data == NULL) {
        return CO_ERR_ARGUMENT;
    }
    expected_length = co_od_type_length(entry->type);
    if (expected_length == 0u || entry->length != expected_length ||
        data_length != entry->length) {
        return CO_ERR_OD_LENGTH;
    }
    if (entry->access != CO_OD_READ_WRITE) {
        return CO_ERR_OD_READ_ONLY;
    }
    if (entry->write_preop_only && state != CO_NMT_PRE_OPERATIONAL) {
        return CO_ERR_OD_STATE;
    }
    if (entry->write == NULL) {
        return CO_ERR_OD_CALLBACK;
    }
    value = co_od_decode_le(data, data_length);
    if (value < entry->min_value || value > entry->max_value) {
        return CO_ERR_OD_VALUE;
    }
    return entry->write(entry->user, data, data_length);
}
