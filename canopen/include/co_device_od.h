/* 阶段 2：设备实际对象表。文件编码 GB2312（代码页 936）。 */
#ifndef CO_DEVICE_OD_H
#define CO_DEVICE_OD_H
#include "co_od.h"

#define CO_DEVICE_OD_COUNT 36u

/* 身份由应用提供；NULL 使用开发占位：Vendor=0、Product=1、Revision=0x10000、Serial=0。 */
typedef struct
{
    uint32_t vendor_id, product_code, revision, serial;
} co_device_identity_t;

/* 每个对象的 RAM 存储。writes 可用于观察同值重写，如重新启动心跳计时。 */
typedef struct
{
    uint32_t value;
    uint32_t writes;
} co_device_value_t;

/* 含指向自身成员的指针：初始化后不得按值复制或搬动；所有访问须串行。
 * fields/entries 是实现存储，不允许调用方绕过接口直接改配置。
 * 不申请动态内存，不访问 HAL，不发送报文。 */
typedef struct
{
    co_device_value_t fields[CO_DEVICE_OD_COUNT];
    co_od_entry_t entries[CO_DEVICE_OD_COUNT];
    co_od_table_t table;
} co_device_od_t;

/* 参数失败不改原对象；重新初始化恢复默认通信配置和安全输出 0。 */
co_status_t co_device_od_init(co_device_od_t *device, uint8_t node_id,
                              const co_device_identity_t *identity);
/* DI 为逻辑有效位（按下=1），AI 为原始 ADC；全部通过范围检查后一起更新。 */
co_status_t co_device_od_update_inputs(co_device_od_t *device, uint8_t di,
                                       uint16_t ai1, uint16_t ai2);
/* 应用读取 DO 命令，后续硬件层负责实际输出和故障安全覆盖。 */
co_status_t co_device_od_get_outputs(const co_device_od_t *device, uint8_t *outputs);
/* 设置通信错误寄存器；bit6 为保留位，必须为 0。具体故障映射在 EMCY 阶段完成。 */
co_status_t co_device_od_set_error(co_device_od_t *device, uint8_t error);
/* 返回周期和成功写入次数；计数自然回绕，心跳模块在串行处理写入后观察它。 */
co_status_t co_device_od_get_heartbeat(const co_device_od_t *device,
                                       uint16_t *period_ms, uint32_t *writes);
/* Reset mutable defaults; communication_only=1 preserves application values. */
co_status_t co_device_od_reset(co_device_od_t *device, uint8_t communication_only);
#endif
