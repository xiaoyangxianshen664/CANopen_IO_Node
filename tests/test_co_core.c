/* 阶段 1 纯 C 基础层：中文学习注释；文件编码 GB2312（代码页 936）。 */
/* CHECK 为真则继续，为假则打印文件、行号和表达式并返回 1；Release 中也有效。 */
#include "co_core.h"
#include <stdio.h>  /* 提供 fprintf 和标准错误输出 stderr */
#include <string.h> /* 提供字节比较 memcmp 和字符串比较 strcmp */

#define CHECK(expr)                                                    \
    do                                                                 \
    {                                                                  \
        if (!(expr))                                                   \
        {                                                              \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
            return 1;                                                  \
        }                                                              \
    } while (0)

typedef struct
{
    unsigned calls;     /* 记录发送回调被调用的次数 */
    can_frame_t frame;  /* 保存最近一次提交报文的副本 */
    co_status_t result; /* 预设发送回调的返回状态 */
} fake_bus_t;

/**
 * 用途：模拟传输层发送回调，记录调用次数并复制报文。
 * 参数：user 必须指向有效 fake_bus_t；frame 为待发送帧。
 * 返回：测试预设的 result，用来模拟成功、忙或失败；不会访问真实 CAN 硬件。
 */
static co_status_t fake_tx(void *user, const can_frame_t *frame)
{
    fake_bus_t *bus = user; /* 把通用私有数据指针还原为模拟总线指针 */
    ++bus->calls;           /* 每次进入回调都计数，用来检测意外发送或重试 */
    bus->frame = *frame;    /* 结构体整体复制，避免保存短生命周期的帧指针 */
    return bus->result;     /* 模拟传输层的成功、忙或失败 */
}

/**
 * 用途：验证帧边界、空指针和不支持的帧类型。
 * 参数：无。
 * 返回：0 表示本组全部通过，1 表示 CHECK 发现失败。
 */
static int test_frame(void)
{
    can_frame_t f = {0};                               /* 全部成员清零，构造标准帧、ID=0、DLC=0 */
    CHECK(co_frame_validate(NULL) == CO_ERR_ARGUMENT); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_frame_validate(&f) == CO_OK);             /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    f.id = 0x7FFu;
    f.dlc = 8;
    CHECK(co_frame_validate(&f) == CO_OK); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    f.id = 0x800u;
    CHECK(co_frame_validate(&f) == CO_ERR_CAN_ID); /* 故意传入异常条件，确认返回预期错误码 */
    f.id = UINT32_MAX;
    CHECK(co_frame_validate(&f) == CO_ERR_CAN_ID); /* 故意传入异常条件，确认返回预期错误码 */
    f.id = 1;
    f.dlc = 9;
    CHECK(co_frame_validate(&f) == CO_ERR_DLC); /* 故意传入异常条件，确认返回预期错误码 */
    f.dlc = 255;
    CHECK(co_frame_validate(&f) == CO_ERR_DLC); /* 故意传入异常条件，确认返回预期错误码 */
    f.dlc = 0;
    f.is_extended = 1;
    CHECK(co_frame_validate(&f) == CO_ERR_FRAME_TYPE); /* 故意传入异常条件，确认返回预期错误码 */
    f.is_extended = 0;
    f.is_remote = 1;
    CHECK(co_frame_validate(&f) == CO_ERR_FRAME_TYPE); /* 故意传入异常条件，确认返回预期错误码 */
    f.is_remote = 0;
    f.is_fd = 1;
    CHECK(co_frame_validate(&f) == CO_ERR_FRAME_TYPE); /* 故意传入异常条件，确认返回预期错误码 */
    return 0;                                          /* 本组检查全部通过，向 CTest 返回成功 */
}

/**
 * 用途：验证初始化参数、节点号范围和失败时保留原配置。
 * 参数：无。
 * 返回：0 表示本组全部通过，1 表示失败。
 */
