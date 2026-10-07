# 1：validate（）函数

```c
static co_status_t validate(const co_emcy_t *emcy)
{
    /*1：例行检查*/
    if (emcy == NULL || emcy->node == NULL || emcy->node->tx == NULL ||		//emcy 本身不是空指针 ，emcy->node 已指向 CANopen 节点
        emcy->device == NULL ||											//节点的发送接口 tx 已设置，否则无法发送 EMCY。
        emcy->device->table.entries != emcy->device->entries ||			   //emcy->device 已指向设备对象字典
        emcy->device->table.count != CO_DEVICE_OD_COUNT)				  //对象字典表的 entries 确实指向设备自己的 entries 数组。
        return CO_ERR_ARGUMENT;											//对象条目数量符合预期 CO_DEVICE_OD_COUNT

    if (emcy->node->node_id == 0u || emcy->node->node_id > CO_NODE_ID_MAX)
        													//CANopen 节点 ID 的有效范围是 1 到 CO_NODE_ID_MAX（本项目最大值为 127）
        return CO_ERR_NODE_ID;							   //0 是 NMT 广播地址，不能作为节点自己的 ID，所以这里单独返回 CO_ERR_NODE_ID。
	
    return CO_OK;
}
```

`validate()`。它是 EMCY 模块内部的**参数和状态检查函数**，在发送或清除 EMCY 前确认所需对象都有效。

可以把整个函数记成：

> 先检查 EMCY 依赖的指针和对象字典结构；再检查 Node-ID 范围；都通过才允许后续 EMCY 操作。







# 2：frame_equal（）函数

```c
static uint8_t frame_equal(const can_frame_t *left, const can_frame_t *right)
{
    return (uint8_t)(left->id == right->id && left->dlc == right->dlc &&	  //检查 CAN-ID，数据长度
                     left->is_extended == right->is_extended &&				 //是否扩展帧、是否远程帧
                     left->is_remote == right->is_remote &&					//是否 CAN FD 帧
                     left->is_fd == right->is_fd &&							//只要有一项不同，&& 后续比较就会短路，整个结果为假。
                     memcmp(left->data, right->data, sizeof(left->data)) == 0);
    															//比较两个内存区域的字节内容，sizeof(left->data) 是整个数据数组的大小
}
```

这个 `frame_equal()` 用来判断两帧 CAN 报文是否相同。它返回 `1` 表示相同，返回 `0` 表示不同。

```c
static：只供当前 co_emcy.c 文件内部调用。
left、right：分别指向要比较的两帧。
返回 uint8_t：这里把比较结果当作真假值使用。 //返回 `1` 表示相同 ，返回 `0` 表示不同
```

在本项目中，EMCY 帧固定 DLC 为 8 字节，所以这里会检查完整的 8 字节数据区。这个函数后面用于判断：新报告的故障帧是否和已经等待重试的 `pending_frame` 完全相同，避免把同一帧重复放进待重试状态。



# 3：make_frame（）函数

```c
static can_frame_t make_frame(const co_emcy_t *emcy, uint16_t error_code,
                              uint8_t error_register,
                              const uint8_t manufacturer[CO_EMCY_MANUFACTURER_LENGTH])
{
    can_frame_t frame = {0};		//先把要返回的报文帧清0
    uint8_t i;
	/*1：设置 EMCY 的 CAN-ID 和数据长度：*/
    frame.id = CO_EMCY_COB_BASE + emcy->node->node_id;
    frame.dlc = CO_EMCY_DATA_LENGTH;
    /*2：填入数据区前三个字节：*/
    frame.data[0] = (uint8_t)(error_code & 0xFFu);//错误码占两个字节，按低字节在前放入报文
    frame.data[1] = (uint8_t)(error_code >> 8);
    frame.data[2] = error_register;//data[2] 放 Error Register

    if (manufacturer != NULL) //如果提供了厂商信息，也就是第四个参数传入了manufacturer数组的首地址而非NULLC，就复制 5 个字节：
    {
        for (i = 0u; i < CO_EMCY_MANUFACTURER_LENGTH; ++i)
            frame.data[3u + i] = manufacturer[i];
    }

    return frame;
}
```

