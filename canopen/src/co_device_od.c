/* 阶段 2：项目对象与 RAM 变量绑定。文件编码 GB2312（代码页 936）。 */
#include "co_device_od.h"

/* 阅读顺序：definitions 定义对象规则 -> init 建立条目和存储连接 -> 回调读写存储。
 * definitions[i] 是规则模板，entries[i] 是可查找条目，fields[i] 保存当前值。
 * 槽位是数组下标，不是对象的 Index；例如 fields[9] 对应 0x6000:01。
 * 下列常量顺序与模板一致，模板调序时必须同步修改，仅本文件使用。 */

/*匿名枚举，用来占位，只需要几个表示位置的常量，比如数组data[1]就写成data[ERROR_SLOT] */
enum
{
    ERROR_SLOT = 1,
    HEARTBEAT_SLOT = 2,
    IDENTITY_SLOT = 4,
    DI_SLOT = 9,
    DO_SLOT = 11,
    AI1_SLOT = 13,
    AI2_SLOT = 14
};

typedef struct
{
    uint16_t index;                  /* 对象索引，例如 0x6200 */
    uint8_t subindex;                /* 子索引，例如 1；与 index 共同定位对象 */
    co_od_type_t type;               /* 内部数据类型标记，不等于 EDS 的类型编号 */
    uint8_t length, writable, preop; /* 字节数、是否可写、是否仅预运行可写 */
    uint32_t initial, maximum;       /* 初值与允许上限；本设备对象下限统一为 0 */
} definition_t;

/* 每行依次为：index、subindex、type、length、writable、preop、initial、maximum。
 * 例如 DO 行的 1,1,0,0,15 表示：1 字节、可写、不限定预运行、初值 0、最大 15。
 * const 模板只描述规则；运行时的数值另存在 device->fields，不修改模板。
 * COB-ID 行先保存基值，初始化才加 Node-ID；映射描述值不加 Node-ID。
 * ro 是对象字典写权限，不妨碍应用通过更新接口改变输入采样值。 */