static int test_init(void)
{
    co_context_t ctx = {0};                                        /* 建立清零的上下文，使用前仍需调用 co_init */
    fake_bus_t bus = {0};                                          /* 调用次数为 0，默认返回 CO_OK */
    CHECK(co_init(NULL, 1, fake_tx, &bus) == CO_ERR_ARGUMENT);     /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_init(&ctx, 1, NULL, &bus) == CO_ERR_ARGUMENT);        /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_init(&ctx, 1, fake_tx, &bus) == CO_OK);               /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(ctx.node_id == 1 && ctx.state == CO_NMT_INITIALIZATION); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(ctx.tx == fake_tx && ctx.tx_user == &bus);               /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(co_init(&ctx, 0, fake_tx, NULL) == CO_ERR_NODE_ID);      /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_init(&ctx, 128, fake_tx, NULL) == CO_ERR_NODE_ID);    /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_init(&ctx, 255, fake_tx, NULL) == CO_ERR_NODE_ID);    /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(ctx.node_id == 1 && ctx.tx_user == &bus);                /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(co_init(&ctx, 127, fake_tx, NULL) == CO_OK);             /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(ctx.node_id == 127 && ctx.tx_user == NULL);              /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    return 0;                                                      /* 本组检查全部通过，向 CTest 返回成功 */
}

/**
 * 用途：验证发送数据、回调调用次数、错误传递以及非法帧拦截。
 * 参数：无。
 * 返回：0 表示本组全部通过，1 表示失败。
 */
static int test_tx(void)
{
    fake_bus_t bus = {0};   /* 调用次数为 0，默认返回 CO_OK */
    co_context_t ctx = {0}; /* 建立清零的上下文，使用前仍需调用 co_init */
    can_frame_t f = {0x7FFu, 8, {0, 1, 2, 3, 4, 5, 6, 255}, 0, 0, 0};
    CHECK(co_send(&ctx, &f) == CO_ERR_ARGUMENT);                         /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_init(&ctx, 1, fake_tx, &bus) == CO_OK);                     /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(co_send(NULL, &f) == CO_ERR_ARGUMENT);                         /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_send(&ctx, NULL) == CO_ERR_ARGUMENT);                       /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(bus.calls == 0);                                               /* 检查回调次数，确保非法帧不发送、失败不自动重试 */
    CHECK(co_send(&ctx, &f) == CO_OK);                                   /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(bus.calls == 1 && bus.frame.id == f.id && bus.frame.dlc == 8); /* 检查回调次数，确保非法帧不发送、失败不自动重试 */
    CHECK(memcmp(bus.frame.data, f.data, 8) == 0);                       /* 逐字节比较 8 字节载荷，确认发送内容未改变 */
    bus.result = CO_ERR_TX_BUSY;
    CHECK(co_send(&ctx, &f) == CO_ERR_TX_BUSY); /* 故意传入异常条件，确认返回预期错误码 */
    bus.result = CO_ERR_TX_FAILED;
    CHECK(co_send(&ctx, &f) == CO_ERR_TX_FAILED); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(bus.calls == 3);                        /* 检查回调次数，确保非法帧不发送、失败不自动重试 */
    f.dlc = 9;
    CHECK(co_send(&ctx, &f) == CO_ERR_DLC); /* 故意传入异常条件，确认返回预期错误码 */
    f.dlc = 8;
    f.id = 0x800u;
    CHECK(co_send(&ctx, &f) == CO_ERR_CAN_ID); /* 故意传入异常条件，确认返回预期错误码 */
    f.id = 1;
    f.is_remote = 1;
    CHECK(co_send(&ctx, &f) == CO_ERR_FRAME_TYPE); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(bus.calls == 3);                         /* 检查回调次数，确保非法帧不发送、失败不自动重试 */
    f.is_remote = 0;
    f.dlc = 0;
    bus.result = CO_OK;
    CHECK(co_send(&ctx, &f) == CO_OK);           /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(bus.calls == 4 && bus.frame.dlc == 0); /* 检查回调次数，确保非法帧不发送、失败不自动重试 */
    return 0;                                    /* 本组检查全部通过，向 CTest 返回成功 */
}

/**
 * 用途：遍历全部合法节点与标准 CAN-ID，验证接收分类及 NMT 广播过滤。
 * 参数：无。
 * 返回：0 表示本组全部通过，1 表示失败。
 * 说明：这里只检查分类；即使识别出 NMT，也不应执行状态切换。
 */