`make_frame()` 的作用是：**把 EMCY 的错误信息组装成一帧 CAN 报文并返回**。它只负责组帧，不负责发送。

返回类型是 `can_frame_t`，所以组好的帧会作为结构体返回。`static` 表示它只在当前 `.c` 文件内部使用。



`manufacturer != NULL` 判断的是：**`manufacturer` 指针有没有指向一块有效的数据**，不是检查这 5 个字节是不是全为 0。

- 传入 `NULL`：没有提供厂商自定义数据，循环不执行；`frame` 起初整体清零，所以 `data[3..7]` 保持为 `00 00 00 00 00`。
- 传入一个有效数组：无论数组里是非零值还是全零，条件都成立，循环会把 5 个字节复制进去。

例如：

```c
make_frame(..., NULL);                 // 不提供数据，帧中这5字节为0
make_frame(..., (uint8_t[5]){0,0,0,0,0}); // 提供了全零数据，也会执行复制
make_frame(..., manufacturer);         // 提供数组内容并复制
```

所以关键区别是：**`NULL` 表示没有数据地址；全零数组仍然是“提供了数据”，只是数据内容为零。**





# 4：remember_active（）函数

```c
static void remember_active(co_emcy_t *emcy, const can_frame_t *frame,
                            uint8_t is_reset)
{
    uint8_t i;
	/*1：函数一开始先清除待发送状态：*/
    emcy->pending = 0u;				//pending记录是否有待重试的帧：0 表示无待发帧，1 表示 pending_frame 中有待重发报文
    emcy->pending_is_reset = 0u;	//pending_is_reset 标记待重试帧的类型：0 表示故障报告帧，1 表示故障清除帧（在pending=1时有效）
    /*2：如果刚刚发送成功的是清除帧*/
    if (is_reset != 0u)//非零表示清除帧成功发出
    {
        emcy->active = 0u;
        emcy->active_error_code = 0u;
        emcy->active_error_register = 0u; //函数把活动故障标志和保存的故障内容清零，然后 return 结束函数。
        memset(emcy->active_manufacturer, 0, sizeof(emcy->active_manufacturer));//memset() 在这里把厂商信息数组的后5个字节都写成 0。
        return;
    }
	/*3：如果发送的是故障报告帧，没有进入上面的分支，就执行下面的代码*/ 
    emcy->active = 1u;//表示现在有一个已成功报告、尚未清除的活动故障。随后从已发送帧的数据区取出故障内容：
    emcy->active_error_code = (uint16_t)frame->data[0] |
                              ((uint16_t)frame->data[1] << 8);//前两个字节按低字节在前组合成错误码；
    emcy->active_error_register = frame->data[2];//第三个字节保存 Error Register。
    for (i = 0u; i < CO_EMCY_MANUFACTURER_LENGTH; ++i)//循环再把 frame->data[3..7] 复制到 active_manufacturer[0..4]。
        emcy->active_manufacturer[i] = frame->data[3u + i];
}
//所以它做的是：故障帧发送成功，就记录活动故障；清除帧发送成功，就清除活动故障记录。pending_frame 是否还残留旧内容不影响结果，因为此时 pending=0。
```

函数作用：`is_reset`：`0` 表示故障报告帧；非 `0` 表示故障清除帧，根据传入的is_reset，为1则进入if分支，否则执行if外面的代码

​		  为1则表示：清除帧发送成功，就清除活动故障记录，结构体active_error_code，active_error_register，active_manufacturer全变为0

​		 为0则表示：故障帧发送成功，从已发送帧的数据区取出故障内容存入active_error_code，active_error_register，active_manufacturer





`is_reset` 这个**函数参数**告诉函数刚成功发送的是故障报告帧还是清除帧，然后函数据此更新 `emcy` 里的状态。

关键是这两个名字虽然相似，却是两个不同变量：

- `is_reset`：本次调用传进来的参数，说明**刚成功发送的帧是什么类型**。
- `emcy->pending_is_reset`：结构体成员，说明**待重试的帧是什么类型**。只有 `pending=1` 时才有意义。



