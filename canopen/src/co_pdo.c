#include "co_pdo.h"
#include <stddef.h>

static co_status_t validate(const co_pdo_t *pdo)
{
    if (pdo == NULL || pdo->node == NULL || pdo->node->tx == NULL ||
        pdo->device == NULL || pdo->device->table.entries != pdo->device->entries ||
        pdo->device->table.count != CO_DEVICE_OD_COUNT)
        return CO_ERR_ARGUMENT;
    if (pdo->node->node_id == 0u || pdo->node->node_id > CO_NODE_ID_MAX)
        return CO_ERR_NODE_ID;
    switch (pdo->node->state)
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

static co_status_t read_value(const co_device_od_t *device, uint16_t index,
                              uint8_t subindex, uint8_t *data, uint8_t length)
{
    const co_od_entry_t *entry;
    co_status_t status = co_od_find(&device->table, index, subindex, &entry);
    return status == CO_OK ? co_od_read(entry, data, length) : status;
}

static co_status_t read_u8(const co_device_od_t *device,
                           uint16_t index,
                           uint8_t subindex,
                           uint8_t *value)
{
    return read_value(device, index, subindex, value, 1u);
}

static co_status_t read_u16(const co_device_od_t *device,
                            uint16_t index,
                            uint8_t subindex,
                            uint16_t *value)
{
    uint8_t data[2];

    co_status_t status =
        read_value(device, index, subindex, data, 2u);

    if (status == CO_OK)
        *value = (uint16_t)data[0] |
                 ((uint16_t)data[1] << 8);

    return status;
}

static co_status_t get_period(const co_device_od_t *device, uint16_t index,
                              uint16_t *period, uint32_t *writes)
{
    const co_od_entry_t *entry;
    co_status_t status = co_od_find(&device->table, index, 5u, &entry);
    if (status != CO_OK)
        return status;
    status = co_od_read(entry, (uint8_t *)period, 2u);
    if (status == CO_OK)
    {
        uint8_t *bytes = (uint8_t *)period;
        *period = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
        *writes = ((const co_device_value_t *)entry->user)->writes;
    }
    return status;
}

static co_status_t send_tpdo1(co_pdo_t *pdo)
{
    can_frame_t frame = {0};
    co_status_t status = read_u8(pdo->device, 0x6000u, 1u, &frame.data[0]);
    if (status != CO_OK)
        return status;
    frame.id = UINT32_C(0x180) + pdo->node->node_id;
    frame.dlc = 1u;
    return co_send(pdo->node, &frame);
}

static co_status_t send_tpdo2(co_pdo_t *pdo)
{
    can_frame_t frame = {0};
    co_status_t status = read_value(pdo->device, 0x6401u, 1u, frame.data, 2u);
    if (status != CO_OK)
        return status;
    status = read_value(pdo->device, 0x6401u, 2u, frame.data + 2u, 2u);
    if (status != CO_OK)
        return status;
    frame.id = UINT32_C(0x280) + pdo->node->node_id;
    frame.dlc = 4u;
    return co_send(pdo->node, &frame);
}

co_status_t co_pdo_init(co_pdo_t *pdo, co_context_t *node,
                        co_device_od_t *device)
{
    co_pdo_t candidate = {node, device, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
                          0u, 0u, 0u, 0u};
    co_status_t status;
    if (pdo == NULL)
        return CO_ERR_ARGUMENT;
    status = validate(&candidate);
    if (status != CO_OK)
        return status;
    status = read_u8(device, 0x6000u, 1u, &candidate.last_di);
    if (status != CO_OK)
        return status;
    status = read_u16(device, 0x6401u, 1u, &candidate.last_ai1);
    if (status != CO_OK)
        return status;
    status = read_u16(device, 0x6401u, 2u, &candidate.last_ai2);
    if (status != CO_OK)
        return status;
    candidate.have_sample = 1u;
    *pdo = candidate;
    return CO_OK;
}

co_status_t co_pdo_receive(co_pdo_t *pdo, const can_frame_t *frame)
{
    co_rx_kind_t kind;
    const co_od_entry_t *entry;
    co_status_t status = validate(pdo);
    if (status != CO_OK)
        return status;
    status = co_classify_rx(pdo->node, frame, &kind);
    if (status != CO_OK)
        return status;
    if (kind != CO_RX_RPDO1 || pdo->node->state != CO_NMT_OPERATIONAL)
        return CO_IGNORED;
    if (frame->dlc != 1u)
        return CO_ERR_DLC;
    if ((frame->data[0] & 0xF0u) != 0u)
        return CO_ERR_OD_VALUE;
    status = co_od_find(&pdo->device->table, 0x6200u, 1u, &entry);
    if (status != CO_OK)
        return status;
    return co_od_write(entry, pdo->node->state, frame->data, 1u);
}

co_status_t co_pdo_process(co_pdo_t *pdo, uint32_t elapsed_ms)
{
    /*1.：准备局部变量并验证 PDO*/
    uint8_t di, ai_enabled;              // 当前 DI 值，ai_enabled  0x6423:00，表示是否允许 AI 变化触发 TPDO2，是一个布尔值，只能写0和1
    uint8_t sent = 0u;                   // 存 ：本次是否至少成功发送了一帧
    uint16_t ai1, ai2, period1, period2; // 当前 AI1 值，当前 AI2 值，period1存TPDO1 事件周期 ，period2存TPDO2 事件周期
    uint32_t writes1, writes2;           // writes1 存 TPDO1 周期对象写入次数，writes2 存 TPDO2 周期对象写入次数
    co_status_t status = validate(pdo);  // 验证 PDO
    if (status != CO_OK)
        return status;
    /*2：非 Operational 状态直接忽略*/
    if (pdo->node->state != CO_NMT_OPERATIONAL)
    {
        pdo->previous_operational = 0u;
        pdo->elapsed_tpdo1 = 0u;
        pdo->elapsed_tpdo2 = 0u;
        return CO_IGNORED;
    }
    /*3：读取当前 DI、AI 和 AI 触发开关*/
    status = read_u8(pdo->device, 0x6000u, 1u, &di); // 读取：0x6000:01 → 当前 DI
    if (status != CO_OK)
        return status;
    status = read_u16(pdo->device, 0x6401u, 1u, &ai1); // 读取：0x6401:01 → AI1
    if (status != CO_OK)
        return status;
    status = read_u16(pdo->device, 0x6401u, 2u, &ai2); // 读取：0x6401:02 → AI2
    if (status != CO_OK)
        return status;
    status = read_u8(pdo->device, 0x6423u, 0u, &ai_enabled); // 读取：0x6423:00，它决定 AI 变化是否触发 TPDO2：0 → AI变化不触发 TPDO2，1 → AI变化触发 TPDO2
    if (status != CO_OK)                                     // 如果任何对象读取失败，立即返回错误，不继续处理。
        return status;
    /*4：读取两个 TPDO 的事件周期*/
    status = get_period(pdo->device, 0x1800u, &period1, &writes1); // 读取：0x1800:05 → TPDO1 事件周期
    if (status != CO_OK)
        return status;
    status = get_period(pdo->device, 0x1801u, &period2, &writes2); // 读取：0x1801:05 → TPDO2 事件周期
    if (status != CO_OK)
        return status;
    /*5：判断是否刚进入 Operational，当前是 NMT状态是Operational，但上一次 PDO 还没有记录为 Operational，也就是pdo->previous_operational = 0u;说明这是刚进入 Operational。*/
    /*于是安排：发送一次 TPDO1，发送一次 TPDO2，因此NMT进入 Operational 后会先发布一次当前的TPDO1和TPDO2，不需要等待输入变化或事件周期到期。 */
    /*
    先判断当前是不是 Operational
    ├── 不是 → 清零 previous_operational，直接返回
    └── 是   → 继续向下

        再判断 previous_operational 是否为 0
    ├── 0 → 刚进入 Operational，安排首次 TPDO
    └── 1 → 已经处于 Operational，不重复安排 */

    if (!pdo->previous_operational)
    {
        pdo->pending_tpdo1 = 1u;
        pdo->pending_tpdo2 = 1u;
        pdo->previous_operational = 1u;
        pdo->elapsed_tpdo1 = 0u;
        pdo->elapsed_tpdo2 = 0u;
    }
    /*6：判断 DI 是否变化：比较当前 DI和上一次保存的 DI*/
    if (di != pdo->last_di)
        pdo->pending_tpdo1 = 1u;
    /*7：判断 AI 是否变化：只有两个条件同时满足，才触发 TPDO2，0x6423:00 不为 0或者AI1 或 AI2 发生变化*/
    /*如果ai_enabled = 0，即使 AI 变化，也不会通过变化事件触发 TPDO2。*/
    if (ai_enabled && (ai1 != pdo->last_ai1 || ai2 != pdo->last_ai2))
        pdo->pending_tpdo2 = 1u;
    /*8：更新输入快照，把本次读取到的值保存为下一次比较用的“旧值”*/
    pdo->last_di = di;
    pdo->last_ai1 = ai1;
    pdo->last_ai2 = ai2;
    /*9： 检查事件周期配置是否被重新写入*/
    if (writes1 != pdo->observed_tpdo1_writes)
    {
        pdo->observed_tpdo1_writes = writes1; // 保存新的 writes 次数
        pdo->elapsed_tpdo1 = 0u;              // TPDO1 计时清零
    }
    /*10：TPDO2 同理：即使主站写入的是相同的周期值，只要写入次数改变，也会重新开始计时。*/
    if (writes2 != pdo->observed_tpdo2_writes)
    {
        pdo->observed_tpdo2_writes = writes2;
        pdo->elapsed_tpdo2 = 0u;
    }
    /*11：累加两个 PDO 的时间，两个 PDO 使用两个独立计时器，互不影响，它不是在这里“判断应该给 TPDO1 还是 TPDO2”，而是同一段经过的真实时间，同时累加到两个独立计时器*/
    if (elapsed_ms > UINT32_MAX - pdo->elapsed_tpdo1)
        pdo->elapsed_tpdo1 = UINT32_MAX;
    else
        pdo->elapsed_tpdo1 += elapsed_ms;
    if (elapsed_ms > UINT32_MAX - pdo->elapsed_tpdo2)
        pdo->elapsed_tpdo2 = UINT32_MAX;
    else
        pdo->elapsed_tpdo2 += elapsed_ms;
    /*12: 判断事件周期是否到期*/
    if (period1 != 0u && pdo->elapsed_tpdo1 >= period1)
        pdo->pending_tpdo1 = 1u;
    if (period2 != 0u && pdo->elapsed_tpdo2 >= period2)
        pdo->pending_tpdo2 = 1u;
    /*13: 发送 TPDO1*/
    if (pdo->pending_tpdo1)
    {
        status = send_tpdo1(pdo);
        if (status != CO_OK)
            return status;
        sent = 1u;
        pdo->pending_tpdo1 = 0u;
        pdo->elapsed_tpdo1 = period1 ? pdo->elapsed_tpdo1 % period1 : 0u;
    }
    /*14:发送 TPDO2*/
    if (pdo->pending_tpdo2)
    {
        status = send_tpdo2(pdo);
        if (status != CO_OK)
            return status;
        sent = 1u;
        pdo->pending_tpdo2 = 0u;
        pdo->elapsed_tpdo2 = period2 ? pdo->elapsed_tpdo2 % period2 : 0u;
    }
    return sent ? CO_OK : CO_IGNORED;
}
