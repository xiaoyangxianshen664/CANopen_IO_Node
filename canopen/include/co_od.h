/* 阶段 2 对象字典基础层：中文学习注释；文件编码 GB2312（代码页 936）。 */
#ifndef CO_OD_H
#define CO_OD_H

#include "co_types.h"
#include <stddef.h> /* 提供 size_t */
#include <stdint.h> /* 提供固定位宽整数类型 */

typedef enum
{
    CO_OD_BOOLEAN = 5,   /* 布尔对象：SDO使用1字节，合法值0/1 */
    CO_OD_UNSIGNED8 = 1,  /* 无符号 8 位整数，占 1 字节 */
    CO_OD_UNSIGNED16 = 2, /* 无符号 16 位整数，占 2 字节 */
    CO_OD_INTEGER16 = 6,  /* 有符号 16 位；内部标记，不是 EDS 类型编号 */
    CO_OD_UNSIGNED32 = 4  /* 无符号 32 位整数，占 4 字节 */
} co_od_type_t;

typedef enum
{
    CO_OD_CONSTANT = 2, /* 固定常量，不允许写入 */
    CO_OD_READ_ONLY = 0, /* 主站可以读，不能通过对象字典写入 */
    CO_OD_READ_WRITE = 1 /* 主站可以读，也可以写 */
} co_od_access_t;

/* 读取回调把对象当前值按 CANopen 小端字节序写入 data。 */
typedef co_status_t (*co_od_read_fn)(void *user, uint8_t *data, uint8_t length);

/* 写入回调接收已经完成长度、权限和范围检查的对象值。 */
typedef co_status_t (*co_od_write_fn)(void *user, const uint8_t *data, uint8_t length);

typedef struct
{
    uint16_t index;           /* 对象索引，例如 0x6200 */
    uint8_t subindex;         /* 对象子索引，例如 0x01 */
    co_od_type_t type;        /* 对象数据类型，同时决定标准长度 */
    co_od_access_t access;    /* 主站的读写权限 */
    uint8_t length;           /* 对象在 SDO/PDO 中占用的字节数 */
    int64_t min_value;        /* 写入值允许的最小值 */
    int64_t max_value;        /* 写入值允许的最大值 */
    uint8_t write_preop_only; /* 非零表示只允许 Pre-operational 状态写入 */
    co_od_read_fn read;       /* 读取对象值的回调 */
    co_od_write_fn write;     /* 写入对象值的回调；只读对象可为 NULL */
    void *user;               /* 回调使用的应用层私有数据 */
} co_od_entry_t;              // 对象条目结构体

typedef struct
{
    const co_od_entry_t *entries; /* 对象条目数组 */
    size_t count;                 /* 数组中有效条目数 */
} co_od_table_t;                  // 对象条目挂载的对象表

/**
 * 用途：根据 Index 和 Sub-index 查找对象条目。
 * 返回：找到时通过 entry 输出地址；未知对象返回 CO_ERR_OD_NOT_FOUND。
 */
co_status_t co_od_find(const co_od_table_t *table, uint16_t index,
                       uint8_t subindex, const co_od_entry_t **entry);

/**
 * 用途：读取对象值，并检查输出指针和长度。
 * 说明：ro 和 rw 对象都允许读取；数据按 CANopen 小端顺序交给回调。
 */
co_status_t co_od_read(const co_od_entry_t *entry, uint8_t *data,
                       uint8_t data_length);

/**
 * 用途：检查权限、状态、长度和值范围后写入对象。
 * 说明：write_preop_only 对象只有在 CO_NMT_PRE_OPERATIONAL 时允许写入。
 */
co_status_t co_od_write(const co_od_entry_t *entry, co_nmt_state_t state,
                        const uint8_t *data, uint8_t data_length);

#endif
