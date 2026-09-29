#include "co_hb.h"
#include <stddef.h>

static co_status_t validate(const co_hb_t *hb)
{
    /*第一组参数检查 */
    /*Heartbeat 对象hb存在吗？节点对象node存在吗？节点有发送回调吗？*/
    /*设备对象字典存在吗？ 对象表内部绑定正确吗？  对象数量正确吗？*/
    if (hb == NULL || hb->node == NULL || hb->node->tx == NULL ||
        hb->device == NULL || hb->device->table.entries != hb->device->entries ||
        hb->device->table.count != CO_DEVICE_OD_COUNT)
        return CO_ERR_ARGUMENT;

    /*第二组检查 Node-ID*/
    if (hb->node->node_id == 0u || hb->node->node_id > CO_NODE_ID_MAX)
        return CO_ERR_NODE_ID;

    /*第三组检查：检查 NMT 状态*/
    switch (hb->node->state)
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

co_status_t co_hb_init(co_hb_t *hb, co_context_t *node,
                       co_device_od_t *device)
{
    /*1： 先创建临时对象 `candidate` */
    co_hb_t candidate = {node, device, 0u, 0u};
    /*2：定义临时变量*/
    uint16_t period;    //`period` 用于接收当前心跳周期：
    co_status_t status; //`status` 用于保存各个函数的返回状态：比如status = validate(&candidate);

    /*3：检查 `hb` 指针*/
    if (hb == NULL)
        return CO_ERR_ARGUMENT;

    /*4：检查候选配置candidate*/
    status = validate(&candidate);
    if (status != CO_OK)
        return status;
    /*5：读取当前 Heartbeat 信息*/
    status = co_device_od_get_heartbeat(device, &period, &candidate.observed_writes); // 读取当前 0x1017:00 的周期值和当前 0x1017:00 成功写入次数
    if (status != CO_OK)
        return status;
    /*6：`(void)period` 是什么意思？*/
    /*在 co_hb_init() 中，period 只是临时读出来，并没有保存到 co_hb_t 结构体里，我有意不使用 period 这个局部变量，请编译器不要因为它未使用而报警。*/
    /*为什么初始化时不保存周期？因为 co_hb_process() 每次运行都会重新读取当前周期，这样主站后来修改：Heartbeat 下一次处理时就能立即看到新的 500。而 co_hb_init() 只需要记住初始化时的写入次数：*/
    (void)period;
    /*7：把临时对象完整复制到调用者的 `hb` 中*/
    *hb = candidate;
    return CO_OK;
}

co_status_t co_hb_process(co_hb_t *hb, uint32_t elapsed_ms)
{
    /*1：准备局部变量*/
    can_frame_t frame = {0};
    uint16_t period;
    uint32_t writes;

    /*2：先检查 Heartbeat 上下文*/
    co_status_t status = validate(hb);

    if (status != CO_OK)
        return status;

    /*3： Initialization 状态不发送周期 Heartbeat*/
    if (hb->node->state == CO_NMT_INITIALIZATION)
    {
        hb->elapsed_ms = 0u;
        return CO_IGNORED;
    }
    /*4：从对象字典读取当前周期和写入次数*/
    status = co_device_od_get_heartbeat(hb->device, &period, &writes);
    if (status != CO_OK)
        return status;
    /*5：检查 Heartbeat 周期是否被重新写入*/
    if (writes != hb->observed_writes) //`hb->observed_writes` 保存的是上一次观察到的写入次数，不相等时进入if，writes写入时变为1，hb->observed_writes原始为0，if内部吧hb->observed_writes0变为1
    {
        hb->observed_writes = writes;
        hb->elapsed_ms = 0u;
    }
    /*6：周期为 0 时关闭 Heartbeat*/
    if (period == 0u)
    {
        hb->elapsed_ms = 0u;
        return CO_IGNORED;
    }
    /*7：累计经过的时间，并防止整数溢出*/
    if (elapsed_ms > UINT32_MAX - hb->elapsed_ms)
        hb->elapsed_ms = UINT32_MAX;
    else
        hb->elapsed_ms += elapsed_ms;

    /*8：未到周期，不发送*/
    if (hb->elapsed_ms < period)
        return CO_IGNORED;
    /*9：组装 Heartbeat 报文*/
    frame.id = UINT32_C(0x700) + hb->node->node_id;
    frame.dlc = 1u;
    frame.data[0] = (uint8_t)hb->node->state;
    /*10：通过统一发送入口发送*/
    status = co_send(hb->node, &frame);
    /*11：发送成功后保留多余时间，发送失败时不清零*/
    if (status == CO_OK)
        hb->elapsed_ms %= period;
    return status;
}
