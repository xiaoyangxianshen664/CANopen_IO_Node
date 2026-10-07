#include "co_emcy.h"
#include "co_core.h"

#include <stddef.h>
#include <string.h>

static co_status_t validate(const co_emcy_t *emcy)
{
    /*1：例行检查*/
    if (emcy == NULL || emcy->node == NULL || emcy->node->tx == NULL || // emcy 本身不是空指针 ，emcy->node 已指向 CANopen 节点
        emcy->device == NULL ||                                         // 节点的发送接口 tx 已设置，否则无法发送 EMCY。
        emcy->device->table.entries != emcy->device->entries ||         // emcy->device 已指向设备对象字典
        emcy->device->table.count != CO_DEVICE_OD_COUNT)                // 对象字典表的 entries 确实指向设备自己的 entries 数组。
        return CO_ERR_ARGUMENT;                                         // 对象条目数量符合预期 CO_DEVICE_OD_COUNT

    if (emcy->node->node_id == 0u || emcy->node->node_id > CO_NODE_ID_MAX) // CANopen 节点 ID 的有效范围是 1 到 CO_NODE_ID_MAX（本项目最大值为 127）

        return CO_ERR_NODE_ID; // 0 是 NMT 广播地址，不能作为节点自己的 ID，所以这里单独返回 CO_ERR_NODE_ID。

    return CO_OK;
}

static uint8_t frame_equal(const can_frame_t *left, const can_frame_t *right)
{
    return (uint8_t)(left->id == right->id && left->dlc == right->dlc && // 检查 CAN-ID，数据长度
                     left->is_extended == right->is_extended &&          // 是否扩展帧、是否远程帧
                     left->is_remote == right->is_remote &&              // 是否 CAN FD 帧
                     left->is_fd == right->is_fd &&                      // 只要有一项不同，&& 后续比较就会短路，整个结果为假。
                     memcmp(left->data, right->data, sizeof(left->data)) == 0);
    // 比较两个内存区域的字节内容，sizeof(left->data) 是整个数据数组的大小
}

static can_frame_t make_frame(const co_emcy_t *emcy, uint16_t error_code,
                              uint8_t error_register,
                              const uint8_t manufacturer[CO_EMCY_MANUFACTURER_LENGTH])
{
    can_frame_t frame = {0}; // 先把要返回的报文帧清0
    uint8_t i;
    /*1：设置 EMCY 的 CAN-ID 和数据长度：*/
    frame.id = CO_EMCY_COB_BASE + emcy->node->node_id;
    frame.dlc = CO_EMCY_DATA_LENGTH;
    /*2：填入数据区前三个字节：*/
    frame.data[0] = (uint8_t)(error_code & 0xFFu); // 错误码占两个字节，按低字节在前放入报文
    frame.data[1] = (uint8_t)(error_code >> 8);
    frame.data[2] = error_register; // data[2] 放 Error Register

    if (manufacturer != NULL) // 如果提供了厂商信息，也就是第四个参数传入了manufacturer数组的首地址而非NULLC，就复制 5 个字节：
    {
        for (i = 0u; i < CO_EMCY_MANUFACTURER_LENGTH; ++i)
            frame.data[3u + i] = manufacturer[i];
    }

    return frame;
}