`pending` 和 `pending_is_reset` 用来描述**发送失败后留下的待重试状态**。帧一旦成功发送，`remember_active()` 就先把这两个标志清零：

```c
emcy->pending = 0u;				//pending记录是否有待重试的帧：0 表示无待发帧，1 表示 pending_frame 中有待重发报文
emcy->pending_is_reset = 0u;	//pending_is_reset 标记待重试帧的类型：0 表示故障报告帧，1 表示故障清除帧
```

之后再根据函数参数 `is_reset` 更新活动故障记录：成功发送的是报告帧，就记录故障；成功发送的是清除帧，就清除故障记录。

补充一点：它们不一定每次成功发送前都曾经置为 1。比如帧第一次发送就成功，两个标志原本就是 0；这里清零是统一地结束待重试状态。



### 示例：

```c
remember_active(&emcy, &frame, is_reset);
```

- `emcy`：要更新的 EMCY 状态结构体。
- `frame`：刚刚成功发送的那一帧。
- `is_reset`：`0` 表示故障报告帧；非 `0` 表示故障清除帧。

先看故障报告帧成功发送的例子。

```c
frame.data = {0x02, 0xFF, 0x11, 1, 2, 3, 4, 5};
is_reset = 0;
```

调用：

```c
remember_active(&emcy, &frame, 0);
```

函数先清除待发送标志：

```c
pending = 0;
pending_is_reset = 0;
```

因为 `is_reset` 是 0，不进入清除分支，于是记录活动故障：

```c
active = 1
active_error_code = 0xFF02    // data[0]=02 是低字节，data[1]=FF 是高字节
active_error_register = 0x11  // 来自 data[2]
active_manufacturer = {1, 2, 3, 4, 5} // 来自 data[3..7]
```

再看清除帧成功发送的例子：

```c
frame.data = {0, 0, 0, 0, 0, 0, 0, 0};
is_reset = 1;
```

调用：

```c
remember_active(&emcy, &frame, 1);
```

函数仍先清除 `pending` 标志，然后发现 `is_reset != 0`，将活动故障标志和故障内容都清零：

```c
active = 0
active_error_code = 0
active_error_register = 0
active_manufacturer = {0, 0, 0, 0, 0}
pending = 0
pending_is_reset = 0
```

真实代码里，这个函数由发送流程在 `co_send()` 返回 `CO_OK` 后调用。因此，它记录的是**发送成功的帧**，不会因为一帧发送失败就把故障标成已成功上报。





# 5：submit（）函数

```c
static co_status_t submit(co_emcy_t *emcy, const can_frame_t *frame,
                          uint8_t is_reset)
{
    co_status_t status = co_send(emcy->node, frame);//这里调用 co_send() 发送 frame，并把返回状态保存到 status。

    if (status == CO_OK)					 //发送成功后调用刚学过的 remember_active()：
    {
        remember_active(emcy, frame, is_reset);//is_reset == 0：这是故障报告帧，记录活动故障内容。
    }									    //is_reset != 0：这是故障清除帧，清除活动故障记录。
    else							  		//发送失败
    {
        emcy->pending_frame = *frame;		  //将待重试报文也仍然保存在 emcy 里。
        emcy->pending = 1u;					 //表示有帧需要重试。	
        emcy->pending_is_reset = is_reset;    //记下待重试的是故障报告帧还是清除帧。
    }
    return status;//把 co_send() 的结果返回给调用者，让上层知道这次发送成功还是失败。当前实现把所有非 CO_OK 的结果都暂存为待重试帧；稍后 																								  co_emcy_process() 会尝试重发。
}
```

`submit()` 负责把一帧 EMCY 报文交给 CANopen 发送接口，并根据发送结果更新状态：**发送成功就记录结果；发送失败就保存整帧等待重试。**



# 6：co_emcy_init（）函数

```c
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
```

`co_emcy_init()` 用来初始化 EMCY 模块：先检查传入的节点和对象字典是否有效，再把初始化好的状态保存到 `*emcy`。





