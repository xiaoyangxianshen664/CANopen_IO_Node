# 解析



## 1：宏常量

```c
#define CO_DEVICE_OD_COUNT 35u		//表示：当前设备对象表一共有 35 个对象条目。
```

后面会用它定义数组大小：

```
co_device_value_t fields[CO_DEVICE_OD_COUNT];
co_od_entry_t entries[CO_DEVICE_OD_COUNT];
```

经过替换后，相当于：

```c
co_device_value_t fields[35];
co_od_entry_t entries[35];
```

所以：

```c
definitions[] → 35 条对象规则
fields[]      → 35 个对象当前值
entries[]     → 35 个实际对象条目
```





## 2：对象当前数据的存储结构

```c
typedef struct {
    uint32_t value;
    uint32_t writes;
} co_device_value_t;
```

它有两个成员

#### 1：`value`

```
uint32_t value;
```

保存这个对象当前的数值。

例如：创建一个变量co_device_value_t  fields= {0，5};

```
device.fields[11].value = 5;
```

表示 DO 对象当前值是 `5`。

不同对象的 `value` 可以保存不同内容：

```
DI 对象   → 当前 DI 位状态
DO 对象   → 当前 DO 输出命令
AI 对象   → 当前 ADC 原始值
心跳对象  → 当前心跳周期
```

------

#### 2：`writes`

```
uint32_t writes;
```

记录这个对象通过写回调成功写入了多少次。

例如：

```
device.fields[11].writes = 0;
```

第一次成功写入 DO：

```
value  = 5
writes = 1
```

再次成功写入：

```
value  = 新值
writes = 2
```

这个成员主要用于：

- 测试写回调是否真的被调用。
- 观察心跳周期是否被重新写入。
- 调试对象值的修改次数。

它不是 CANopen 规范中必须存在的对象字段，而是我们项目内部的辅助记录。



#### 3：创建一个变量

```
co_device_value_t output = {0};
```

等价于：

```c
output.value  = 0
output.writes = 0
```

也可以明确写：

```c
co_device_value_t output = {
    .value = 3,
    .writes = 0
};
```

表示：

```c
alue = 3 表示当前值为 3
writes = 0表示还没有发生成功写入
```



#### 4：放进 `fields[]`

在完整设备对象中：

```c
typedef struct {
    co_device_value_t fields[CO_DEVICE_OD_COUNT];
    co_od_entry_t entries[CO_DEVICE_OD_COUNT];
    co_od_table_t table;
} co_device_od_t;

// co_device_value_t fields[CO_DEVICE_OD_COUNT];
含义是：
创建一个名为 fields 的数组，数组中有 35 个元素，每个元素的类型都是 co_device_value_t 结构体。

每个元素都有：
fields[i].value
fields[i].writes
例如：
fields[11].value = 5;
fields[11].writes = 1;
就是修改第 12 个结构体元素中的成员。
项目中：
fields[9]  → DI 当前数据
fields[11] → DO 当前数据
fields[13] → AI1 当前数据
fields[14] → AI2 当前数据
所以你这句话可以准确记成：
fields[] 是一个结构体数组，里面有 35 个 co_device_value_t 类型的结构体变量，每个元素保存一个对象的当前值和写入次数。
    
//因此
fields[i]
    ├── value
    └── writes
例如：
fields[11]
    ├── value  → 0x6200:01 DO 当前值
    └── writes → DO 成功写入次数
初始化时：
device->fields[i].value = d->initial;
device->fields[i].writes = 0;
也就是：
按照规则模板设置当前初值
把写入次数清零
```



#### 5：和读写回调的关系

读回调：

```
read_value(&device->fields[i], data, length);
```

它读取：

```
cell->value
```

但不会修改：

```
cell->writes
```

写回调：

```
write_value(&device->fields[i], data, length);
```

它会：

```
cell->value = next;
++cell->writes;
```

所以可以记成：

```c
co_device_value_t
    ├── value  → 当前对象值
    └── writes → 成功写入次数

read_value()
    └── 读取 value，写入 data[]

write_value()
    ├── 从 data[] 组合新 value
    └── writes 加 1
```









### 3：**设备身份信息结构体类型**

```c
typedef struct {
    uint32_t vendor_id;			//厂商编号
    uint32_t product_code;		//产品编号
    uint32_t revision;			//产品或软件版本
    uint32_t serial;			//序列号
} co_device_identity_t;
```

创建一个身份变量

```c
co_device_identity_t identity = {
    123u,
    456u,
    789u,
    321u
};
```

对应：

```c
identity
 ├── vendor_id    = 123
 ├── product_code = 456
 ├── revision     = 789
 └── serial       = 321
```







### 4：实际对象表结构体

```c
typedef struct {
    co_device_value_t fields[CO_DEVICE_OD_COUNT];
    co_od_entry_t entries[CO_DEVICE_OD_COUNT];
    co_od_table_t table;
} co_device_od_t;

```

它把三类东西放在同一个设备对象里：

```c
co_device_od_t device
    ├── fields[35]   当前数据
    ├── entries[35]  对象条目
    └── table        对 entries[] 的访问入口
```



#### 1. `fields[35]`

```
co_device_value_t fields[CO_DEVICE_OD_COUNT];
```

`fields` 是一个数组，里面有 35 个 `co_device_value_t` 结构体。

假设 `co_device_value_t` 是：

```
typedef struct {
    uint32_t value;
    uint32_t writes;
} co_device_value_t;
```

那么：

```
fields[0]  → 第 0 个对象的当前值
fields[1]  → 第 1 个对象的当前值
fields[9]  → DI 当前值
fields[11] → DO 当前值
fields[13] → AI1 当前值
fields[14] → AI2 当前值
```

例如：

```
device.fields[11].value = 5;
```

表示把第 11 个槽位的当前值设置为 5。