static int test_routing(void)
{
    co_context_t ctx = {0};        /* 建立清零的上下文，使用前仍需调用 co_init */
    fake_bus_t bus = {0};          /* 调用次数为 0，默认返回 CO_OK */
    can_frame_t f = {0};           /* 全部成员清零，构造标准帧、ID=0、DLC=0 */
    co_rx_kind_t kind = CO_RX_SDO; /* 故意设非空初值，检查出错时是否清除结果 */
    unsigned node, id;
    CHECK(co_init(&ctx, 1, fake_tx, &bus) == CO_OK);             /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
    CHECK(co_classify_rx(NULL, &f, &kind) == CO_ERR_ARGUMENT);   /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(kind == CO_RX_NONE);                                   /* 检查分类输出，避免错误或忽略时残留旧结果 */
    CHECK(co_classify_rx(&ctx, NULL, &kind) == CO_ERR_ARGUMENT); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(co_classify_rx(&ctx, &f, NULL) == CO_ERR_ARGUMENT);    /* 故意传入异常条件，确认返回预期错误码 */
    for (node = 1; node <= 127; ++node)
    {                                                                /* 遍历全部合法本机节点编号 */
        CHECK(co_init(&ctx, (uint8_t)node, fake_tx, &bus) == CO_OK); /* 检查实际结果是否符合预期；不满足则立即报告本组失败 */
        f.id = 0;
        f.dlc = 2;
        f.data[0] = 1;
        for (id = 0; id <= 255; ++id)
        {                                                                        /* 遍历 NMT 目标字节全部取值，包括广播和非法目标 */
            co_status_t expected = (id == 0 || id == node) ? CO_OK : CO_IGNORED; /* 只有广播或本机编号应被接受 */
            f.data[1] = (uint8_t)id;
            CHECK(co_classify_rx(&ctx, &f, &kind) == expected);          /* 检查分类输出，避免错误或忽略时残留旧结果 */
            CHECK(kind == (expected == CO_OK ? CO_RX_NMT : CO_RX_NONE)); /* 检查分类输出，避免错误或忽略时残留旧结果 */
        }
        for (id = 0; id <= 8; ++id)
        { /* 遍历经典 CAN 长度，NMT 只有 DLC=2 合法 */
            f.dlc = (uint8_t)id;
            f.data[1] = 0;
            CHECK(co_classify_rx(&ctx, &f, &kind) == (id == 2 ? CO_OK : CO_ERR_DLC)); /* 故意传入异常条件，确认返回预期错误码 */
        }
        f.dlc = 8;
        for (id = 1; id <= 0x7FF; ++id)
        {                                       /* 遍历全部非零标准 ID，检查不应误收其它服务 */
            co_rx_kind_t expected = CO_RX_NONE; /* 默认忽略，仅两个本机接收 ID 可改变预期 */
            if (id == 0x600u + node)
                expected = CO_RX_SDO; /* SDO 请求使用 0x600 加节点号 */
            if (id == 0x200u + node)
                expected = CO_RX_RPDO1; /* RPDO1 使用 0x200 加节点号 */
            f.id = id;
            CHECK(co_classify_rx(&ctx, &f, &kind) ==
                  (expected == CO_RX_NONE ? CO_IGNORED : CO_OK));
            CHECK(kind == expected); /* 检查分类输出，避免错误或忽略时残留旧结果 */
        }
    }
    f.id = 0x800u;
    CHECK(co_classify_rx(&ctx, &f, &kind) == CO_ERR_CAN_ID); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(kind == CO_RX_NONE);                               /* 检查分类输出，避免错误或忽略时残留旧结果 */
    f.id = 0x67Fu;
    f.dlc = 9;
    CHECK(co_classify_rx(&ctx, &f, &kind) == CO_ERR_DLC); /* 故意传入异常条件，确认返回预期错误码 */
    f.dlc = 8;
    f.is_extended = 1;
    CHECK(co_classify_rx(&ctx, &f, &kind) == CO_ERR_FRAME_TYPE); /* 故意传入异常条件，确认返回预期错误码 */
    CHECK(ctx.state == CO_NMT_INITIALIZATION && bus.calls == 0); /* 检查回调次数，确保非法帧不发送、失败不自动重试 */
    return 0;                                                    /* 本组检查全部通过，向 CTest 返回成功 */
}

/**
 * 用途：根据命令行参数选择一个测试组，供 CTest 分别运行。
 * 参数：argc 为参数数量（含程序名）；argv[1] 为 frame/init/tx/routing。
 * 返回：所选测试结果；参数缺失或测试名未知时返回 1。
 */
int main(int argc, char **argv)
{
    if (argc != 2)
        return 1; /* 需要程序名和一个测试组名 */
    if (strcmp(argv[1], "frame") == 0)
        return test_frame(); /* 运行帧格式测试 */
    if (strcmp(argv[1], "init") == 0)
        return test_init(); /* 运行初始化测试 */
    if (strcmp(argv[1], "tx") == 0)
        return test_tx(); /* 运行发送回调测试 */
    if (strcmp(argv[1], "routing") == 0)
        return test_routing(); /* 运行节点分类测试 */
    return 1;                  /* 未知测试组名，返回失败 */
}