static const definition_t definitions[CO_DEVICE_OD_COUNT] = {
    {0x1000, 0, CO_OD_UNSIGNED32, 4, 0, 0, 0x00070191, UINT32_MAX}, /* 设备类型：401 加 DI/DO/AI 功能标志，CiA 401-1 6.2.1，M=0 */
    {0x1001, 0, CO_OD_UNSIGNED8, 1, 0, 0, 0, 255},                  /* 错误寄存器，初始无故障 */
    {0x1017, 0, CO_OD_UNSIGNED16, 2, 1, 0, 1000, 65535},            /* 心跳周期，默认 1000 ms，0 关闭 */
    {0x1018, 0, CO_OD_UNSIGNED8, 1, 0, 0, 4, 4},                    /* Identity 最大子索引为 4 */
    {0x1018, 1, CO_OD_UNSIGNED32, 4, 0, 0, 0, UINT32_MAX},          /* Vendor-ID=0 是未分配开发占位 */
    {0x1018, 2, CO_OD_UNSIGNED32, 4, 0, 0, 1, UINT32_MAX},          /* 开发产品编号为 1 */
    {0x1018, 3, CO_OD_UNSIGNED32, 4, 0, 0, 0x10000, UINT32_MAX},    /* 开发版本号 0x00010000 */
    {0x1018, 4, CO_OD_UNSIGNED32, 4, 0, 0, 0, UINT32_MAX},          /* 开发序列号 0，不保证唯一 */
    {0x6000, 0, CO_OD_UNSIGNED8, 1, 0, 0, 1, 1},                    /* DI 有 1 个输入字节 */
    {0x6000, 1, CO_OD_UNSIGNED8, 1, 0, 0, 0, 15},                   /* 低四位为四路 DI 的逻辑状态 */
    {0x6200, 0, CO_OD_UNSIGNED8, 1, 0, 0, 1, 1},                    /* DO 有 1 个输出字节 */
    {0x6200, 1, CO_OD_UNSIGNED8, 1, 1, 0, 0, 15},                   /* 低四位为四路 DO 命令，范围 0..15 */
    {0x6401, 0, CO_OD_UNSIGNED8, 1, 0, 0, 2, 2},                    /* AI 有两路输入 */
    {0x6401, 1, CO_OD_INTEGER16, 2, 0, 0, 0, 32760},                 /* AI1：INTEGER16：单极性ADC左对齐，原始值乘8 */
    {0x6401, 2, CO_OD_INTEGER16, 2, 0, 0, 0, 32760},                 /* AI2：同样占两字节 */
    {0x1400, 0, CO_OD_UNSIGNED8, 1, 0, 0, 2, 2},                    /* RPDO1 最大子索引为 2 */
    {0x1400, 1, CO_OD_UNSIGNED32, 4, 0, 0, 0x200, UINT32_MAX},      /* RPDO1 的 COB-ID 基值，初始化加节点号 */
    {0x1400, 2, CO_OD_UNSIGNED8, 1, 0, 0, 255, 255},                /* RPDO1 传输类型固定为 255 */
    {0x1600, 0, CO_OD_UNSIGNED8, 1, 0, 0, 1, 1},                    /* RPDO1 固定映射一个对象 */
    {0x1600, 1, CO_OD_UNSIGNED32, 4, 0, 0, 0x62000108, UINT32_MAX}, /* 映射 DO：索引6200、子索引01、长度08位 */
    {0x1800, 0, CO_OD_UNSIGNED8, 1, 0, 0, 5, 5},                    /* TPDO1 最大子索引5，子索引4不实现 */
    {0x1800, 1, CO_OD_UNSIGNED32, 4, 0, 0, 0x40000180, UINT32_MAX}, /* TPDO1 基值；bit30=1 禁止 RTR，并非纯 CAN-ID */
    {0x1800, 2, CO_OD_UNSIGNED8, 1, 0, 0, 255, 255},                /* TPDO1 传输类型固定为 255 */
    {0x1800, 3, CO_OD_UNSIGNED16, 2, 0, 0, 0, 65535},               /* TPDO1 抑制时间，单位100us；固定0，COB-ID不可禁用时禁止修改 */
    {0x1800, 5, CO_OD_UNSIGNED16, 2, 1, 1, 0, 65535},               /* TPDO1 事件周期，单位ms，默认0关闭周期触发 */
    {0x1801, 0, CO_OD_UNSIGNED8, 1, 0, 0, 5, 5},                    /* TPDO2 最大子索引5，子索引4不实现 */
    {0x1801, 1, CO_OD_UNSIGNED32, 4, 0, 0, 0x40000280, UINT32_MAX}, /* TPDO2 基值；bit30=1 禁止 RTR */
    {0x1801, 2, CO_OD_UNSIGNED8, 1, 0, 0, 255, 255},                /* TPDO2 传输类型固定为 255 */
    {0x1801, 3, CO_OD_UNSIGNED16, 2, 0, 0, 0, 65535},               /* TPDO2 抑制时间，固定0，不可写 */
    {0x1801, 5, CO_OD_UNSIGNED16, 2, 1, 1, 0, 65535},             /* TPDO2 事件周期，默认0；需要100ms时在预运行配置 */
    {0x1A00, 0, CO_OD_UNSIGNED8, 1, 0, 0, 1, 1},                    /* TPDO1 固定映射一个对象 */
    {0x1A00, 1, CO_OD_UNSIGNED32, 4, 0, 0, 0x60000108, UINT32_MAX}, /* 映射 DI：6000:01，8位 */
    {0x1A01, 0, CO_OD_UNSIGNED8, 1, 0, 0, 2, 2},                    /* TPDO2 固定映射两个对象 */
    {0x1A01, 1, CO_OD_UNSIGNED32, 4, 0, 0, 0x64010110, UINT32_MAX}, /* 映射 AI1：6401:01，16位 */
    {0x1A01, 2, CO_OD_UNSIGNED32, 4, 0, 0, 0x64010210, UINT32_MAX},  /* 映射 AI2：6401:02，16位 */
    {0x6423, 0, CO_OD_BOOLEAN, 1, 1, 0, 0, 1} /* 模拟量变化触发总开关，默认关闭 */
};

