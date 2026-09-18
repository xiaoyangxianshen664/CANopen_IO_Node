/* 阶段 1 纯 C 基础层：中文学习注释；文件编码 GB2312（代码页 936）。 */
/* 类型定义供协议层和适配层共同使用；此处不操作硬件。 */
#ifndef CO_TYPES_H
#define CO_TYPES_H

#include <stdint.h> /* 提供 uint8_t、uint32_t 等固定位宽整数类型 */

#define CO_CAN_ID_MAX UINT32_C(0x7FF) /* 11 位标准 CAN-ID 的最大值；32 位类型用于检测越界输入 */
#define CO_CAN_DATA_MAX 8u /* 经典 CAN 每帧最多 8 个数据字节 */
#define CO_NODE_ID_MAX 127u /* 节点号上限；0 仅用于 NMT 广播目标 */
#define CO_COB_NMT UINT32_C(0x000) /* NMT 固定 CAN-ID，目标节点在数据第 2 字节中 */
#define CO_COB_RPDO1_BASE UINT32_C(0x200) /* RPDO1 基值，加节点号后得到接收输出命令的 CAN-ID */
#define CO_COB_SDO_RX_BASE UINT32_C(0x600) /* SDO 请求基值；节点 1 接收 0x601 */

typedef enum {
    CO_OK = 0, /* 操作成功；分类接口中仅表示分类成功 */
    CO_IGNORED, /* 帧与本节点无关，正常忽略 */
    CO_ERR_ARGUMENT, /* 指针或必要回调无效 */
    CO_ERR_NODE_ID, /* 节点号不在 1~127 内 */
    CO_ERR_CAN_ID, /* CAN-ID 超过 11 位范围 */
    CO_ERR_DLC, /* 数据长度不符合当前检查要求 */
    CO_ERR_FRAME_TYPE, /* 不支持扩展、远程或 CAN FD 帧 */
    CO_ERR_TX_BUSY, /* 传输层暂忙，调用者决定后续处理 */
CO_ERR_TX_FAILED, /* 传输层发送失败 */
    CO_ERR_OD_NOT_FOUND, /* 对象字典中没有对应的 Index/Sub-index */
    CO_ERR_OD_READ_ONLY, /* 对象只读，拒绝写入 */
    CO_ERR_OD_LENGTH, /* 对象数据长度不匹配 */
    CO_ERR_OD_VALUE, /* 对象值超出允许范围 */
    CO_ERR_OD_CALLBACK, /* 对象缺少必要的读写回调 */
    CO_ERR_OD_STATE /* 当前 NMT 状态不允许修改对象 */
} co_status_t;

typedef enum {
    CO_NMT_INITIALIZATION = 0, /* 初始化状态，数值 0 也用于 Boot-up 通知 */
    CO_NMT_STOPPED = 4, /* 停止状态，对应状态字节 0x04 */
    CO_NMT_OPERATIONAL = 5, /* 运行状态，对应状态字节 0x05 */
    CO_NMT_PRE_OPERATIONAL = 127 /* 预运行状态，对应状态字节 0x7F */
} co_nmt_state_t;


typedef struct {
    uint32_t id; /* 实际 CAN-ID，不是带配置标志位的对象字典 COB-ID 参数 */
    uint8_t dlc; /* 有效数据长度，允许 0~8 */
    uint8_t data[CO_CAN_DATA_MAX]; /* 数据缓冲区；只有前 dlc 字节属于有效载荷 */
    uint8_t is_extended; /* 非零表示扩展帧，本项目拒绝 */
    uint8_t is_remote; /* 非零表示远程帧，本项目拒绝 */
    uint8_t is_fd; /* 非零表示 CAN FD 帧，本项目拒绝 */
} can_frame_t;


/* 发送函数指针：user 为私有数据，frame 为只读帧；返回传输状态。
 * 回调必须非阻塞；排队时先复制帧。成功只表示接受提交，不代表总线 ACK。 */
typedef co_status_t (*co_tx_fn)(void *user, const can_frame_t *frame);

typedef struct {
    uint8_t node_id; /* 本节点编号，当前项目使用 1 */
    co_nmt_state_t state; /* 节点当前 NMT 状态，阶段 1 只设置初值 */
    co_tx_fn tx; /* 保存发送函数地址，使协议层不依赖具体硬件 */
    void *tx_user; /* 保存传输层私有数据地址，发送时原样传给回调 */
} co_context_t;

typedef enum {
    CO_RX_NONE = 0, /* 未匹配任何接收服务 */
    CO_RX_NMT, /* 本节点或广播 NMT */
    CO_RX_SDO, /* 本节点 SDO 请求 */
    CO_RX_RPDO1 /* 本节点 RPDO1 */
} co_rx_kind_t;

#endif