# 7：co_emcy_report（）函数

```c
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
```

`co_emcy_report()` 是 EMCY 模块的故障报告入口。只 专门处理**故障报告帧**，不处理清理帧

> **先把“我要报告的故障”整理成一帧 EMCY，然后检查有没有旧的失败帧、有没有重复的已报告故障；如果都没有，就更新 0x1001，并尝试发送。发送成功记为 active，发送失败记为 pending。**

```c
 发现 CAN 接收队列溢出
        ↓
我要报告这个故障
        ↓
调用 co_emcy_report()
        ↓
检查模块和错误码
        ↓
给 Error Register 自动加 Generic 位
        ↓
检查保留位bit5和bit6
        ↓
在局部变量 frame 中组装完整 EMCY 帧
        ↓
检查是否已有 pending 待重试帧
        ↓
检查是否是已经是 active 的重复故障，如果是相同的则不管了
        ↓
否则就是新的，那么更新对象字典 0x1001
        ↓
submit() 尝试发送
        ↓
   ↙          ↘
 成功          失败
  ↓             ↓
active=1      pending=1
记录故障       保存失败的帧
```

```c
active = 1 //表示某个故障报告帧已经成功发送，故障记录存到了active_error_code 和active_error_register 和 active_manufacturer
pending = 1 //表示有一帧 EMCY 发送失败，完整报文保存在 pending_frame 中，等待重试。

```



### 实例：

例如第一次报告 CAN 接收溢出：

```c
co_emcy_report(&emcy, 0xFF01, 0x10, NULL);
```

经过：

```
0x10 | 0x01 = 0x11
```

生成：

```
CAN-ID：0x081
Data：  01 FF 11 00 00 00 00 00
```

如果发送成功：

```c
active = 1
pending = 0

active_error_code = 0xFF01
active_error_register = 0x11
active_manufacturer = {0, 0, 0, 0, 0}
```

再次报告完全相同的故障时，三个活动故障字段都相同，于是返回：

```c
CO_IGNORED
```

不会重复发送。

如果第一次发送失败：

```c
active = 0
pending = 1
pending_frame = 故障报告帧
pending_is_reset = 0
    
//return submit(emcy, &frame, 0u)的else分支中
emcy->pending_frame = *frame;
emcy->pending = 1u;
emcy->pending_is_reset = is_reset;
```

之后重试成功，才变成：

```c
active = 1
pending = 0
active_error_* = 故障内容
```





### 总结：

##### 第一次就发送成功

```
发现故障
    ↓
co_emcy_report()
    ↓
故障报告帧发送成功
    ↓
active=1, pending=0
```

之后再次检测：

- 如果是完全相同的故障：返回 `CO_IGNORED`，不重复发送；
- 如果是不同故障：继续组帧并尝试发送。



##### 第一次发送失败

```c
发现故障
    ↓
co_emcy_report()
    ↓
发送失败
    ↓
pending_frame 保存完整故障帧
pending=1
pending_is_reset=0
```

之后由：

```
co_emcy_process(&emcy);
```

重试发送。重试成功后：

```c
active=1
pending=0
active_error_* 保存故障内容
```

补充一个重要限制：当前模块只有一个 `active_error_*` 记录和一个 `pending_frame` 位置。如果多个不同故障同时出现，暂时没有故障队列，不能完整保存多个故障。恢复后还需要通过 `co_emcy_clear()` 发送清除帧。





# 8：co_emcy_clear（）函数