/* 将实际存储值编码为小端字节；通用 OD 已验证缓冲区和长度。 */
static co_status_t read_value(void *user, uint8_t *data, uint8_t length)
{
    const co_device_value_t *cell = user; /* user 保存 fields[i] 的地址；cell 指向原存储，不创建副本 */
    uint8_t i;
    for (i = 0; i < length; ++i)
    {
        data[i] = (uint8_t)(cell->value >> (8u * i)); /* 右移后取低8位：data[0]先放最低字节 */
    }
    return CO_OK;
}

/* 只在通用层完成权限、长度、状态、范围检查后提交新值。 */
static co_status_t write_value(void *user, const uint8_t *data, uint8_t length)
{
    co_device_value_t *cell = user; /* 还原为存储指针，通过它修改原对象的值 */
    uint32_t next = 0;              /* 暂存解码结果，尚未写入原对象 */
    uint8_t i;
    for (i = 0; i < length; ++i)
    {
        next |= (uint32_t)data[i] << (8u * i); /* 低字节移0位，高字节依次移8/16/24位 */
    }
    cell->value = next; /* 字节全部组合完成后，提交到这个对象的 RAM 存储 */
    ++cell->writes;     /* 同值重写也产生一次配置更新。 */
    return CO_OK;
}

/* 用途：检查设备指针、表与自身数组的连接和条目数；真为非零，假为0。
 * && 短路保证 device==NULL 时不会访问其成员。这不是逐项完整性校验。
 * 只允许传入已初始化或清零的对象，不能传入内容不确定的自动变量。 */
static int ready(const co_device_od_t *device)
{
    return device != NULL && device->table.entries == device->entries &&
           device->table.count == CO_DEVICE_OD_COUNT;
}

/* 用途：根据只读模板建立设备的实际对象表并绑定存储。
 * 参数：device 为要初始化的结构体地址；node_id 为1..127；identity可为NULL。
 * 返回：成功CO_OK，非法参数/节点号提前返回，尚未修改device。
 * 注意：初始化后含自引用指针，不可按值复制；重初始化会恢复默认值，不发送报文。 */
co_status_t co_device_od_init(co_device_od_t *device, uint8_t node_id,
                              const co_device_identity_t *identity)
{
    size_t i;
    co_device_identity_t id = {0, 1, 0x10000, 0}; /* 本地身份副本，先填开发默认值 */
    if (device == NULL)
        return CO_ERR_ARGUMENT;
    if (node_id == 0 || node_id > CO_NODE_ID_MAX)
        return CO_ERR_NODE_ID;
    if (identity != NULL)
        id = *identity; /* 写入 device 前保存输入身份。 */
    for (i = 0; i < CO_DEVICE_OD_COUNT; ++i)
    {
        const definition_t *d = &definitions[i]; /* d指向第i条只读规则 */
        co_od_entry_t *e = &device->entries[i];  /* e指向要填写的第i条实际条目 */
        device->fields[i].value = d->initial;    /* 为本条目准备初始数据 */
        device->fields[i].writes = 0;            /* 初始化不算主站写操作 */
        if (d->subindex == 1 && (d->index == 0x1400 ||
                                 d->index == 0x1800 || d->index == 0x1801))
        {
            device->fields[i].value += node_id; /* 默认连接集与节点号绑定。 */
        }
        e->index = d->index; /*通过模板给实际对象条目赋值 */
        e->subindex = d->subindex;
        e->type = d->type;
        e->length = d->length;
        e->access = d->writable ? CO_OD_READ_WRITE : CO_OD_READ_ONLY; /* 条件 ? 真值 : 假值 */
        /* 动态只读值与不变的配置常量分别描述。 */
        if (!d->writable && d->index != 0x1001 &&
            !(d->index == 0x6000 && d->subindex != 0) &&
            !(d->index == 0x6401 && d->subindex != 0))
            e->access = CO_OD_CONSTANT;
        e->min_value = 0;
        e->max_value = d->maximum;
        e->write_preop_only = d->preop;
        e->read = read_value;                        /* 保存读函数地址，所有条目共用此函数 */
        e->write = d->writable ? write_value : NULL; /* 只读对象不绑定写回调 */
        e->user = &device->fields[i];                /* 关键连接：同一个回调通过不同user访问不同对象 */
    }
    device->fields[IDENTITY_SLOT].value = id.vendor_id;
    device->fields[IDENTITY_SLOT + 1].value = id.product_code;
    device->fields[IDENTITY_SLOT + 2].value = id.revision;
    device->fields[IDENTITY_SLOT + 3].value = id.serial;
    device->table.entries = device->entries; /* 数组转为首元素地址，供co_od_find查找 */
    device->table.count = CO_DEVICE_OD_COUNT;
    return CO_OK;
}

