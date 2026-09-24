/* 阶段 2 对象字典基础层：中文学习注释；文件编码 GB2312（代码页 936）。 */
#include "co_od.h"
#include <stddef.h> /* 提供 NULL */

/**
 * 用途：根据类型返回字节数（1、2、4）；不支持的类型返回 0。仅本文件使用。
 */
static uint8_t co_od_type_length(co_od_type_t type)
{
    switch (type)
    { /* 按对象类型选择标准长度 */
    case CO_OD_BOOLEAN:
    case CO_OD_UNSIGNED8:
        return 1u; /* 8 位无符号对象占 1 字节 */
    case CO_OD_INTEGER16:
    case CO_OD_UNSIGNED16:
        return 2u; /* 16 位无符号对象占 2 字节 */
    case CO_OD_UNSIGNED32:
        return 4u; /* 32 位无符号对象占 4 字节 */
    default:
        return 0u; /* 用 0 表示不支持的类型 */
    }
}

/**
 * 用途：把小端字节数组组合成整数，供写入范围检查使用。调用前须保证 data 有效且 length 为 1、2 或 4。
 */
static uint32_t co_od_decode_le(const uint8_t *data, uint8_t length)
{
    uint32_t value = 0u; /* 从全零开始组合整数 */
    uint8_t i;           /* 字节下标，从低字节开始处理 */

    for (i = 0u; i < length; ++i)
    {                                             /* 遍历有效字节，最低地址存低字节 */
        value |= ((uint32_t)data[i]) << (8u * i); /* 先转为 32 位，再左移 0/8/16/24 位并合入结果 */
    }
    return value; /* 返回解码后的整数 */
}

/**
 * 用途：按 index/subindex 查表。table 是表地址，entry 是调用者结果指针的地址。成功写入条目地址并返回 CO_OK；未找到返回 CO_ERR_OD_NOT_FOUND。
 */
co_status_t co_od_find(const co_od_table_t *table, uint16_t index,
                       uint8_t subindex, const co_od_entry_t **entry)
{
    size_t i; /* 对象数组下标 */

    if (table == NULL || entry == NULL)
    {                           /* 检查表地址和输出地址；此分支不会清空输出 */
        return CO_ERR_ARGUMENT; /* 参数无效，立即结束 */
    }
    *entry = NULL; /* 解引用输出地址，清空调用者的 found 指针 */
    if (table->entries == NULL && table->count != 0u)
    {                           /* 非空表必须有数组；空表允许 entries 为 NULL */
        return CO_ERR_ARGUMENT; /* 参数无效，立即结束 */
    }
    for (i = 0u; i < table->count; ++i)
    { /* 只扫描 count 个有效条目 */
        if (table->entries[i].index == index &&
            table->entries[i].subindex == subindex)
        {                                /* 索引和子索引必须同时匹配 */
            *entry = &table->entries[i]; /* 把条目地址写入调用者的指针变量，不复制条目 */
            return CO_OK;                /* 本次操作成功 */
        }
    }
    return CO_ERR_OD_NOT_FOUND; /* 遍历结束仍无匹配项 */
}

/**
 * 用途：检查 entry、data 和精确长度，再调用读回调填充 data。返回检查错误或回调结果；不直接操作硬件，也不校验读取值的范围。
 */
co_status_t co_od_read(const co_od_entry_t *entry, uint8_t *data,
                       uint8_t data_length)
{
    uint8_t expected_length; /* 根据类型得到的标准字节数 */

    if (entry == NULL || data == NULL)
    {                           /* 访问成员或缓冲区之前先检查空指针 */
        return CO_ERR_ARGUMENT; /* 参数无效，立即结束 */
    }
    expected_length = co_od_type_length(entry->type); /* 类型标记转换为标准长度 */
    if (expected_length == 0u || entry->length != expected_length ||
        data_length != entry->length)
    {                            /* 类型有效且三个长度必须一致；调用者须提供足够空间 */
        return CO_ERR_OD_LENGTH; /* 长度或类型描述错误，不执行回调 */
    }
    if (entry->read == NULL)
    {                              /* 检查是否绑定读取函数 */
        return CO_ERR_OD_CALLBACK; /* 缺少所需回调 */
    }
    return entry->read(entry->user, data, data_length); /* 传入辅助地址和输出缓冲区，原样返回回调状态 */
}

/**
 * 用途：检查 entry、data、长度、权限、状态和数值范围，再调用写回调。state 由调用者提供；校验失败不调用回调，回调结果原样返回。
 */
co_status_t co_od_write(const co_od_entry_t *entry, co_nmt_state_t state,
                        const uint8_t *data, uint8_t data_length)
{
    uint8_t expected_length; /* 根据类型得到的标准字节数 */
    int64_t value;          /* 待写入数据解码后的整数 */

    if (entry == NULL || data == NULL)
    {                           /* 访问成员或缓冲区之前先检查空指针 */
        return CO_ERR_ARGUMENT; /* 参数无效，立即结束 */
    }
    expected_length = co_od_type_length(entry->type); /* 类型标记转换为标准长度 */
    if (expected_length == 0u || entry->length != expected_length ||
        data_length != entry->length)
    {                            /* 类型有效且三个长度必须一致；调用者须提供足够空间 */
        return CO_ERR_OD_LENGTH; /* 长度或类型描述错误，不执行回调 */
    }
    if (entry->access != CO_OD_READ_WRITE)
    {                               /* 只有可读写对象允许写入 */
        return CO_ERR_OD_READ_ONLY; /* 拒绝写入，应用值不由本次检查修改 */
    }
    if (entry->write_preop_only && state != CO_NMT_PRE_OPERATIONAL)
    {                           /* 仅在限制标志非零时要求预运行状态 */
        return CO_ERR_OD_STATE; /* 当前状态不满足该对象写入限制 */
    }
    if (entry->write == NULL)
    {                              /* 检查是否绑定写入函数 */
        return CO_ERR_OD_CALLBACK; /* 缺少所需回调 */
    }
    value = co_od_decode_le(data, data_length); /* 长度已验证，安全解码最多 4 个字节 */
    /* 按16位补码解释有符号值，再进行范围检查。 */
    if (entry->type == CO_OD_INTEGER16 && value >= 0x8000)
        value -= 0x10000;
    if (entry->type == CO_OD_BOOLEAN && value > 1)
        return CO_ERR_OD_VALUE;
    if (value < entry->min_value || value > entry->max_value)
    {                           /* 最小值和最大值本身都允许写入 */
        return CO_ERR_OD_VALUE; /* 数值越界，不调用写回调 */
    }
    return entry->write(entry->user, data, data_length); /* 检查通过后调用一次写回调，失败也不在此重试 */
}