```c
co_status_t co_emcy_clear(co_emcy_t *emcy)
{
    can_frame_t frame;                 // 保存待发送的 EMCY 清除帧。
    co_status_t status = validate(emcy); // 先检查 EMCY、节点和对象字典是否有效。

    if (status != CO_OK)
    {
        return status;                 // 参数或关联资源无效，不能继续清除。
    }

    /*
     * 如果已经有一帧报文等待重试：
     * - pending_is_reset != 0：说明清除帧已经在等待重试，
     *   再次调用清除没有意义，直接忽略。
     * - pending_is_reset == 0：说明故障报告帧还没发送成功，
     *   不能跳过故障报告直接发送清除帧，返回发送忙。
     */
    if (emcy->pending != 0u)
    {
        return (emcy->pending_is_reset != 0u)
                   ? CO_IGNORED
                   : CO_ERR_TX_BUSY;
    }

    /*
     * active == 0 表示当前没有已经成功上报、
     * 仍然有效的活动故障，因此不需要发送清除帧。
     */
    if (emcy->active == 0u)
    {
        return CO_IGNORED;
    }

    /*
     * 组装 EMCY 清除帧：
     * error_code    = 0
     * error_register = 0
     * manufacturer  = NULL
     *
     * 节点 ID 为 1 时，报文示例为：
     * CAN-ID = 0x081
     * Data   = 00 00 00 00 00 00 00 00
     */
    frame = make_frame(emcy, 0u, 0u, NULL);

    /*
     * 先把对象字典 0x1001:00 的 Error Register 清零，
     * 表示设备当前没有错误寄存器标志。
     */
    status = co_device_od_set_error(emcy->device, 0u);
    if (status != CO_OK)
    {
        return status;                 // 对象字典更新失败，不提交清除帧。
    }

    /*
     * 提交清除帧。
     * is_reset = 1 表示这是清除帧，而不是故障报告帧。
     *
     * 发送成功：remember_active() 清除 active 和 active_error_*。
     * 发送失败：submit() 保存 pending_frame，并设置：
     *           pending = 1
     *           pending_is_reset = 1
     */
    return submit(emcy, &frame, 1u);
}

```

`co_emcy_clear()` 是 **清除当前活动故障** 的入口。它不是报告新故障，而是在系统确认恢复后，发送一帧全零 EMCY 清除帧。

`co_emcy_report()` 是“我要宣布出故障了”；`co_emcy_clear()` 是“我之前宣布的这个故障已经恢复了，现在我要宣布清除它”。



### 1、先不要看代码，先看它到底想干什么

假设之前发生了：

```c
CAN 接收队列溢出
        ↓
co_emcy_report()
        ↓
EMCY 发送成功
```

现在状态：

```c
active  = 1
pending = 0
```

表示：

> **这个故障已经成功报告，而且目前仍然是一个活动故障。**

后来系统恢复了：

```
CAN 接收队列恢复正常
```

这时候：

```
co_emcy_clear(&emcy);
```

就是告诉 EMCY 模块：

> “这个活动故障现在恢复了，给我发一帧清除 EMCY。”



### 2、我们完整跑一次

假设现在：

```c
active  = 1
pending = 0

active_error_code
    = 0xFF01

active_error_register
    = 0x11
```

也就是说之前：

```
CAN RX Overflow
```

已经成功报告。



### 3、第一步：检查模块

```c
co_status_t status = validate(emcy);

if (status != CO_OK)
    return status;
```

就是：

> EMCY 模块现在还能不能正常工作？

假设：

```
status = CO_OK
```

继续。



### 4、第二步：最重要——检查 `pending`

```c
if (emcy->pending != 0u)
{
    return emcy->pending_is_reset != 0u
           ? CO_IGNORED
           : CO_ERR_TX_BUSY;
}
```

这一段你刚刚已经学过，所以现在应该很好理解。

它实际上分两种情况。

------



##### 情况 A：没有 pending

```
pending = 0
```

正常。

继续往下。

这就是我们现在所假设的情况。

------



##### 情况 B：有 pending

```c
pending = 1
```

那就继续看：

```
pending_is_reset
```

如果：

```
pending_is_reset = 1
```

说明：

```
已经有一张“清除帧”
正在等待重试
```

这时候你又调用：

```
co_emcy_clear()
```

没有意义。

因为：

```
清除帧 A
    ↓
已经在 pending
    ↓
你又要求清除
    ↓
还是同一个事情
```

所以：

```
return CO_IGNORED;
```

------

如果：

```
pending_is_reset = 0
```

说明现在 pending 的是：

```
故障报告帧
```

也就是：

```
故障报告还没发成功
```

