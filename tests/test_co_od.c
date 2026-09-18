/* 阶段 2 对象字典测试：中文学习注释；文件编码 GB2312（代码页 936）。 */
#include "co_od.h"
#include <stdio.h> /* 提供 fprintf */
#include <string.h> /* 提供 strcmp */

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
    return 1; } } while (0)

typedef struct {
    uint32_t value; /* 模拟对象当前数值 */
    unsigned reads; /* 记录读取回调次数 */
    unsigned writes; /* 记录写入回调次数 */
} fake_value_t;

static co_status_t fake_read(void *user, uint8_t *data, uint8_t length)
{
    fake_value_t *value = user; /* 还原回调私有数据指针 */
    uint8_t i;

    ++value->reads; /* 记录一次读取 */
    for (i = 0u; i < length; ++i) {
        data[i] = (uint8_t)(value->value >> (8u * i)); /* 按小端顺序输出 */
    }
    return CO_OK;
}

static co_status_t fake_write(void *user, const uint8_t *data, uint8_t length)
{
    fake_value_t *value = user; /* 还原回调私有数据指针 */
    uint8_t i;

    value->value = 0u; /* 写入前清除旧值 */
    for (i = 0u; i < length; ++i) {
        value->value |= ((uint32_t)data[i]) << (8u * i); /* 按小端顺序接收 */
    }
    ++value->writes; /* 记录一次写入 */
    return CO_OK;
}

static int test_find(void)
{
    fake_value_t value = {0};
    const co_od_entry_t entries[] = {
        {0x6200u, 0x01u, CO_OD_UNSIGNED8, CO_OD_READ_WRITE,
         1u, 0u, 15u, 0u, fake_read, fake_write, &value}
    };
    const co_od_table_t table = {entries, 1u};
    const co_od_entry_t *entry = NULL;

    CHECK(co_od_find(NULL, 0x6200u, 1u, &entry) == CO_ERR_ARGUMENT);
    CHECK(co_od_find(&table, 0x6200u, 1u, NULL) == CO_ERR_ARGUMENT);
    CHECK(co_od_find(&table, 0x6200u, 2u, &entry) == CO_ERR_OD_NOT_FOUND);
    CHECK(entry == NULL);
    CHECK(co_od_find(&table, 0x6200u, 1u, &entry) == CO_OK);
    CHECK(entry == &entries[0]);
    CHECK(entry->index == 0x6200u && entry->subindex == 1u);
    return 0;
}

static int test_read(void)
{
    fake_value_t value = {0x1234u, 0u, 0u};
    const co_od_entry_t entry = {
        0x6401u, 0x01u, CO_OD_UNSIGNED16, CO_OD_READ_ONLY,
        2u, 0u, 4095u, 0u, fake_read, NULL, &value
    };
    uint8_t data[2] = {0u, 0u};

    CHECK(co_od_read(NULL, data, 2u) == CO_ERR_ARGUMENT);
    CHECK(co_od_read(&entry, NULL, 2u) == CO_ERR_ARGUMENT);
    CHECK(co_od_read(&entry, data, 1u) == CO_ERR_OD_LENGTH);
    CHECK(co_od_read(&entry, data, 2u) == CO_OK);
    CHECK(data[0] == 0x34u && data[1] == 0x12u);
    CHECK(value.reads == 1u);
    return 0;
}

static int test_write(void)
{
    fake_value_t output = {0u, 0u, 0u};
    fake_value_t heartbeat = {0u, 0u, 0u};
    const co_od_entry_t read_only = {
        0x6000u, 0x01u, CO_OD_UNSIGNED8, CO_OD_READ_ONLY,
        1u, 0u, 15u, 0u, fake_read, NULL, &output
    };
    const co_od_entry_t writable = {
        0x6200u, 0x01u, CO_OD_UNSIGNED8, CO_OD_READ_WRITE,
        1u, 0u, 15u, 0u, fake_read, fake_write, &output
    };
    const co_od_entry_t preop_only = {
        0x1800u, 0x03u, CO_OD_UNSIGNED16, CO_OD_READ_WRITE,
        2u, 0u, 65535u, 1u, fake_read, fake_write, &heartbeat
    };
    uint8_t output_data[1] = {0x05u};
    uint8_t too_large[1] = {0x10u};
    uint8_t heartbeat_data[2] = {0x64u, 0x00u};

    CHECK(co_od_write(&read_only, CO_NMT_OPERATIONAL,
                      output_data, 1u) == CO_ERR_OD_READ_ONLY);
    CHECK(output.writes == 0u);
    CHECK(co_od_write(&writable, CO_NMT_OPERATIONAL,
                      output_data, 0u) == CO_ERR_OD_LENGTH);
    CHECK(co_od_write(&writable, CO_NMT_OPERATIONAL,
                      too_large, 1u) == CO_ERR_OD_VALUE);
    CHECK(output.writes == 0u);
    CHECK(co_od_write(&writable, CO_NMT_OPERATIONAL,
                      output_data, 1u) == CO_OK);
    CHECK(output.value == 5u && output.writes == 1u);
    CHECK(co_od_write(&preop_only, CO_NMT_OPERATIONAL,
                      heartbeat_data, 2u) == CO_ERR_OD_STATE);
    CHECK(heartbeat.writes == 0u);
    CHECK(co_od_write(&preop_only, CO_NMT_PRE_OPERATIONAL,
                      heartbeat_data, 2u) == CO_OK);
    CHECK(heartbeat.value == 100u && heartbeat.writes == 1u);
    return 0;
}

static int test_entry_definition(void)
{
    fake_value_t value = {0u, 0u, 0u};
    co_od_entry_t invalid_type = {
        0x2000u, 0x00u, (co_od_type_t)3, CO_OD_READ_ONLY,
        3u, 0u, 0u, 0u, fake_read, NULL, &value
    };
    co_od_entry_t invalid_length = {
        0x2000u, 0x01u, CO_OD_UNSIGNED16, CO_OD_READ_ONLY,
        1u, 0u, 0u, 0u, fake_read, NULL, &value
    };
    uint8_t data[2] = {0u, 0u};

    CHECK(co_od_read(&invalid_type, data, 3u) == CO_ERR_OD_LENGTH);
    CHECK(co_od_read(&invalid_length, data, 1u) == CO_ERR_OD_LENGTH);
    invalid_type.type = CO_OD_UNSIGNED8;
    invalid_type.length = 1u;
    invalid_type.read = NULL;
    CHECK(co_od_read(&invalid_type, data, 1u) == CO_ERR_OD_CALLBACK);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (strcmp(argv[1], "find") == 0) return test_find();
    if (strcmp(argv[1], "read") == 0) return test_read();
    if (strcmp(argv[1], "write") == 0) return test_write();
    if (strcmp(argv[1], "definition") == 0) return test_entry_definition();
    return 1;
}
