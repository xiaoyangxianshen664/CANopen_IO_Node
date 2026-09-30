#include "co_sdo.h"
#include <stddef.h>

enum
{
    ABORT_TOGGLE = UINT32_C(0x05040001),
    ABORT_READ_WRITE = UINT32_C(0x06010002),
    ABORT_INDEX = UINT32_C(0x06020000),
    ABORT_SUBINDEX = UINT32_C(0x06090011),
    ABORT_LENGTH = UINT32_C(0x06070010),
    ABORT_TOO_LONG = UINT32_C(0x06070012),
    ABORT_TOO_SHORT = UINT32_C(0x06070013),
    ABORT_VALUE = UINT32_C(0x06090030),
    ABORT_STATE = UINT32_C(0x08000022)
};

static co_status_t validate(const co_sdo_t *sdo)
{
    if (sdo == NULL || sdo->node == NULL || sdo->node->tx == NULL ||
        sdo->device == NULL || sdo->device->table.entries != sdo->device->entries ||
        sdo->device->table.count != CO_DEVICE_OD_COUNT)
        return CO_ERR_ARGUMENT;
    if (sdo->node->node_id == 0u || sdo->node->node_id > CO_NODE_ID_MAX)
        return CO_ERR_NODE_ID;
    switch (sdo->node->state)
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

static int active(const co_nmt_state_t state)
{
    return state == CO_NMT_PRE_OPERATIONAL || state == CO_NMT_OPERATIONAL;
}

static uint32_t abort_for_status(co_status_t status, uint8_t requested,
                                 uint8_t expected)
{
    switch (status)
    {
    case CO_ERR_OD_NOT_FOUND:
        return ABORT_INDEX;
    case CO_ERR_OD_READ_ONLY:
        return ABORT_READ_WRITE;
    case CO_ERR_OD_STATE:
        return ABORT_STATE;
    case CO_ERR_OD_VALUE:
        return ABORT_VALUE;
    case CO_ERR_OD_LENGTH:
        if (requested > expected)
            return ABORT_TOO_LONG;
        if (requested < expected)
            return ABORT_TOO_SHORT;
        return ABORT_LENGTH;
    default:
        return ABORT_LENGTH;
    }
}

static co_status_t find_entry(const co_device_od_t *device, uint16_t index,
                              uint8_t subindex, const co_od_entry_t **entry,
                              uint32_t *abort_code)
{
    size_t i;
    co_status_t status = co_od_find(&device->table, index, subindex, entry);
    if (status == CO_OK)
        return CO_OK;
    for (i = 0u; i < device->table.count; ++i)
        if (device->table.entries[i].index == index)
        {
            *abort_code = ABORT_SUBINDEX;
            return CO_ERR_OD_NOT_FOUND;
        }
    *abort_code = ABORT_INDEX;
    return CO_ERR_OD_NOT_FOUND;
}

static can_frame_t response_base(const co_sdo_t *sdo, uint16_t index,
                                 uint8_t subindex)
{
    can_frame_t response = {0};
    response.id = CO_COB_SDO_TX_BASE + sdo->node->node_id;
    response.dlc = 8u;
    response.data[1] = (uint8_t)index;
    response.data[2] = (uint8_t)(index >> 8);
    response.data[3] = subindex;
    return response;
}

static co_status_t send_abort(const co_sdo_t *sdo, uint16_t index,
                              uint8_t subindex, uint32_t code)
{
    can_frame_t response = response_base(sdo, index, subindex);
    response.data[0] = CO_SDO_CMD_ABORT;
    response.data[4] = (uint8_t)code;
    response.data[5] = (uint8_t)(code >> 8);
    response.data[6] = (uint8_t)(code >> 16);
    response.data[7] = (uint8_t)(code >> 24);
    return co_send(sdo->node, &response);
}

static uint8_t upload_command(uint8_t length)
{
    static const uint8_t commands[5] = {0u, CO_SDO_CMD_UPLOAD_1,
                                        CO_SDO_CMD_UPLOAD_2,
                                        CO_SDO_CMD_UPLOAD_3,
                                        CO_SDO_CMD_UPLOAD_4};
    return length <= 4u ? commands[length] : 0u;
}

co_status_t co_sdo_init(co_sdo_t *sdo, co_context_t *node,
                        co_device_od_t *device)
{
    co_sdo_t candidate = {node, device};
    co_status_t status;
    if (sdo == NULL)
        return CO_ERR_ARGUMENT;
    status = validate(&candidate);
    if (status != CO_OK)
        return status;
    *sdo = candidate;
    return CO_OK;
}

co_status_t co_sdo_receive(co_sdo_t *sdo, const can_frame_t *request)
{
    /*1：创建临时变量*/
    co_rx_kind_t kind;                 // 阶段1分类器给出的报文类型
    const co_od_entry_t *entry = NULL; // 用来存找到的对象条目地址
    can_frame_t response;              // 准备发送给主站的响应帧结构体变量
    uint16_t index;                    // 请求中的16位对象索引
    uint8_t subindex;                  // 请求中的子索引
    uint8_t requested_length = 0u;     // Download请求声明的有效数据长度
    uint32_t abort_code = ABORT_INDEX; // 失败时要返回的Abort码
    uint8_t data[4] = {0};             // 暂存读取出的对象数据

    /*2：先验证 SDO 上下文*/
    co_status_t status = validate(sdo); // 当前操作返回状态
    if (status != CO_OK)
        return status;

    /*3：复用阶段 1 的接收分类器*/
    status = co_classify_rx(sdo->node, request, &kind);
    if (status != CO_OK)
        return status;
    /*4：状态门控，判断是否为SDO帧和当前状态NMT是否允许处理SDO*/
    if (kind != CO_RX_SDO || !active(sdo->node->state))
        return CO_IGNORED;
    /*5：检查 SDO 固定 DLC：当前项目的 SDO 只支持固定 8 字节帧，其余均不再往下处理*/
    if (request->dlc != 8u)
        return CO_ERR_DLC;
    /*6：将接收报文中的字节按小端组合为 Index 和 Sub-index。例如 data[1]=0x17、data[2]=0x10，组合得到 Index=0x1017。*/

    index = (uint16_t)request->data[1] | ((uint16_t)request->data[2] << 8);
    subindex = request->data[3];

    /*7：判断是否为读取命令还是写入命令*/
    switch (request->data[0])
    {
    case CO_SDO_CMD_UPLOAD:                                                     // #define CO_SDO_CMD_UPLOAD 0x40u ，此时表示主站请求读取对象。
        status = find_entry(sdo->device, index, subindex, &entry, &abort_code); /*8：查找对象，找到了返回CO_OK，进入下方的if，没找到就不进入下面这么大的if，函数调用 send_abort() 返回错误响应。*/
        if (status == CO_OK)
        {
            status = co_od_read(entry, data, entry->length); /*9：读取对象数据*/
            if (status != CO_OK)

                abort_code = abort_for_status(status, entry->length, entry->length); /*10：如果读取失败：bort_for_status（）会把阶段2的内部错误转换成SDO Abort码走出 if 后，函数调用 send_abort() 返回错误响应。*/
            else
            {
                response = response_base(sdo, index, subindex); /*11. 组装读取成功响应*/
                response.data[0] = upload_command(entry->length);
                for (requested_length = 0u; requested_length < entry->length; ++requested_length)
                    response.data[4u + requested_length] = data[requested_length]; // 然后复制读取出来的对象数据：
                return co_send(sdo->node, &response);                              /*12：发送读取成功响应*/
            }
        }
        return send_abort(sdo, index, subindex, abort_code); /*如果没找到条目对象，那么组装并发送一帧 SDO Abort 错误响应*/

    case CO_SDO_CMD_DOWNLOAD_1: // #define CO_SDO_CMD_DOWNLOAD_1 0x2F  ，此时表示主站写入1字节内容。
        requested_length = 1u;
        break;
    case CO_SDO_CMD_DOWNLOAD_2: // #define CO_SDO_CMD_DOWNLOAD_2 0x2B   ，此时表示主站写入2字节内容。
        requested_length = 2u;
        break;
    case CO_SDO_CMD_DOWNLOAD_3: // #define CO_SDO_CMD_DOWNLOAD_3 0x27   ，此时表示主站写入3字节内容。
        requested_length = 3u;
        break;
    case CO_SDO_CMD_DOWNLOAD_4: // #define CO_SDO_CMD_DOWNLOAD_4 0x23   ，此时表示主站写入4字节内容。
        requested_length = 4u;
        break;
    default:
        return send_abort(sdo, index, subindex, ABORT_TOGGLE); // 未知命令则返回，并且组装并发送一帧 SDO Abort 错误响应
    }

    /*13:查找要写入的对象,和读取一样，先确认对象存在。*/
    status = find_entry(sdo->device, index, subindex, &entry, &abort_code);
    if (status != CO_OK)
        return send_abort(sdo, index, subindex, abort_code);
    /*14：先检查对象是否可写*/
    if (entry->access != CO_OD_READ_WRITE)
        return send_abort(sdo, index, subindex, ABORT_READ_WRITE);
    /*15：检查请求长度和对象长度*/
    if (requested_length != entry->length)
        return send_abort(sdo, index, subindex, abort_for_status(CO_ERR_OD_LENGTH, requested_length, entry->length));
    /*16：调用阶段 2 的写入接口*/
    status = co_od_write(entry, sdo->node->state, request->data + 4u, requested_length);
    /*17：写入失败时，把阶段2返回的内部错误转换成Abort码，组装并发送SDO Abort响应。*/
    if (status != CO_OK)
        return send_abort(sdo, index, subindex, abort_for_status(status, requested_length, entry->length));
    /*18：组装并发送写入成功响应*/
    response = response_base(sdo, index, subindex);
    response.data[0] = CO_SDO_CMD_DOWNLOAD_OK;
    return co_send(sdo->node, &response);
}
