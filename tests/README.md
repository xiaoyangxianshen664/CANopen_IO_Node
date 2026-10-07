# 测试

放置协议栈、对象字典和节点应用的测试代码及测试记录。

阶段 1：`test_co_core.c` 注册 frame、init、tx、routing 四组 CTest。CHECK 宏在 Release 下仍会执行；模拟发送器复制帧并返回预设状态，用于检查实际调用次数、报文内容和失败传递。执行方式见根 README。
