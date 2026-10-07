# `co_emcy.c` 总图



### 一、先记住 3 个核心“容器”



##### 1. `frame`

```
can_frame_t frame;
```

它是：

> **这一次临时准备要发送的 CAN 帧。**

特点：

```
临时的
↓
make_frame() 生成
↓
submit() 尝试发送
```

它不是“历史记录”。

------



##### 2. `active_error_*`

例如：

```c
active_error_code
active_error_register
active_manufacturer
```

它们表示：

> **已经成功发送出去，并且当前仍然有效的故障。**

所以：

```
active = 1
```

就意味着：

> 当前有一个已经成功报告的活动故障。

------

最主要的用途是**防止重复上报**：之后系统每次检查到同一个 CAN 接收溢出，如果不保存这些内容，程序就不知道“之前是不是已经报告过相同故障”

它们不是为了再次发送原帧，而是为了**记住当前故障、避免重复上报、识别故障变化（第一次故障是什么，第二次又是什么），并在清除前保留故障上下文**。



##### 3. `pending_frame`

它表示：

> **之前发送失败，现在等待重新发送的完整 CAN 帧。**

同时：

```
pending = 1
```

表示：

> 现在确实有一帧正在等待重试。

而：

```
pending_is_reset
```

用来区分：

```
0 → 故障报告帧
1 → 故障清除帧
```





### 2、9 个函数其实可以分成 4 类

| 类别     | 函数                | 职责                                                         |
| -------- | ------------------- | ------------------------------------------------------------ |
| 基础工具 | `validate()`        | 检查 EMCY 模块是否有效                                       |
| 基础工具 | `frame_equal()`     | 比较两张 CAN 帧                                              |
| 基础工具 | `make_frame()`      | 组装 EMCY CAN 帧                                             |
| 状态管理 | `remember_active()` | 发送成功后更新 active_error_code，active_error_register，active_manufacturer |
| 发送核心 | `submit()`          | 真正尝试发送，成功/失败分别处理                              |
| 对外入口 | `co_emcy_report()`  | 报告故障                                                     |
| 对外入口 | `co_emcy_clear()`   | 清除故障                                                     |
| 对外入口 | `co_emcy_process()` | 重试 pending 帧                                              |
| 初始化   | `co_emcy_init()`    | 建立 EMCY 初始状态                                           |

其中真正的**核心枢纽**其实是：

```c
co_emcy_report()
co_emcy_clear()
co_emcy_process()
        ↓
     submit()
        ↓
remember_active()
```



### 3、初始化

```c
co_emcy_init()
      ↓
建立 EMCY 模块
      ↓
保存 node / device / 发送接口等
      ↓
active = 0
pending = 0
```

初始状态：

```
active  = 0
pending = 0
```

意思：

> 没有活动故障上报成功active=1，也没有失败待重试的帧pending=1。



### 4、发生故障：`co_emcy_report()`

假设：

```c
CAN RX Overflow
error_code = 0xFF01
```

流程：

```c
co_emcy_report()
        ↓
① validate()
        ↓
② error_register | 0x01
        ↓
③ 检查 bit5 / bit6 保留位
        ↓
④ make_frame()
        ↓
⑤ 检查 pending
        ↓
⑥ 检查 active 是否重复
        ↓
⑦ 更新 0x1001
        ↓
⑧ submit(..., 0)
```

这里：

```
is_reset = 0
```

表示：

> **这是故障报告帧。**



### 5、报告成功

如果：

```
submit()
    ↓
发送成功
```

最终：

```
remember_active()
```

记录：

```
active = 1
```

并保存：

```
active_error_code
active_error_register
active_manufacturer
```

最终：

```
active  = 1
pending = 0
```

含义：

> **故障已经成功通知出去，存个备份到active_error_code*，*active_error_register，active_manufacturer ，用于清除帧的判断。



### 6、报告失败

如果：

```
submit()
    ↓
发送失败
```

就不会把它记成 active。

而是：

```
pending_frame = 当前故障帧
pending = 1
pending_is_reset = 0
```

所以：

```
active  = 0
pending = 1
pending_is_reset = 0
```

含义：

> **故障还没成功报告出去，现在这张故障 EMCY 等待重试。**



### 7、这里最容易混淆的一点：`active` 和 `pending`

直接记：

```c
active
↓
已经成功发出去

pending
↓
还没成功发出去
```

所以：