static void remember_active(co_emcy_t *emcy, const can_frame_t *frame,
                            uint8_t is_reset)
{
    uint8_t i;
    /*1：函数一开始先清除待发送状态：*/
    emcy->pending = 0u;          // pending记录是否有待重试的帧：0 表示无待发帧，1 表示 pending_frame 中有待重发报文
    emcy->pending_is_reset = 0u; // pending_is_reset 标记待重试帧的类型：0 表示故障报告帧，1 表示故障清除帧（在pending=1时有效）
    /*2：如果刚刚发送成功的是清除帧*/
    if (is_reset != 0u) // 非零表示清除帧成功发出
    {
        emcy->active = 0u;
        emcy->active_error_code = 0u;
        emcy->active_error_register = 0u;                                        // 函数把活动故障标志和保存的故障内容清零，然后 return 结束函数。
        memset(emcy->active_manufacturer, 0, sizeof(emcy->active_manufacturer)); // memset() 在这里把厂商信息数组的后5个字节都写成 0。
        return;
    }
    /*3：如果发送的是故障报告帧，没有进入上面的分支，就执行下面的代码*/
    emcy->active = 1u; // 表示现在有一个已成功报告、尚未清除的活动故障。随后从已发送帧的数据区取出故障内容：
    emcy->active_error_code = (uint16_t)frame->data[0] |
                              ((uint16_t)frame->data[1] << 8); // 前两个字节按低字节在前组合成错误码；
    emcy->active_error_register = frame->data[2];              // 第三个字节保存 Error Register。
    for (i = 0u; i < CO_EMCY_MANUFACTURER_LENGTH; ++i)         // 循环再把 frame->data[3..7] 复制到 active_manufacturer[0..4]。
        emcy->active_manufacturer[i] = frame->data[3u + i];
}

static co_status_t submit(co_emcy_t *emcy, const can_frame_t *frame,
                          uint8_t is_reset)
{
    co_status_t status = co_send(emcy->node, frame); // 这里调用 co_send() 发送 frame，并把返回状态保存到 status。

    if (status == CO_OK) // 发送成功后调用刚学过的 remember_active()：
    {
        remember_active(emcy, frame, is_reset); // is_reset == 0：这是故障报告帧，记录活动故障内容，is_reset != 0：这是故障清除帧，清除活动故障记录。
    }
    else
    {                                      // 发送失败
        emcy->pending_frame = *frame;      // 将待重试报文也仍然保存在 emcy 里。
        emcy->pending = 1u;                // 表示有帧需要重试。
        emcy->pending_is_reset = is_reset; // 记下待重试的是故障报告帧还是清除帧。
    }
    return status; // 把 co_send() 的结果返回给调用者，让上层知道这次发送成功还是失败。当前实现把所有非 CO_OK 的结果都暂存为待重试帧；稍后 																								  co_emcy_process() 会尝试重发。
}

co_status_t co_emcy_init(co_emcy_t *emcy, co_context_t *node,
                         co_device_od_t *device)
{
    co_emcy_t candidate = {0}; // 先创建一个临时结构体 candidate，并把所有字段清零。因此它的初始状态是没有活动故障、没有待重试帧。
    co_status_t status;

    if (emcy == NULL) // 如果调用者没有提供接收初始化结果的结构体地址，就无法保存模块状态
    {
        return CO_ERR_ARGUMENT; // 立即返回参数错误。
    }

    candidate.node = node;     // 执行到这里说明上面if bulk没有触发，emcy 不是空指针，node 也不是空指针，device 也不是空指针。于是把 node 和 device 保存到 candidate 里。
    candidate.device = device; // 设备对象字典地址放进临时结构体：

    status = validate(&candidate); // 调用 validate() 检查这些node 和 device是否有效
    if (status != CO_OK)           // 检查失败就返回错误，不把临时结构体写进 *emcy。检查通过后才执行：
    {
        return status;
    }

    *emcy = candidate; // 这是把整个结构体复制到调用者提供的位置：节点和设备指针被保存下来，其余运行状态保持为零。
    return CO_OK;      // 最后返回 CO_OK。
}

