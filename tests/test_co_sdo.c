#include "co_sdo.h"
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

static void request(can_frame_t *f, uint8_t command, uint16_t index,
                    uint8_t subindex)
{
    *f = (can_frame_t){0};
    f->id = 0x601;
    f->dlc = 8;
    f->data[0] = command;
    f->data[1] = (uint8_t)index;
    f->data[2] = (uint8_t)(index >> 8);
    f->data[3] = subindex;
}

static uint32_t abort_code(const can_frame_t *f)
{
    return (uint32_t)f->data[4] |
           ((uint32_t)f->data[5] << 8) |
           ((uint32_t)f->data[6] << 16) |
           ((uint32_t)f->data[7] << 24);
}

int main(void)
{
    /*1:创建测试对象*/
    co_context_t node = {0};     // 阶段1节点上下文
    co_device_od_t device = {0}; // 阶段2设备对象字典
    co_sdo_t sdo = {0};          // 阶段5 SDO服务对象，存两指针，指向node 和 device
    fake_t fake = {0};           // PC端模拟发送环境
    can_frame_t req = {0};       // 模拟主站发来的SDO请求帧
    /*2：测试空指针错误*/
    CHECK(co_sdo_init(NULL, &node, &device) == CO_ERR_ARGUMENT); // 没有提供保存初始化结果的 SDO 对象，返回CO_ERR_ARGUMENT
    CHECK(co_sdo_receive(NULL, &req) == CO_ERR_ARGUMENT);        // 没有提供有效的 SDO 上下文，所以也返回CO_ERR_ARGUMENT

    /*3：3. 初始化阶段 1、阶段 2 和阶段 5*/
    CHECK(co_init(&node, 1, tx, &fake) == CO_OK);        // 初始化节点：Node-ID  = 1，状态 = Initialization，发送函数 = tx，发送辅助数据 = &fake
    CHECK(co_device_od_init(&device, 1, NULL) == CO_OK); // 初始化对象字典：建立36个对象条目、36个数据存储单元及table连接关系
    CHECK(co_sdo_init(&sdo, &node, &device) == CO_OK);   // 初始化 SDO，完成sdo.node → node 和sdo.device → device
    /*4: 构造读取 0x1017:00 的请求：主站请求 Node-ID=1 的节点读取 0x1017:00*/
    request(&req, CO_SDO_CMD_UPLOAD, 0x1017, 0);
    /*5：节点处于 Initialization，虽然报文是发给本节点的 SDO，但当前状态不开放 SDO 服务，因此在 active() 状态门控处直接返回
          CO_IGNORED，不查对象、不读写对象，也不发送响应。CO_ERR_OD_STATE 只会在 SDO 已经进入对象写入流程后，某个对象本身不允许当前 NMT 状态写入时产生。*/
    CHECK(co_sdo_receive(&sdo, &req) == CO_IGNORED && fake.sends == 0u);
    /*6： 切换到 Pre-operational 后读取对象 ：测试直接修改节点状态，模拟阶段 3 已经完成 Boot-up，节点进入 Pre-operational*/
    node.state = CO_NMT_PRE_OPERATIONAL;
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK);                                   // 然后再次处理同一帧：
    CHECK(fake.frame.id == 0x581u && fake.frame.data[0] == CO_SDO_CMD_UPLOAD_2 && // 检查响应帧，我们返回给上位机的：并验证响应 ID  = 0x581，响应命令字  = 0x4B，读取成功并返回 2 字节，返回数据 = E8 03，即 1000
          fake.frame.data[4] == 0xE8u && fake.frame.data[5] == 0x03u);
    /*7. 写入新的 Heartbeat 周期*/
    request(&req, CO_SDO_CMD_DOWNLOAD_2, 0x1017, 0);
    req.data[4] = 0xF4;
    req.data[5] = 0x01;
    /*8：SDO 内部执行：找到 0x1017:00→确认可写→确认请求长度为 2→ 调用co_od_write()→对象值改为 500→返回 0x60（成功写入2字节内容），fake.frame.data[0] == CO_SDO_CMD_DOWNLOAD_OK是在验证Data[0] = 0x60，表示写入成功*/
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && fake.frame.data[0] == CO_SDO_CMD_DOWNLOAD_OK);
    /*9：再次读取，确认写入真的生效：重新构造读取请求，避免保留上一次 Download 命令。*/
    request(&req, CO_SDO_CMD_UPLOAD, 0x1017, 0);
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && fake.frame.data[4] == 0xF4u && fake.frame.data[5] == 0x01u); // 读取后检查Data[4] = F4 Data[5] = 01，确定对象字典的心跳周期从1000ms改为500ms
    /*10： 测试 Index 不存在，重新发送一个请求帧*/
    request(&req, CO_SDO_CMD_UPLOAD, 0x9999, 0);
    // 处理后检查，这里 co_sdo_receive() 返回 CO_OK，是因为：SDO 请求已经被正确处理，并成功发送了一帧 Abort 响应。CO_OK 不表示请求的对象存在，而表示服务处理和响应发送成功。
    // 同时检查：Data[0] = 0x80 和返回错误码Abort code = 0x06020000
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && fake.frame.data[0] == CO_SDO_CMD_ABORT && abort_code(&fake.frame) == 0x06020000u);
    /*11：测试 Sub-index 不存在*/
    request(&req, CO_SDO_CMD_UPLOAD, 0x6401, 3);
    // Index 存在但是Sub-index 不存在，返回错误码0x06090011u
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && abort_code(&fake.frame) == 0x06090011u);
    /*12： 测试数据长度过短*/
    request(&req, CO_SDO_CMD_DOWNLOAD_1, 0x1017, 0);                                      // 主站声明写入 1 字节但：0x1017:00 → UNSIGNED16，需要 2 字节，因此requested = 1，expected  = 2得到：requested < expected
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && abort_code(&fake.frame) == 0x06070013u); // 返回：ABORT_TOO_SHORT = 0x06070013
    /*13：测试写入只读对象*/
    request(&req, CO_SDO_CMD_DOWNLOAD_2, 0x6000, 1); // 对象：0x6000:01是数字输入对象，权限为：READ_ONLY，主站却使用 Download 命令尝试写入，所以返回：0x06010002
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && abort_code(&fake.frame) == 0x06010002u);
    /*14：测试 BOOLEAN 非法值*/
    request(&req, CO_SDO_CMD_DOWNLOAD_1, 0x6423, 0); // 对象：0x6423:00 类型是： BOOLEAN布尔值，开关用的，有效值只有：0 或 1 请求写入：2
    req.data[4] = 2;
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && abort_code(&fake.frame) == 0x06090030u); // 对象层：返回错误码co_od_write(...) == CO_ERR_OD_VALUE，SDO 层转换为：ABORT_VALUE = 0x06090030
    /*15：测试未知命令*/
    request(&req, 0x41, 0x1017, 0); // 0x41 不是当前支持的命令：0x2F → Download 1 字节 0x2B → Download 2 字节 0x27 → Download 3 字节 0x23 → Download 4 字节 ，这里命令虽然非法，但帧结构本身完整，所以可以正常生成 Abort 响应。
    // 这里命令虽然非法，但帧结构本身完整，所以可以正常生成 Abort 响应。
    CHECK(co_sdo_receive(&sdo, &req) == CO_OK && abort_code(&fake.frame) == 0x05040001u); // 因此进入：default:return send_abort(..., ABORT_TOGGLE);返回：0x05040001 表示：命令无效或当前版本不支持
    /*16： 测试 DLC 错误*/
    req.dlc = 7;                                     // 当前 SDO 要求：DLC = 8，函数在解析命令前检查：if (request->dlc != 8u)  return CO_ERR_DLC;
    CHECK(co_sdo_receive(&sdo, &req) == CO_ERR_DLC); // 这和未知命令不同：未知命令：帧格式完整，可以返回 Abort，DLC 错误：请求格式不完整，不能可靠解析，所以直接返回内部错误
    /*17：Stopped 状态下忽略 SDO*/

    req.dlc = 8;                                     // 先把 DLC 恢复为合法的：
    node.state = CO_NMT_STOPPED;                     // 再把节点切换到：CO_NMT_STOPPED
    CHECK(co_sdo_receive(&sdo, &req) == CO_IGNORED); // Stopped 不在允许范围内，因此：请求被忽略 不查对象 不发送 Abort，直接返回CO_IGNORED
    /*18：测试结束*/
    puts("SDO: upload, download, abort mapping and state gating passed"); // 所有 CHECK() 都通过后打印成功信息，并返回：
    return 0;                                                             // 测试程序返回 0，表示整个 SDO 测试通过。
}