```
active=1
pending=0
```

是：

> 已经报告成功。

而：

```c
active=0
pending=1
pending_is_reset=0
```

是：

> 故障报告失败，正在等待重试。



### 8、`pending` 的冲突处理

如果：

```
pending = 1
```

说明当前已经有一个位置被占用了。



新来的故障和 pending 完全一样

```
pending_frame = A
新 frame       = A
```

返回：

```
CO_IGNORED
```

因为：

> A 已经在等重试，不需要再放一份 A。

------





新来的故障和 pending 不一样

```
pending_frame = A
新 frame       = B
```

返回：

```
CO_ERR_TX_BUSY
```

因为：

> 这里只有一个 pending 槽，不能让 B 把 A 覆盖掉。

至于 B 怎么办：

> **由上层决定是否记录、延迟、重试或者进入自己的故障队列。*，目前项目没做处理！





### 9、系统恢复：`co_emcy_clear()`

假设：

```
active = 1
pending = 0
```

说明：

> 之前的故障已经成功报告。

现在系统恢复，于是：

```
co_emcy_clear()
```

流程：

```c
co_emcy_clear()
       ↓
① validate()
       ↓
② 检查 pending
       ↓
③ 检查 active
       ↓
④ make_frame(0, 0, NULL)
       ↓
⑤ 生成全零清除帧
       ↓
⑥ 0x1001 = 0
       ↓
⑦ submit(..., is_reset)	//这里：is_reset = 1，表示这是清除帧
```





### 10、清除成功

如果：

```
submit()
    ↓
发送成功
```

那么：

```
active = 0
pending = 0
```

同时：

```
active_error_*
```

清掉。

于是：

```
正常状态：
active  = 0
pending = 0
```

------

### 11、清除失败

如果清除帧发送失败：

```
active = 1
pending = 1
pending_is_reset = 1
```

注意这里非常重要：

```
pending_is_reset = 1
```

说明：

> **pending 里面不是故障报告，而是故障清除帧。**

所以此时：

```
pending_frame
    ↓
00 00 00 00 00 00 00 00
```

等待以后重试。

### 12、`co_emcy_process()` 就是“收拾残局”

它干的事情非常简单：

```c
co_emcy_process()
       ↓
有没有 pending？
   ↙       ↘
没有        有
 ↓          ↓
忽略       submit(pending_frame,
                   pending_is_reset)
```

所以它**自己不重新组帧**。

它直接拿：

```
pending_frame
```

重新发送。





### 13、重试成功后的两种结果



① 原来是故障报告

```
pending_is_reset = 0
```

重试成功：

```
pending → 0
active  → 1
```

并记录：

```
active_error_*
```

------



② 原来是清除帧

```
pending_is_reset = 1
```

重试成功：

```
pending → 0
active  → 0
```

并清除：

```
active_error_*
```



### 14、最终状态机

你以后复习其实就看这张：

```c
                    正常
             active=0,pending=0
                    │
                    │ 发生故障
                    ▼
             co_emcy_report()
                    │
              submit(is_reset=0)
                ↙           ↘
             成功             失败
              │                │
              ▼                ▼
      active=1,pending=0   active=0,pending=1
                           reset=0
              │                │
              │                │ co_emcy_process()
              │                ▼
              │             重试成功
              │                │
              │                ▼
              └──────────→ active=1
                    │
                    │ 故障恢复
                    ▼
             co_emcy_clear()
                    │
              submit(is_reset=1)
                ↙           ↘
             成功             失败
              │                │
              ▼                ▼
      active=0,pending=0   active=1,pending=1
                           reset=1
                                │
                                │ co_emcy_process()
                                ▼
                              成功
                                │
                                ▼
                         active=0,pending=0
```



### 15、最后压缩成一句“程序员版”

你以后看到整个 `co_emcy.c`，脑子里只需要出现：

```c
co_emcy_report()
  ↓
制作故障帧
  ↓
submit
  ├─ 成功 → active
  └─ 失败 → pending(reset=0)

 co_emcy_clear()
  ↓
制作全零帧
  ↓
submit
  ├─ 成功 → 清除 active
  └─ 失败 → pending(reset=1)

co_emcy_process()
  ↓
重发 pending_frame
```

再加上：

```
active
= 已成功报告的当前故障

pending
= 发送失败、正在等待重试的帧

pending_is_reset
= pending 里面到底是“故障报告”还是“清除帧”
```