co_status_t co_emcy_report(co_emcy_t *emcy, uint16_t error_code,
                           uint8_t error_register,
                           const uint8_t manufacturer[CO_EMCY_MANUFACTURER_LENGTH]) // 参数分别是 EMCY 状态结构体、错误码、Error Register 位图，以及可选的 5 字节厂商信息。
{
    can_frame_t frame;           // frame结构体用于存EMCY 帧
    uint8_t normalized_register; // 存 Error Register和通用错误位 0x01相或的结果
    /*1. 检查模块和错误码*/
    co_status_t status = validate(emcy);

    if (status != CO_OK)
        return status;
    if (error_code == 0u) // 如果错误码是 0，则返回， 留给 EMCY 清除帧使用，因此不能用它报告故障
        return CO_ERR_OD_VALUE;
    /*2. 确保通用错误位，并检查保留位*/
    normalized_register = (uint8_t)(error_register | CO_EMCY_REGISTER_GENERIC); // 无论调用者传入什么，报告故障时都自动置上通用错误位 0x01。
    if ((normalized_register & 0x60u) != 0u)                                    // 0x60 对应 Error Register 中保留的 bit5、bit6
        return CO_ERR_OD_VALUE;                                                 // 如果这两位有任何一位被设置，输入就不接受，返回值错误。
    /*3. 组装 EMCY 帧*/
    frame = make_frame(emcy, error_code, normalized_register, manufacturer); // 调用刚学过的 make_frame()，根据节点号设置 CAN-ID，把错误码、规范化后的 Error Register 和厂商信息放进数据区。这里还没有发送，只是先把帧准备好。
    /*4. 如果已有待重试帧，先处理冲突*/
    if (emcy->pending != 0u) // pending=1 表示已经有一帧发送失败，正在等待重试：
    {
        if (emcy->pending_is_reset == 0u &&                  // pending_is_reset == 0u表示是故障帧
            frame_equal(&emcy->pending_frame, &frame) != 0u) // 并且待重试的是一模一样的故障报告帧，就返回 CO_IGNORED，不重复排入同一帧。
            return CO_IGNORED;
        return CO_ERR_TX_BUSY; // 如果待重试帧不同/或者是清洁帧，那么不改变这一帧
    }
    /*5. 对比Emcy帧，前后相同则不重复发送*/
    if (emcy->active != 0u && emcy->active_error_code == error_code && // emcy->active != 0，说明确认当前已经有一个活动故障
        emcy->active_error_register == normalized_register &&          // 比较之前已记录的错误码和这次准备上报的错误码是否相同
        memcmp(emcy->active_manufacturer, frame.data + 3u,             // 四个条件全部满足时，说明：当前故障 = 已经上报的活动故障
               CO_EMCY_MANUFACTURER_LENGTH) == 0)
        return CO_IGNORED; // 直接忽略，不重复发送，这次又报告完全相同的内容，就返回 CO_IGNORED，不会再次调用 submit()。
    /*6. 更新对象字典并提交发送：先把 Error Register 写入对象字典 0x1001:00*/
    status = co_device_od_set_error(emcy->device, normalized_register);
    if (status != CO_OK)
        return status;

    return submit(emcy, &frame, 0u); // 再调用 submit() 尝试发送。这里传入 is_reset=0，表示提交的是故障报告帧。
    /*如果发送成功，submit() 会让 remember_active() 保存活动故障内容；如果发送失败，submit() 会把整帧保存在 pending_frame 中，等待重试。
    注意：对象字典的 Error Register 是在发送尝试之前更新的，所以即使发送暂时失败，0x1001:00 也已经反映这次故障。*/
}

co_status_t co_emcy_clear(co_emcy_t *emcy)
{
    can_frame_t frame;                   // 先准备一个临时 CAN 帧
    co_status_t status = validate(emcy); // 再检查 EMCY 模块、节点、发送接口和对象字典是否有效。
    if (status != CO_OK)                 // 如果模块状态不合法，直接返回错误，不进行清除。
        return status;
    if (emcy->pending != 0u) // 检查是否已有待重试帧
        return emcy->pending_is_reset != 0u ? CO_IGNORED : CO_ERR_TX_BUSY;
    if (emcy->active == 0u)
        return CO_IGNORED;

    frame = make_frame(emcy, 0u, 0u, NULL);
    status = co_device_od_set_error(emcy->device, 0u);
    if (status != CO_OK)
        return status;

    return submit(emcy, &frame, 1u);
}

co_status_t co_emcy_process(co_emcy_t *emcy)
{
    co_status_t status = validate(emcy); // 先检查 EMCY 模块、节点、发送接口和对象字典是否仍然有效。

    if (status != CO_OK)
        return status;       // 如果 EMCY 状态无效，直接返回错误。
    if (emcy->pending == 0u) // 检查有没有待重试帧，没有则立马返回
        return CO_IGNORED;

    return submit(emcy, &emcy->pending_frame, emcy->pending_is_reset); // 重试保存的完整报文
}
