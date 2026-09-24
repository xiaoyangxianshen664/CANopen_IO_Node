/* 阶段 2 对象字典测试：中文学习注释；文件编码 GB2312（代码页 936）。 */
#include "co_od.h"
#include <stdio.h>  /* 提供 fprintf */
#include <string.h> /* 提供 strcmp */

/* 条件为假时打印位置和表达式，并退出当前测试函数；Release 构建也有效。 */
#define CHECK(expr)                                                    \
    do                                                                 \
    {                                                                  \
        if (!(expr))                                                   \
        {                                                              \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
            return 1;                                                  \
        }                                                              \
    } while (0)

typedef struct
{
    uint32_t value;  /* 模拟对象当前数值 */
    unsigned reads;  /* 记录读取回调次数 */
    unsigned writes; /* 记录写入回调次数 */
} fake_value_t;

/**
 * 用途：通过 user 找到 fake_value_t，统计读取次数并把 value 按小端写入 data。测试调用须保证指针有效、长度不超过 4；返回 CO_OK。
 */
static co_status_t fake_read(void *user, uint8_t *data, uint8_t length)
{
    fake_value_t *value = user; /* 还原回调私有数据指针 */
    uint8_t i;                  /* 字节下标，从低字节开始处理 */

    ++value->reads; /* 记录一次读取 */
    for (i = 0u; i < length; ++i)
    {                                                  /* 遍历有效字节，最低地址存低字节 */
        data[i] = (uint8_t)(value->value >> (8u * i)); /* 按小端顺序输出 */
    }
    return CO_OK; /* 本次操作成功 */
}

/**
 * 用途：通过 user 找到 fake_value_t，把小端 data 组合为 value 并累计写入次数。只修改模拟变量，不控制 GPIO；返回 CO_OK。
 */
static co_status_t fake_write(void *user, const uint8_t *data, uint8_t length)
{
    fake_value_t *value = user; /* 还原回调私有数据指针 */
    uint8_t i;                  /* 字节下标，从低字节开始处理 */

    value->value = 0u; /* 写入前清除旧值 */
    for (i = 0u; i < length; ++i)
    {                                                    /* 遍历有效字节，最低地址存低字节 */
        value->value |= ((uint32_t)data[i]) << (8u * i); /* 按小端顺序接收 */
    }
    ++value->writes; /* 记录一次写入 */
    return CO_OK;    /* 本次操作成功 */
}

/**
 * 用途：测试空参数、不存在的子索引以及成功查找后的条目地址。无参数；全部 CHECK 通过返回 0，失败立即返回 1。
 */
static int test_find(void)
{
    fake_value_t value = {0};        /* 模拟数值及读写次数均初始化为零 */
    const co_od_entry_t entries[] = {/* 建立只有一个数字输出条目的测试数组 */
                                     {0x6200u, 0x01u, CO_OD_UNSIGNED8, CO_OD_READ_WRITE,
                                      1u, 0u, 15u, 0u, fake_read, fake_write, &value}};
    const co_od_table_t table = {entries, 1u}; /* 表保存数组首地址和条目数量 */
    const co_od_entry_t *entry = NULL;         /* 用于接收查找结果地址 */

    CHECK(co_od_find(NULL, 0x6200u, 1u, &entry) == CO_ERR_ARGUMENT);       /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(co_od_find(&table, 0x6200u, 1u, NULL) == CO_ERR_ARGUMENT);       /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(co_od_find(&table, 0x6200u, 2u, &entry) == CO_ERR_OD_NOT_FOUND); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(entry == NULL);                                                  /* 未知对象时输出保持空指针 */
    CHECK(co_od_find(&table, 0x6200u, 1u, &entry) == CO_OK);               /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(entry == &entries[0]);                                           /* 结果指向原数组条目，不是新副本 */
    CHECK(entry->index == 0x6200u && entry->subindex == 1u);               /* 比较实际结果与预期；不相等就立即报告本组失败 */
    return 0;                                                              /* 本测试组全部检查通过 */
}

/**
 * 用途：测试读参数、精确长度、小端输出及回调次数。0x1234 是字节序测试值，不是合法的 12 位 ADC 示例；当前读取接口不做范围检查。
 */
static int test_read(void)
{
    fake_value_t value = {0x1234u, 0u, 0u}; /* 使用高低字节不同的值验证小端顺序 */
    const co_od_entry_t entry = {           /* 只读 16 位对象，绑定 fake_read 和模拟存储 */
                                 0x6401u, 0x01u, CO_OD_UNSIGNED16, CO_OD_READ_ONLY,
                                 2u, 0u, 4095u, 0u, fake_read, NULL, &value};
    uint8_t data[2] = {0u, 0u}; /* 读取结果缓冲区，先清零 */

    CHECK(co_od_read(NULL, data, 2u) == CO_ERR_ARGUMENT);    /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(co_od_read(&entry, NULL, 2u) == CO_ERR_ARGUMENT);  /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(co_od_read(&entry, data, 1u) == CO_ERR_OD_LENGTH); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(co_od_read(&entry, data, 2u) == CO_OK);            /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(data[0] == 0x34u && data[1] == 0x12u);             /* 低字节在前，高字节在后 */
    CHECK(value.reads == 1u);                                /* 只有合法读取调用过一次回调 */
    return 0;                                                /* 本测试组全部检查通过 */
}

/**
 * 用途：测试只读拒绝、长度与范围错误、正常写入及预运行限制。检查回调计数，确认失败检查不会提交写入。全部通过返回 0。
 */
