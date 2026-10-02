#include "co_pdo.h"
#include <stdio.h>
#define CHECK(x)                                       \
    do                                                 \
    {                                                  \
        if (!(x))                                      \
        {                                              \
            fprintf(stderr, "%d: %s\n", __LINE__, #x); \
            return 1;                                  \
        }                                              \
    } while (0)
typedef struct
{
    unsigned sends;
    co_status_t result;
    can_frame_t frame;
} fake_t;
static co_status_t tx(void *user, const can_frame_t *frame)
{
    fake_t *f = user;
    ++f->sends;
    f->frame = *frame;
    return f->result;
}
static co_status_t write_value(co_device_od_t *d, uint16_t index, uint8_t sub, uint16_t value, uint8_t length)
{
    const co_od_entry_t *e;
    uint8_t data[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    co_status_t s = co_od_find(&d->table, index, sub, &e);
    return s == CO_OK ? co_od_write(e, CO_NMT_PRE_OPERATIONAL, data, length) : s;
}
int main(void)
{
    /*1. 创建测试对象*/
    co_context_t node = {0};     // 节点上下文
    co_device_od_t device = {0}; // 设备对象字典
    co_pdo_t pdo = {0};          // PDO 模块运行上下文
    fake_t fake = {0};           // 模拟发送环境
    can_frame_t frame = {0};     // 测试用 CAN 报文
    uint8_t out;                 // 用来读取 DO 输出值
    /*2. 检查空指针：验证三个公开接口都能拒绝空的 PDO 指针*/
    CHECK(co_pdo_init(NULL, &node, &device) == CO_ERR_ARGUMENT); // 返回→ 参数错误
    CHECK(co_pdo_receive(NULL, &frame) == CO_ERR_ARGUMENT);      // 返回→ 参数错误
    CHECK(co_pdo_process(NULL, 1u) == CO_ERR_ARGUMENT);          // 返回→ 参数错误
    /*3. 初始化节点、对象字典和 PDO，初始化完成后：此时还没有进入 Operational。*/
    CHECK(co_init(&node, 1u, tx, &fake) == CO_OK);
    CHECK(co_device_od_init(&device, 1u, NULL) == CO_OK);
    CHECK(co_pdo_init(&pdo, &node, &device) == CO_OK);

    /*4. 这段测试代码手动构造了一帧 RPDO1 接收报文*/
    frame.id = 0x201u;  // 节点 1 的 RPDO1的can-id是：0x201
    frame.dlc = 1u;     // DO的dlc项目规定为1字节
    frame.data[0] = 5u; // 数据 0x05 表示要写入 DO：
    /*5. 非 Operational 时忽略 RPDO，此时节点仍然是：CO_NMT_INITIALIZATION，所以即使报文 ID 和数据都正确，PDO 也不处理。*/
    CHECK(co_pdo_receive(&pdo, &frame) == CO_IGNORED);
    /*6. 进入 Operational 后接收 RPDO*/
    node.state = CO_NMT_OPERATIONAL;                                      // 现在状态允许 PDO 工作，接收流程是：
    CHECK(co_pdo_receive(&pdo, &frame) == CO_OK);                         // 识别为 RPDO1帧 → 检查 DLC = 1 →  检查高 4 位为 0 →  写入 0x6200:01（因为RPDO1的映射对象就是DO对应0x6200:01）
    CHECK(co_device_od_get_outputs(&device, &out) == CO_OK && out == 5u); // 验证对象字典中的 DO 值确实变成了： 5u

    /*7. 第一次处理 Operational，发送两个 TPDO*/
    CHECK(co_pdo_process(&pdo, 0u) == CO_OK);                                   // 第一次调用 co_pdo_process() 时：当前状态是 Operational，previous_operational 原来是 0，因此函数认为节点刚进入 Operational：pending_tpdo1置1，pending_tpdo2置1
    CHECK(fake.sends == 2u && fake.frame.id == 0x281u && fake.frame.dlc == 4u); // 于是连续发送：TPDO1（can-id：0x181）和TPDO2（can-id：0x281）发送次数从 0 变成 2。最后一次发送的是 TPDO2，所以：fake.frame.id == 0x281，fake.frame.dlc == 4字节
    /*8. DI 变化触发 TPDO1：更新设备输入：DI = 3，AI1 = 100 × 8，AI2 = 200 × 8*/
    CHECK(co_device_od_update_inputs(&device, 3u, 100u, 200u) == CO_OK);
    // PDO 模块比较：当前 DI = 3，上一次 last_di = 0，发现 DI 变化：pending_tpdo1 = 1;于是发送 TPDO1，上报数字输入值DI
    CHECK(co_pdo_process(&pdo, 0u) == CO_OK && fake.sends == 3u && fake.frame.id == 0x181u && fake.frame.data[0] == 3u); // 由于 0x6423:00 默认是 0，AI 变化此时不会触发 TPDO2。
    /*9. 打开 AI 变化触发并修改 AI*/
    CHECK(write_value(&device, 0x6423u, 0u, 1u, 1u) == CO_OK);                               // 把0x6423:00 置1，表示允许 AI 变化触发 TPDO2。
    CHECK(co_device_od_update_inputs(&device, 3u, 101u, 200u) == CO_OK);                     // 把AI1 从 100 变成 101，对象值从：800 → 808
    CHECK(co_pdo_process(&pdo, 0u) == CO_OK && fake.sends == 4u && fake.frame.id == 0x281u); // 允许 AI 变化触发 TPDO2后ai_enabled =变为1，AI1 发生变化，pending_tpdo2 = 1，随后发送发送 TPDO2。
    /*10. 设置 TPDO1 的事件周期*/
    CHECK(write_value(&device, 0x1800u, 5u, 100u, 2u) == CO_OK);         // TPDO1 设置为每 100 ms 周期触发。
    CHECK(co_pdo_process(&pdo, 99u) == CO_IGNORED);                      // 累计 99 ms，未到 100 ms 周期，pending_tpdo1 = 0，未发送。
    CHECK(co_pdo_process(&pdo, 1u) == CO_OK && fake.frame.id == 0x181u); // 再经过 1 ms：累计时间 = 99 + 1 = 100 ms，TPDO1 到期，发送：CAN-ID = 0x181
    /*11. 测试 RPDO 数据非法*/
    frame.data[0] = 0x80u; // 0x80 的高 4 位不可为 0：所以返回：CO_ERR_OD_VALUE
    CHECK(co_pdo_receive(&pdo, &frame) == CO_ERR_OD_VALUE);
    // RPDO1 固定只能有 1 个数据字节，现在 DLC 为 2
    frame.dlc = 2u;
    CHECK(co_pdo_receive(&pdo, &frame) == CO_ERR_DLC);
    /*12. 测试发送忙和重试*/
    fake.result = CO_ERR_TX_BUSY;                                        // 让模拟发送回调返回“发送忙”。
    CHECK(co_device_od_update_inputs(&device, 4u, 101u, 200u) == CO_OK); // 然后改变 DI：3 → 4 会触发 TPDO1。
    frame.dlc = 1u;
    CHECK(co_pdo_process(&pdo, 0u) == CO_ERR_TX_BUSY); // PDO 尝试发送 TPDO1，但模拟发送失败。因为发送失败时：pdo->pending_tpdo1不会被清零，所以这个发送任务仍然保留
    fake.result = CO_OK;                               // 恢复发送：
    CHECK(co_pdo_process(&pdo, 0u) == CO_OK);          // 再次调用：即使这次增加时间是 0 ms，仍然会发送，因为上一次失败后：pending_tpdo1 仍然是 1，这验证了 PDO 的失败重试机制。
    /*13. Stopped 状态忽略 PDO*/
    node.state = CO_NMT_STOPPED; // Stopped 状态下：RPDO 不处理，TPDO 不发送，计时清零，previous_operational 清零
    CHECK(co_pdo_receive(&pdo, &frame) == CO_IGNORED);
    CHECK(co_pdo_process(&pdo, 1000u) == CO_IGNORED);                           // 即使传入 1000 ms，也不会发送 PDO。
    puts("PDO: RPDO output, TPDO mapping, triggers, timing and gating passed"); // 表示所有 PDO 测试通过。
    return 0;
}
