/* 阶段 1 纯 C 基础层：中文学习注释；文件编码 GB2312（代码页 936）。 */
#include "co_core.h"
#include <stddef.h> /* 提供空指针常量 NULL */

/**
 * 用途：检查已初始化的节点上下文（仅本文件内部使用）。
 * 参数：ctx 为节点上下文，只读取，不修改。
 * 返回：CO_OK 表示有效；空指针或缺少回调返回参数错误，节点号越界返回节点错误。
 */
static co_status_t co_context_validate(const co_context_t *ctx)
{
    if (ctx == NULL || ctx->tx == NULL)
    { /* 短路求值：ctx 为空时不会继续读取 ctx->tx */
        return CO_ERR_ARGUMENT;
    }
    if (ctx->node_id == 0u || ctx->node_id > CO_NODE_ID_MAX)
    { /* 拒绝广播编号和超出范围的本机编号 */
        return CO_ERR_NODE_ID;
    }
    return CO_OK; /* 本次操作成功 */
}

/**
 * 用途：初始化节点上下文，建立节点编号与传输回调的关联。
 * 参数：ctx 为输出上下文；node_id 为 1~127；tx 为非阻塞发送函数；tx_user 为回调私有数据，可为空。
 * 返回：成功为 CO_OK；参数或节点号非法返回对应错误，且不修改原上下文。
 * 说明：初始状态为 Initialization，本函数不会发送 Boot-up；调用者负责串行访问。
 */
co_status_t co_init(co_context_t *ctx, uint8_t node_id,
                    co_tx_fn tx, void *tx_user)
{
    if (ctx == NULL || tx == NULL)
    { /* 先检查指针，避免写入无效内存或保存空回调 */
        return CO_ERR_ARGUMENT;
    }
    if (node_id == 0u || node_id > CO_NODE_ID_MAX)
    { /* 所有检查通过后才写上下文，保证失败不改配置 */
        return CO_ERR_NODE_ID;
    }
    ctx->node_id = node_id;             /* 记录本节点编号 */
    ctx->state = CO_NMT_INITIALIZATION; /* 初始状态，不在这里进入运行状态 */
    ctx->tx = tx;                       /* 绑定外部提供的发送函数 */
    ctx->tx_user = tx_user;             /* 绑定传输私有数据，可为空 */
    return CO_OK;                       /* 本次操作成功 */
}

/**
 * 用途：校验经典 CAN 标准数据帧的基本格式。
 * 参数：frame 为待校验帧，只读取，不修改。
 * 返回：CO_OK 或参数、帧类型、CAN-ID、DLC 对应错误码。
 * 说明：DLC=0 是合法空数据帧，NULL 是无效指针；这里不检查具体服务的数据长度。
 */
co_status_t co_frame_validate(const can_frame_t *frame)
{
    if (frame == NULL)
    { /* 先检查指针，再访问帧成员 */
        return CO_ERR_ARGUMENT;
    }
    if (frame->is_extended || frame->is_remote || frame->is_fd)
    { /* 只接受标准经典 CAN 数据帧 */
        return CO_ERR_FRAME_TYPE;
    }
    if (frame->id > CO_CAN_ID_MAX)
    { /* 禁止截断非法 ID 后当作合法帧处理 */
        return CO_ERR_CAN_ID;
    }
    if (frame->dlc > CO_CAN_DATA_MAX)
    { /* 避免超过 8 字节缓冲区；DLC=0 仍合法 */
        return CO_ERR_DLC;
    }
    return CO_OK; /* 本次操作成功 */
}

/**
 * 用途：校验节点和帧后，通过回调把帧交给传输层。
 * 参数：ctx 为已初始化节点；frame 为待发送帧。
 * 返回：校验错误或发送回调的原始返回值，忙和失败不会在这里重试。
 * 说明：回调必须非阻塞；异步排队时须复制帧。CO_OK 不代表总线已经收到 ACK。
 */
co_status_t co_send(const co_context_t *ctx, const can_frame_t *frame)
{
    co_status_t status = co_context_validate(ctx); /* 先检查节点上下文和发送回调 */
    if (status != CO_OK)
    {                  /* 失败立即返回，不执行后续操作 */
        return status; /* 保留具体错误原因 */
    }
    status = co_frame_validate(frame); /* 确认帧的基本格式有效 */
    if (status != CO_OK)
    {                  /* 失败立即返回，不执行后续操作 */
        return status; /* 保留具体错误原因 */
    }
    return ctx->tx(ctx->tx_user, frame); /* 调用传输回调一次，并原样返回传输结果 */
}

/**
 * 用途：根据 CAN-ID 和 NMT 目标节点，将接收帧分类。
 * 参数：ctx 为已初始化节点；frame 为接收帧；kind 为分类输出，不能为 NULL。
 * 返回：本节点或广播 NMT 为 CO_OK，无关帧为 CO_IGNORED，非法参数/格式返回错误。
 * 说明：失败或忽略时 kind 清为 CO_RX_NONE；这里只分类，不执行命令、不改状态或输出。
 * 说明：NMT 在读取目标字节前检查 DLC=2；SDO/PDO 的服务长度和状态限制留给后续模块。
 */
co_status_t co_classify_rx(const co_context_t *ctx, const can_frame_t *frame,
                           co_rx_kind_t *kind)
{
    co_status_t status;
    if (kind == NULL)
    { /* 分类结果必须有可写的输出地址 */
        return CO_ERR_ARGUMENT;
    }
    *kind = CO_RX_NONE;                /* 先清除结果，防止失败时残留上一次分类 */
    status = co_context_validate(ctx); /* 确认上下文有效 */
    if (status != CO_OK)
    {                  /* 失败立即返回，不执行后续操作 */
        return status; /* 保留具体错误原因 */
    }
    status = co_frame_validate(frame); /* 确认帧的基本格式有效 */
    if (status != CO_OK)
    {                  /* 失败立即返回，不执行后续操作 */
        return status; /* 保留具体错误原因 */
    }
    if (frame->id == CO_COB_NMT)
    { /* NMT 的节点号不在 CAN-ID 中，而在 data[1] */
        if (frame->dlc != 2u)
        { /* 必须先满足 2 字节长度，才读取 NMT 目标 */
            return CO_ERR_DLC;
        }
        if (frame->data[1] != 0u && frame->data[1] != ctx->node_id)
        {                      /* 既不是广播，也不是发给本节点 */
            return CO_IGNORED; /* 不处理无关帧 */
        }
        *kind = CO_RX_NMT; /* 记录 NMT 类别，但不执行 data[0] 中的命令 */
    }
    else if (frame->id == CO_COB_SDO_RX_BASE + ctx->node_id)
    {                      /* 匹配本节点 SDO 请求 CAN-ID */
        *kind = CO_RX_SDO; /* 只分类，后续 SDO 模块负责解析和响应 */
    }
    else if (frame->id == CO_COB_RPDO1_BASE + ctx->node_id)
    {                        /* 匹配本节点输出命令 CAN-ID */
        *kind = CO_RX_RPDO1; /* 只分类，后续 PDO 模块负责状态和数据检查 */
    }
    else
    {
        return CO_IGNORED; /* 不处理无关帧 */
    }
    return CO_OK; /* 本次操作成功 */
}