/* 用途：由应用提交四路逻辑 DI 和两路 ADC 原始值。
 * 参数：di低四位表示逻辑有效状态；ai1/ai2为0..4095。
 * 返回：参数或范围错误时整组不改；成功后对象读回调即可读到新值。
 * 这里接收采样结果，不直接读GPIO/ADC；串行访问下先全检查再提交。 */
co_status_t co_device_od_update_inputs(co_device_od_t *device, uint8_t di,
                                       uint16_t ai1, uint16_t ai2)
{
    if (!ready(device))
        return CO_ERR_ARGUMENT;
    if (di > 15 || ai1 > 4095 || ai2 > 4095)
        return CO_ERR_OD_VALUE;
    device->fields[DI_SLOT].value = di;
    device->fields[AI1_SLOT].value = (uint32_t)ai1 << 3; /* 正号位保留，12位有效值左对齐 */
    device->fields[AI2_SLOT].value = (uint32_t)ai2 << 3;
    return CO_OK;
}

/* 用途：应用通过 outputs 指向的变量取得当前 DO 命令。
 * 返回：CO_OK表示输出参数已填写；无效设备或NULL输出地址返回参数错误。
 * 本函数不控制引脚，也不负责停止状态的安全输出覆盖。 */
co_status_t co_device_od_get_outputs(const co_device_od_t *device, uint8_t *outputs)
{
    if (!ready(device) || outputs == NULL)
        return CO_ERR_ARGUMENT;
    *outputs = (uint8_t)device->fields[DO_SLOT].value; /* 解引用，修改调用方传入的变量 */
    return CO_OK;
}

/* 用途：应用更新0x1001错误寄存器，虽然该对象对主站只读。
 * 参数：error为错误位集合；bit6及CiA401保留bit5不能置1；任何错误自动带通用错误bit0。
 * 返回：非法参数/保留位错误不改旧值；成功仅更新RAM，不发送EMCY。 */
co_status_t co_device_od_set_error(co_device_od_t *device, uint8_t error)
{
    if (!ready(device))
        return CO_ERR_ARGUMENT;
    if ((error & 0x60u) != 0)
        return CO_ERR_OD_VALUE;
    device->fields[ERROR_SLOT].value = error ? (error | 1u) : 0u;
    return CO_OK;
}

/* 用途：读取心跳周期和该对象的成功写入计数，通过两个地址输出。
 * period_ms接收周期，writes接收计数；同值重写也会改变计数。
 * 返回：参数有效为CO_OK；NULL输出地址返回参数错误。
 * 后续心跳服务负责观察更新、重启计时；这里不计时、不发送心跳。 */
co_status_t co_device_od_get_heartbeat(const co_device_od_t *device,
                                       uint16_t *period_ms, uint32_t *writes)
{
    if (!ready(device) || period_ms == NULL || writes == NULL)
        return CO_ERR_ARGUMENT;
    *period_ms = (uint16_t)device->fields[HEARTBEAT_SLOT].value;
    *writes = device->fields[HEARTBEAT_SLOT].writes;
    return CO_OK;
}

/* Stage 3 reset: restore mutable values without rebuilding self pointers.
 * Identity/fixed configuration stay bound to this device instance. */
co_status_t co_device_od_reset(co_device_od_t *device, uint8_t communication_only)
{
    size_t i;
    if (!ready(device) || communication_only > 1u)
        return CO_ERR_ARGUMENT;
    for (i = 0; i < CO_DEVICE_OD_COUNT; ++i)
    {
        const definition_t *d = &definitions[i];
        if (device->entries[i].access == CO_OD_CONSTANT)
            continue;
        if (communication_only && (d->index < 0x1000 || d->index > 0x1FFF))
            continue;
        device->fields[i].value = d->initial;
        device->fields[i].writes = 0;
    }
    return CO_OK;
}