这时候你却要求：

```
“把故障清掉”
```

不行。

因为系统还处于：

```
故障报告待发送
```

状态。

所以：

```
return CO_ERR_TX_BUSY;
```



### 5、第三步：检查 `active`

```c
if (emcy->active == 0u)
    return CO_IGNORED;
```

这个也很好理解。

如果：

```
active = 0
```

意味着：

> **当前根本没有一个已经成功报告、仍然有效的故障。**

那你清什么？

例如：

```
active = 0
pending = 0
```

调用：

```
co_emcy_clear();
```

就是：

```
“请清除当前故障”
```

但是：

```
“当前没有故障”
```

所以：

```
CO_IGNORED
```



### 6、所以到这里其实是在做“三道门”

你可以记成：

```c
co_emcy_clear()
       │
       ▼
① 有没有 pending？
       │
       ├── 清除帧 → CO_IGNORED
       │
       └── 故障帧 → CO_ERR_TX_BUSY
       │
       ▼
② active == 0？
       │
       └── 是 → CO_IGNORED
       │
       ▼
③ 真正制作清除帧
```





### 7、第四步：制作清除帧

```
frame = make_frame(emcy, 0u, 0u, NULL);
```

注意这里和 `co_emcy_report()` 的区别。

报告故障的时候：

```
error_code       = 0xFF01
error_register   = 0x11
manufacturer     = ...
```

而清除：

```
error_code       = 0
error_register   = 0
manufacturer     = NULL
```

于是：

```
CAN ID = 0x081       // 假设 Node ID = 1

Data =
00 00 00 00 00 00 00 00
```

这就是 EMCY 清除帧。

------



### 8、第五步：把 0x1001 清零

```
status = co_device_od_set_error(emcy->device, 0u);
```

于是：

```
0x1001:00 = 0x00
```

之前：

```
0x1001:00 = 0x11
```

现在：

```
0x1001:00 = 0x00
```

从对象字典角度：

> **当前 Error Register 已经没有错误标志。**

------



### 9、第六步：真正提交清除帧

```
return submit(emcy, &frame, 1u);
```

这里的：

```
1u
```

就是：

> **告诉 `submit()`：这是一张 reset/clear 清除帧。**

所以 `submit()` 后面会根据：

```
is_reset = 1
```

进行不同的状态处理。

------



### 10、如果清除帧发送成功

假设：

```
submit()
    ↓
CAN发送成功
```

那么之前：

```
active = 1
pending = 0
```

会变成：

```
active = 0
pending = 0
```

同时之前记录的：

```
active_error_code
active_error_register
active_manufacturer
```

也会被清掉/更新。

整个过程：

```c
发生故障
   ↓
report
   ↓
active = 1
   ↓
故障恢复
   ↓
clear
   ↓
发送 00 00 00 00 00 00 00 00
   ↓
active = 0
```

这就闭环了。



### 总览：

##### ① 检查 `pending`

```
if (emcy->pending != 0u)
```

确认有没有一帧 EMCY 正在等待重试。

- 如果待重试的是清除帧：说明清除操作已经排队，返回 `CO_IGNORED`；
- 如果待重试的是故障报告帧：说明故障报告还没成功发出，返回 `CO_ERR_TX_BUSY`，不能直接清除。



##### ② 检查 `active`

```
if (emcy->active == 0u)
```

确认是否存在已经成功上报、仍然有效的活动故障。

如果没有：

```
active = 0
```

就没有清除对象，返回 `CO_IGNORED`。



##### ③ 组装清除帧

前两步通过后，代码认为：

```
没有待重试帧
并且确实存在活动故障
```

于是直接制作清除帧：

```
frame = make_frame(emcy, 0u, 0u, NULL);
```

得到：

```
00 00 00 00 00 00 00 00
```

但这三个步骤仍然没有判断“系统是否稳定”。它们只是确认：

```
当前是否允许执行清除操作
```

“系统已经稳定 1 秒”这个更高层的条件，仍然由 `process_reliability()` 先判断。它确认稳定后才调用 `co_emcy_clear()`；`co_emcy_clear()` 再做这两个状态检查并组装清除帧。

