#include "co_hb.h"
#include "co_core.h"
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

static co_status_t set_period(co_device_od_t *device, uint16_t period)
{
    const co_od_entry_t *entry;
    uint8_t data[2] = {(uint8_t)period, (uint8_t)(period >> 8)};
    co_status_t status = co_od_find(&device->table, 0x1017, 0, &entry);
    return status == CO_OK ? co_od_write(entry, CO_NMT_PRE_OPERATIONAL,
                                         data, 2)
                           : status;
}

int main(void)
{
    /*1：创建测试对象*/
    co_context_t node = {0};
    co_device_od_t device = {0};
    co_hb_t hb = {0};
    fake_t fake = {0};

    /*2：测试无效参数*/
    CHECK(co_hb_init(NULL, &node, &device) == CO_ERR_ARGUMENT);
    CHECK(co_hb_process(NULL, 1) == CO_ERR_ARGUMENT);

    /*3：初始化节点和对象字典*/
    CHECK(co_init(&node, 2, tx, &fake) == CO_OK);
    CHECK(co_device_od_init(&device, 2, NULL) == CO_OK);
    CHECK(co_hb_init(&hb, &node, &device) == CO_OK);

    /*4：Initialization 状态下不发送 Heartbeat*/
    CHECK(co_hb_process(&hb, 5000) == CO_IGNORED);
    CHECK(fake.sends == 0u);
    /*5：NMT 状态切换至Pre-operational*/
    node.state = CO_NMT_PRE_OPERATIONAL;
    /*6：测试未到期*/
    CHECK(co_hb_process(&hb, 999) == CO_IGNORED);
    CHECK(fake.sends == 0u);
    /*7：刚好到期及检查报文：*/
    CHECK(co_hb_process(&hb, 1) == CO_OK);
    CHECK(fake.sends == 1u && fake.frame.id == 0x702u && fake.frame.dlc == 1u);
    CHECK(fake.frame.data[0] == CO_NMT_PRE_OPERATIONAL);

    /*8：切换到 Operational，检查状态字节会变化*/
    node.state = CO_NMT_OPERATIONAL;
    CHECK(co_hb_process(&hb, 1000) == CO_OK);
    CHECK(fake.frame.data[0] == CO_NMT_OPERATIONAL);

    /*9：周期设置为 0，关闭 Heartbeat*/
    CHECK(set_period(&device, 0) == CO_OK);
    CHECK(co_hb_process(&hb, 5000) == CO_IGNORED && fake.sends == 2u);

    /*10：重新设置周期并测试*/
    CHECK(set_period(&device, 500) == CO_OK);
    CHECK(co_hb_process(&hb, 499) == CO_IGNORED);
    CHECK(co_hb_process(&hb, 1) == CO_OK && fake.sends == 3u);

    /*11：模拟发送忙，检查失败重试*/
    fake.result = CO_ERR_TX_BUSY;
    CHECK(co_hb_process(&hb, 500) == CO_ERR_TX_BUSY && fake.sends == 4u); // 这次又累计了 500 ms，于是再次自动进入发送流程，但模拟发送回调返回：CO_ERR_TX_BUSY

    /*12：发送恢复成功，立即重试*/
    fake.result = CO_OK;
    CHECK(co_hb_process(&hb, 0) == CO_OK && fake.sends == 5u);

    /*13：同值重写也会重新计时*/
    CHECK(set_period(&device, 500) == CO_OK); /* same value still restarts */
    CHECK(co_hb_process(&hb, 499) == CO_IGNORED);
    CHECK(co_hb_process(&hb, 1) == CO_OK && fake.sends == 6u);
    CHECK(set_period(&device, 0) == CO_OK);
    CHECK(co_hb_process(&hb, 500) == CO_IGNORED);
    puts("Heartbeat: timing, state byte, disable, rewrite and retry passed");
    return 0;
}