static int test_write(void)
{
    fake_value_t output = {0u, 0u, 0u};    /* 模拟数字输出存储 */
    fake_value_t heartbeat = {0u, 0u, 0u}; /* 名字沿用 heartbeat，实际在此绑定 PDO 抑制时间测试条目 */
    const co_od_entry_t read_only = {      /* 只读条目不绑定写回调 */
                                     0x6000u, 0x01u, CO_OD_UNSIGNED8, CO_OD_READ_ONLY,
                                     1u, 0u, 15u, 0u, fake_read, NULL, &output};
    const co_od_entry_t writable = {/* 数字输出允许写入 0 至 15 */
                                    0x6200u, 0x01u, CO_OD_UNSIGNED8, CO_OD_READ_WRITE,
                                    1u, 0u, 15u, 0u, fake_read, fake_write, &output};
    const co_od_entry_t preop_only = {/* PDO 抑制时间条目，仅预运行状态可写 */
                                      0x1800u, 0x03u, CO_OD_UNSIGNED16, CO_OD_READ_WRITE,
                                      2u, 0u, 65535u, 1u, fake_read, fake_write, &heartbeat};
    uint8_t output_data[1] = {0x05u};           /* 合法数字输出命令，bit0 和 bit2 为 1 */
    uint8_t too_large[1] = {0x10u};             /* 16 超过四路输出允许的最大值 15 */
    uint8_t heartbeat_data[2] = {0x64u, 0x00u}; /* 小端数值 100，此处用于 PDO 抑制时间 */

    CHECK(co_od_write(&read_only, CO_NMT_OPERATIONAL,
                      output_data, 1u) == CO_ERR_OD_READ_ONLY); /* 检查多行写入调用的返回状态 */
    CHECK(output.writes == 0u);                                 /* 确认错误输入没有调用写回调 */
    CHECK(co_od_write(&writable, CO_NMT_OPERATIONAL,
                      output_data, 0u) == CO_ERR_OD_LENGTH); /* 检查多行写入调用的返回状态 */
    CHECK(co_od_write(&writable, CO_NMT_OPERATIONAL,
                      too_large, 1u) == CO_ERR_OD_VALUE); /* 检查多行写入调用的返回状态 */
    CHECK(output.writes == 0u);                           /* 确认错误输入没有调用写回调 */
    CHECK(co_od_write(&writable, CO_NMT_OPERATIONAL,
                      output_data, 1u) == CO_OK);     /* 检查多行写入调用的返回状态 */
    CHECK(output.value == 5u && output.writes == 1u); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(co_od_write(&preop_only, CO_NMT_OPERATIONAL,
                      heartbeat_data, 2u) == CO_ERR_OD_STATE); /* 检查多行写入调用的返回状态 */
    CHECK(heartbeat.writes == 0u);                             /* 确认错误输入没有调用写回调 */
    CHECK(co_od_write(&preop_only, CO_NMT_PRE_OPERATIONAL,
                      heartbeat_data, 2u) == CO_OK);          /* 检查多行写入调用的返回状态 */
    CHECK(heartbeat.value == 100u && heartbeat.writes == 1u); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    return 0;                                                 /* 本测试组全部检查通过 */
}

/**
 * 用途：使用故意错误的对象描述测试未知类型、长度不匹配和缺少读回调。0x2000 仅为测试条目，不是正式设备对象。
 */
static int test_entry_definition(void)
{
    fake_value_t value = {0u, 0u, 0u}; /* 为条目提供模拟存储和计数 */
    co_od_entry_t invalid_type = {     /* 故意使用不支持的类型标记 3 */
                                  0x2000u, 0x00u, (co_od_type_t)3, CO_OD_READ_ONLY,
                                  3u, 0u, 0u, 0u, fake_read, NULL, &value};
    co_od_entry_t invalid_length = {/* 故意把 16 位类型描述成 1 字节 */
                                    0x2000u, 0x01u, CO_OD_UNSIGNED16, CO_OD_READ_ONLY,
                                    1u, 0u, 0u, 0u, fake_read, NULL, &value};
    uint8_t data[2] = {0u, 0u}; /* 读取结果缓冲区，先清零 */

    CHECK(co_od_read(&invalid_type, data, 3u) == CO_ERR_OD_LENGTH);   /* 比较实际结果与预期；不相等就立即报告本组失败 */
    CHECK(co_od_read(&invalid_length, data, 1u) == CO_ERR_OD_LENGTH); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    invalid_type.type = CO_OD_UNSIGNED8;                              /* 先恢复合法类型 */
    invalid_type.length = 1u;                                         /* 同步恢复合法长度 */
    invalid_type.read = NULL;                                         /* 单独构造缺少读取回调的情况 */
    CHECK(co_od_read(&invalid_type, data, 1u) == CO_ERR_OD_CALLBACK); /* 比较实际结果与预期；不相等就立即报告本组失败 */
    return 0;                                                         /* 本测试组全部检查通过 */
}

/**
 * 用途：argv[1] 选择 find/read/write/definition 测试组；argc 含程序名应为 2。返回所选测试结果；无效参数返回 1。
 */
int main(int argc, char **argv)
{
    if (argc != 2)
        return 1; /* 需要程序名和一个测试组名 */
    if (strcmp(argv[1], "find") == 0)
        return test_find(); /* 匹配名称，执行该组并返回结果 */
    if (strcmp(argv[1], "read") == 0)
        return test_read(); /* 匹配名称，执行该组并返回结果 */
    if (strcmp(argv[1], "write") == 0)
        return test_write(); /* 匹配名称，执行该组并返回结果 */
    if (strcmp(argv[1], "definition") == 0)
        return test_entry_definition(); /* 匹配名称，执行该组并返回结果 */
    return 1;                           /* 未知测试组，向主程序调用环境报告失败 */
}