所以完整关系是：

```c
process_reliability()
    先判断系统是否恢复稳定
    ↓
co_emcy_clear()
    再判断 pending 和 active
    ↓
组装并发送清除帧
```

也就是说，`co_emcy_clear()` 并不是一被调用就盲目发送，而是先确认当前 EMCY 状态允许清除；但它本身不负责等待系统稳定。





# 9：co_emcy_process（）函数

```c
co_status_t co_emcy_process(co_emcy_t *emcy)
{
    co_status_t status = validate(emcy);

    if (status != CO_OK)
        return status;
    if (emcy->pending == 0u)
        return CO_IGNORED;

    return submit(emcy, &emcy->pending_frame, emcy->pending_is_reset);
}
```

`co_emcy_process()` 是 EMCY 的**待重试处理函数**。它不生成新的报文，只负责把之前发送失败、保存在 `pending_frame` 中的报文再发一次。





### 实例：

`co_emcy_process()` 调用：

```c
//submit函数原型
static co_status_t submit(co_emcy_t *emcy, const can_frame_t *frame,
                          uint8_t is_reset)
submit(emcy,&emcy->pending_frame,emcy->pending_is_reset);
       
```

实际进入的是：

```c
static co_status_t submit(co_emcy_t *emcy,const can_frame_t *frame,uint8_t is_reset)                      
{
    co_status_t status = co_send(emcy->node, frame);

    if (status == CO_OK)
    {
        remember_active(emcy, frame, is_reset);
    }
    else
    {
        emcy->pending_frame = *frame;
        emcy->pending = 1u;
        emcy->pending_is_reset = is_reset;
    }

    return status;
}
```

可以按三步理解。



##### 1. 取出待重试帧并发送

```c
co_status_t status = co_send(emcy->node, frame);
```

这里的 `frame` 指针指向：

```
emcy->pending_frame
```

所以 `co_send()` 发送的就是之前保存的完整 但是没成功发送出去的EMCY 报文。

例如：

```c
pending_frame：
CAN-ID：0x081
Data：  01 FF 11 00 00 00 00 00
```

如果这是故障报告帧，`is_reset=0`；

如果是清除帧，`is_reset=1`。



##### 2. 重试成功

```c
if (status == CO_OK)
{
    remember_active(emcy, frame, is_reset);
}
```

发送成功后，调用 `remember_active()`：

- `is_reset=0`：说明故障报告帧成功发送，保存 `active_error_*`，设置 `active=1`；
- `is_reset=1`：说明清除帧成功发送，清空 `active_error_*`，设置 `active=0`。

同时，`remember_active()` 一开始会执行：

```c
emcy->pending = 0u;
emcy->pending_is_reset = 0u;
```

表示待重试帧已经成功发送，不再需要重试。



##### 3. 重试仍然失败

```c
else
{
    emcy->pending_frame = *frame;
    emcy->pending = 1u;
    emcy->pending_is_reset = is_reset;
}
```

如果 `co_send()` 仍然返回错误：

```
CO_ERR_TX_BUSY
```

就继续保持待重试状态。

```
emcy->pending_frame = *frame;
```

再次保存完整报文。这里由于当前 `frame` 本来就指向 `pending_frame`，实际上是把它自己复制回自己，确保待重试帧内容保持不变。

```
emcy->pending = 1u;
```

表示仍有报文没有发送成功。

```
emcy->pending_is_reset = is_reset;
```

继续保留报文类型：

```
0：故障报告帧
1：故障清除帧
```



##### 总览：

故障报告帧重试成功：

```
active=1
pending=0
active_error_* 保存故障内容
```

清除帧重试成功：

```
active=0
pending=0
active_error_* 清零
```

重试再次失败：

```
pending=1
pending_frame 保持原报文
pending_is_reset 保持原类型
```

所以 `submit()` 的核心职责就是：

```
尝试发送一帧
    ↓
成功：更新 active 状态并清除 pending
失败：保留 pending 帧和类型，等待下一次重试
```