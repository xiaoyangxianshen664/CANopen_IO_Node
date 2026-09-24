#include "co_nmt.h"
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
    unsigned sends, notifications;
    co_status_t result;
    can_frame_t frame;
    co_nmt_state_t state;
} fake_t;
static co_status_t tx(void *user, const can_frame_t *frame)
{
    fake_t *f = user;
    ++f->sends;
    f->frame = *frame;
    return f->result;
}
static void notify(void *user, co_nmt_state_t state)
{
    fake_t *f = user;
    ++f->notifications;
    f->state = state;
}
static co_status_t write_value(co_device_od_t *d, uint16_t index, uint8_t sub,
                               uint16_t value, uint8_t length)
{
    const co_od_entry_t *e;
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    co_status_t s = co_od_find(&d->table, index, sub, &e);
    return s == CO_OK ? co_od_write(e, CO_NMT_PRE_OPERATIONAL, bytes, length) : s;
}
static uint32_t value_at(co_device_od_t *d, uint16_t index, uint8_t sub)
{
    const co_od_entry_t *e;
    if (co_od_find(&d->table, index, sub, &e) != CO_OK)
        return UINT32_MAX;
    return ((co_device_value_t *)e->user)->value;
}
int main(void)
{
    /*1： 创建本次测试需要的变量*/
    co_context_t node = {0};
    co_device_od_t d = {0};
    co_nmt_t nmt = {0};
    fake_t f = {0};
    can_frame_t cmd = {0};
    const co_device_identity_t identity = {12, 34, 56, 78};
    unsigned id, target, command, before;
    const uint8_t commands[] = {1, 2, 128};
    const co_nmt_state_t states[] = {CO_NMT_OPERATIONAL, CO_NMT_STOPPED, CO_NMT_PRE_OPERATIONAL};

    /*2：先检查未准备好的输入*/
    CHECK(co_nmt_bootup(NULL) == CO_ERR_ARGUMENT);
    CHECK(co_nmt_receive(&nmt, NULL) == CO_ERR_ARGUMENT); /* frame传入NULL，拒绝空报文指针 */
    CHECK(co_device_od_reset(&d, 1) == CO_ERR_ARGUMENT);
    /*3： 对节点号 1～127，分别执行整套测试*/
    for (id = 1; id <= 127; ++id)
    {
        CHECK(co_init(&node, (uint8_t)id, tx, &f) == CO_OK);
        CHECK(co_device_od_init(&d, (uint8_t)id, &identity) == CO_OK);
        CHECK(co_nmt_init(&nmt, &node, &d, notify, &f) == CO_OK);
        /*4： 初始化中收到 Start，应当忽略*/
        cmd.dlc = 2;
        cmd.data[0] = 1;
        cmd.data[1] = 0;
        CHECK(co_nmt_receive(&nmt, &cmd) == CO_IGNORED);
        /*5：依次模拟 Boot-up 发送忙、失败、成功*/
        before = f.sends;
        f.result = CO_ERR_TX_BUSY;
        CHECK(co_nmt_bootup(&nmt) == CO_ERR_TX_BUSY);
        CHECK(node.state == CO_NMT_INITIALIZATION);
        f.result = CO_ERR_TX_FAILED;
        CHECK(co_nmt_bootup(&nmt) == CO_ERR_TX_FAILED);
        f.result = CO_OK;
        CHECK(co_nmt_bootup(&nmt) == CO_OK);
        CHECK(f.frame.id == 0x700 + id && f.frame.dlc == 1 && f.frame.data[0] == 0);
        CHECK(!f.frame.is_extended && !f.frame.is_remote && !f.frame.is_fd);
        CHECK(node.state == CO_NMT_PRE_OPERATIONAL && f.state == node.state);
        CHECK(co_nmt_bootup(&nmt) == CO_IGNORED && f.sends == before + 3);
        /*6：遍历目标地址，测试三种普通命令 */
        for (target = 0; target <= 255; ++target)
        {
            cmd.data[1] = (uint8_t)target;
            for (command = 0; command < 3; ++command)
            {
                co_nmt_state_t old = node.state;
                cmd.data[0] = commands[command];
                CHECK(co_nmt_receive(&nmt, &cmd) == ((target == 0 || target == id) ? CO_OK : CO_IGNORED));
                CHECK(node.state == ((target == 0 || target == id) ? states[command] : old));
                before = f.notifications;
                CHECK(co_nmt_receive(&nmt, &cmd) == ((target == 0 || target == id) ? CO_OK : CO_IGNORED));
                CHECK(before == f.notifications);
            }
        }
        /*7：遍历未知命令*/
        cmd.data[1] = 0;
        for (command = 0; command <= 255; ++command)
        {
            if (command == 1 || command == 2 || command == 128 || command == 129 || command == 130)
                continue;
            cmd.data[0] = (uint8_t)command;
            CHECK(co_nmt_receive(&nmt, &cmd) == CO_IGNORED);
        }
        /*8: 准备复位前的数据*/
        CHECK(write_value(&d, 0x1017, 0, 500, 2) == CO_OK);
        CHECK(write_value(&d, 0x1801, 5, 100, 2) == CO_OK);
        CHECK(write_value(&d, 0x6423, 0, 1, 1) == CO_OK);
        CHECK(write_value(&d, 0x6200, 1, 15, 1) == CO_OK);
        CHECK(co_device_od_update_inputs(&d, 5, 2048, 4095) == CO_OK);
        /*9：测试通信复位，并让这次 Boot-up 返回忙*/
        cmd.data[0] = 130;
        f.result = CO_ERR_TX_BUSY;
        CHECK(co_nmt_receive(&nmt, &cmd) == CO_ERR_TX_BUSY);
        CHECK(node.state == CO_NMT_INITIALIZATION);
        CHECK(value_at(&d, 0x1017, 0) == 1000 && value_at(&d, 0x1801, 5) == 0);
        CHECK(value_at(&d, 0x6423, 0) == 1 && value_at(&d, 0x6200, 1) == 15);
        CHECK(value_at(&d, 0x6401, 1) == 16384);
        f.result = CO_OK;
        CHECK(co_nmt_bootup(&nmt) == CO_OK);
        /*10：测试节点复位*/
        cmd.data[0] = 129;
        cmd.data[1] = (uint8_t)id;
        CHECK(co_nmt_receive(&nmt, &cmd) == CO_OK);
        CHECK(value_at(&d, 0x6423, 0) == 0 && value_at(&d, 0x6200, 1) == 0);
        CHECK(value_at(&d, 0x6401, 1) == 0);
        CHECK(value_at(&d, 0x1018, 1) == 12 && value_at(&d, 0x1018, 4) == 78);
        CHECK(value_at(&d, 0x1800, 1) == 0x40000180 + id);
    }
    /*11：循环结束后检查非法帧*/
    before = f.sends;
    CHECK(co_nmt_receive(&nmt, NULL) == CO_ERR_ARGUMENT); /*没有传入can报文帧的结构体地址*/
    cmd.dlc = 1;
    CHECK(co_nmt_receive(&nmt, &cmd) == CO_ERR_DLC); /* NMT应为DLC=2，这里少1字节 */
    cmd.dlc = 3;
    CHECK(co_nmt_receive(&nmt, &cmd) == CO_ERR_DLC); /* NMT应为DLC=2，这里多1字节 */
    cmd.dlc = 2;
    cmd.is_remote = 1;
    CHECK(co_nmt_receive(&nmt, &cmd) == CO_ERR_FRAME_TYPE); /* 传入RTR远程帧，不接受 */
    cmd.is_remote = 0;
    cmd.is_extended = 1;
    CHECK(co_nmt_receive(&nmt, &cmd) == CO_ERR_FRAME_TYPE); /* 传入扩展帧，不接受 */
    cmd.is_extended = 0;
    cmd.is_fd = 1;
    CHECK(co_nmt_receive(&nmt, &cmd) == CO_ERR_FRAME_TYPE); /* 传入CAN FD帧，不接受 */
    cmd.is_fd = 0;
    cmd.id = 0x800;
    CHECK(co_nmt_receive(&nmt, &cmd) == CO_ERR_CAN_ID); /* CAN-ID超出11位标准帧范围 */
    cmd.id = 0x600 + 127;
    CHECK(co_nmt_receive(&nmt, &cmd) == CO_IGNORED);                  /* 这是SDO请求ID，不是NMT，交给其他服务 */
    CHECK(f.sends == before && node.state == CO_NMT_PRE_OPERATIONAL); /* 非法/无关帧不应发送，也不应改变状态 */
    CHECK(co_device_od_reset(&d, 2) == CO_ERR_ARGUMENT);              /* reset范围参数只能是0或1，传入2非法 */
    puts("NMT: boot, routing, commands, reset scopes and failures passed");
    return 0;
}
