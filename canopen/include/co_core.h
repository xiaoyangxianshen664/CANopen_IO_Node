/* 阶段 1 纯 C 基础层：中文学习注释；文件编码 GB2312（代码页 936）。 */
#ifndef CO_CORE_H
#define CO_CORE_H

#include "co_types.h"


/**
 * 用途：初始化节点上下文，建立节点编号与传输回调的关联。
 * 参数：ctx 为输出上下文；node_id 为 1~127；tx 为非阻塞发送函数；tx_user 为回调私有数据，可为空。
 * 返回：成功为 CO_OK；参数或节点号非法返回对应错误，且不修改原上下文。
 * 说明：初始状态为 Initialization，本函数不会发送 Boot-up；调用者负责串行访问。
 */
co_status_t co_init(co_context_t *ctx, uint8_t node_id,
                    co_tx_fn tx, void *tx_user);
/**
 * 用途：校验经典 CAN 标准数据帧的基本格式。
 * 参数：frame 为待校验帧，只读取，不修改。
 * 返回：CO_OK 或参数、帧类型、CAN-ID、DLC 对应错误码。
 * 说明：DLC=0 是合法空数据帧，NULL 是无效指针；这里不检查具体服务的数据长度。
 */
co_status_t co_frame_validate(const can_frame_t *frame);
/**
 * 用途：校验节点和帧后，通过回调把帧交给传输层。
 * 参数：ctx 为已初始化节点；frame 为待发送帧。
 * 返回：校验错误或发送回调的原始返回值，忙和失败不会在这里重试。
 * 说明：回调必须非阻塞；异步排队时须复制帧。CO_OK 不代表总线已经收到 ACK。
 */
co_status_t co_send(const co_context_t *ctx, const can_frame_t *frame);


/**
 * 用途：根据 CAN-ID 和 NMT 目标节点，将接收帧分类。
 * 参数：ctx 为已初始化节点；frame 为接收帧；kind 为分类输出，不能为 NULL。
 * 返回：本节点或广播 NMT 为 CO_OK，无关帧为 CO_IGNORED，非法参数/格式返回错误。
 * 说明：失败或忽略时 kind 清为 CO_RX_NONE；这里只分类，不执行命令、不改状态或输出。
 * 说明：NMT 在读取目标字节前检查 DLC=2；SDO/PDO 的服务长度和状态限制留给后续模块。
 */
co_status_t co_classify_rx(const co_context_t *ctx, const can_frame_t *frame,
                           co_rx_kind_t *kind);

#endif
