/* 设备对象表集成测试；GB2312（代码页 936）。 */
#include "co_device_od.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
/* 表达式为假时输出文件、行号和表达式文本，并结束当前测试返回1；真则继续。 */
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

/* 用途：按 index/sub 查条目，经读回调取小端字节，再合成为整数便于 CHECK 比较。
 * d是设备地址，index/sub是对象地址；成功返回值，失败返回UINT32_MAX。
 * UINT32_MAX也可能是合法数据，故这里只作测试辅助，不是通用错误返回协议。
 * e是接收条目地址的指针变量；&e让co_od_find能修改e本身。 */
static uint32_t read_at(co_device_od_t *d, uint16_t index, uint8_t sub)
{
    const co_od_entry_t *e = NULL;
    uint8_t bytes[4] = {0}; /* 最多4字节，容纳当前支持的所有类型 */
    uint32_t result = 0;
    uint8_t i;
    /* ||短路：找不到时不调用co_od_read，也不访问空指针e->length。 */
    if (co_od_find(&d->table,index,sub,&e) != CO_OK ||
        co_od_read(e,bytes,e->length) != CO_OK) return UINT32_MAX;
    for (i=0;i<e->length;++i) result |= (uint32_t)bytes[i] << (8u*i);
    return result;
}

/* 用途：测试用写入口，把整数value拆成小端字节，再调用通用对象写接口。
 * state模拟调用方的NMT状态，length故意允许传错，以测试长度检查。
 * 返回查找或写入的状态码；合法提交最终会调用设备模块的write_value。 */
static co_status_t write_at(co_device_od_t *d, uint16_t index, uint8_t sub,
                            co_nmt_state_t state, uint32_t value, uint8_t length)
{
    const co_od_entry_t *e = NULL;
    uint8_t bytes[4];
    uint8_t i;
    co_status_t status = co_od_find(&d->table,index,sub,&e);
    if (status != CO_OK) return status;
    for (i=0;i<4;++i) bytes[i]=(uint8_t)(value >> (8u*i));
    return co_od_write(e,state,bytes,length);
}

/* 用途：验证实际36条对象的连接、默认值、访问保护和实例隔离。
 * 返回：全部CHECK通过为0，任一CHECK失败立即为1，由CTest判定本组结果。
 * 下方按功能分组；d与other是两份独立设备，而不是同一设备的两个指针。 */
static int test_device(void)
{
    co_device_od_t d = {0}, other = {0};
    const co_od_entry_t *e = NULL;
    co_device_identity_t identity = {123,456,789,321}; /* 测试数据，不代表真实注册身份 */
    size_t i,j;
    uint8_t output=99; /* 故意不同于默认DO，确认接口确实填写此变量 */
    uint16_t period=0;
    uint32_t writes=0;
    /* 1. 无效参数与初始化：清零不等于已初始化；失败重初始化不能破坏配置。 */
    CHECK(co_device_od_init(NULL,1,NULL)==CO_ERR_ARGUMENT);
    CHECK(co_device_od_get_outputs(&d,&output)==CO_ERR_ARGUMENT);
    CHECK(co_device_od_init(&d,1,NULL)==CO_OK);
    CHECK(d.table.count==36);
    CHECK(co_device_od_init(&d,0,NULL)==CO_ERR_NODE_ID);
    CHECK(co_device_od_init(&d,128,NULL)==CO_ERR_NODE_ID);
    CHECK(read_at(&d,0x1400,1)==0x201); /* 失败初始化保留原配置。 */
    /* 2. 基础对象和固定映射：核对开发身份、COB-ID标志、映射及默认周期。 */
    CHECK(read_at(&d,0x1000,0)==0x70191);
    CHECK(read_at(&d,0x1018,0)==4 && read_at(&d,0x1018,1)==0);
    CHECK(read_at(&d,0x1018,2)==1 && read_at(&d,0x1018,3)==0x10000);
    CHECK(read_at(&d,0x1018,4)==0);
    CHECK(read_at(&d,0x1800,1)==0x40000181 && read_at(&d,0x1801,1)==0x40000281);
    CHECK(read_at(&d,0x1600,1)==0x62000108 && read_at(&d,0x1A00,1)==0x60000108);
    CHECK(read_at(&d,0x1A01,1)==0x64010110 && read_at(&d,0x1A01,2)==0x64010210);
    CHECK(read_at(&d,0x1800,0)==5 && read_at(&d,0x1801,5)==0);
    CHECK(co_od_find(&d.table,0x1800,4,&e)==CO_ERR_OD_NOT_FOUND);
    CHECK(co_od_find(&d.table,0x2000,0,&e)==CO_ERR_OD_NOT_FOUND);
    /* 3. 遍历所有条目：每个都能读；两两比较地址组合，排除重复；只读项拒写。 */
    for (i=0;i<d.table.count;++i) { /* 所有条目唯一，且都能成功读取。 */
        uint8_t data[4]={0};
        e=&d.entries[i];
        CHECK(co_od_read(e,data,e->length)==CO_OK);
        for (j=i+1;j<d.table.count;++j)
            CHECK(e->index!=d.entries[j].index || e->subindex!=d.entries[j].subindex);
        if (e->access!=CO_OD_READ_WRITE)
            CHECK(co_od_write(e,CO_NMT_PRE_OPERATIONAL,data,e->length)==CO_ERR_OD_READ_ONLY);
    }
    /* 4. DI/AI：先成功更新，再分别构造DI和两路AI越界；失败必须保留整组旧值。 */
    CHECK(co_device_od_update_inputs(&d,15,0x123,4095)==CO_OK);
    CHECK(read_at(&d,0x6000,1)==15 && read_at(&d,0x6401,1)==0x918);
    CHECK(read_at(&d,0x6401,2)==32760);
    CHECK(co_od_find(&d.table,0x6401,1,&e)==CO_OK && e->type==CO_OD_INTEGER16);
    CHECK(co_device_od_update_inputs(&d,16,0,0)==CO_ERR_OD_VALUE);
    CHECK(co_device_od_update_inputs(&d,0,4096,0)==CO_ERR_OD_VALUE);
    CHECK(co_device_od_update_inputs(&d,0,0,65535)==CO_ERR_OD_VALUE);
    CHECK(read_at(&d,0x6000,1)==15 && read_at(&d,0x6401,1)==0x918);
    CHECK(read_at(&d,0x6401,2)==32760); /* 整组失败，不能更新一半。 */
    /* 5. DO：写15表示低四位全置1；16使用保留位，必须拒绝；长度错误也不改值。 */
    CHECK(write_at(&d,0x6200,1,CO_NMT_OPERATIONAL,15,1)==CO_OK);
    CHECK(co_device_od_get_outputs(&d,&output)==CO_OK && output==15);
    CHECK(write_at(&d,0x6200,1,CO_NMT_OPERATIONAL,16,1)==CO_ERR_OD_VALUE);
    CHECK(write_at(&d,0x6200,1,CO_NMT_OPERATIONAL,0,2)==CO_ERR_OD_LENGTH);
    CHECK(read_at(&d,0x6200,1)==15);
    /* 6. 心跳：默认1000ms，写0关闭，写65535取上界；重复写同值仍累计次数。 */
    CHECK(co_device_od_get_heartbeat(&d,&period,&writes)==CO_OK && period==1000 && writes==0);
    CHECK(write_at(&d,0x1017,0,CO_NMT_PRE_OPERATIONAL,0,2)==CO_OK);
    CHECK(write_at(&d,0x1017,0,CO_NMT_OPERATIONAL,65535,2)==CO_OK);
    CHECK(write_at(&d,0x1017,0,CO_NMT_OPERATIONAL,65535,2)==CO_OK);
    CHECK(co_device_od_get_heartbeat(&d,&period,&writes)==CO_OK && period==65535 && writes==3);
    CHECK(write_at(&d,0x1017,0,CO_NMT_OPERATIONAL,1,1)==CO_ERR_OD_LENGTH);
    /* 7. 两组TPDO：抑制时间固定0；事件周期仅Pre-operational允许写。 */
    for (i=0;i<2;++i) {
        uint16_t index=(uint16_t)(0x1800+i);
        /* 固定COB-ID不提供禁用入口：抑制时间固定0；事件周期仍可配置。 */
        CHECK(write_at(&d,index,3,CO_NMT_PRE_OPERATIONAL,9,2)==CO_ERR_OD_READ_ONLY);
        CHECK(read_at(&d,index,3)==0);
        CHECK(write_at(&d,index,5,CO_NMT_OPERATIONAL,9,2)==CO_ERR_OD_STATE);
        CHECK(write_at(&d,index,5,CO_NMT_STOPPED,9,2)==CO_ERR_OD_STATE);
        CHECK(write_at(&d,index,5,CO_NMT_INITIALIZATION,9,2)==CO_ERR_OD_STATE);
        CHECK(read_at(&d,index,5)==0);
        CHECK(write_at(&d,index,5,CO_NMT_PRE_OPERATIONAL,65535,2)==CO_OK);
        CHECK(read_at(&d,index,5)==65535);
        CHECK(write_at(&d,index,5,CO_NMT_PRE_OPERATIONAL,0,2)==CO_OK);
    }
    /* 模拟量变化开关默认关闭，布尔值/长度错误不修改旧值。 */
    CHECK(read_at(&d,0x6423,0)==0);
    CHECK(write_at(&d,0x6423,0,CO_NMT_PRE_OPERATIONAL,1,1)==CO_OK);
    CHECK(write_at(&d,0x6423,0,CO_NMT_PRE_OPERATIONAL,2,1)==CO_ERR_OD_VALUE);
    CHECK(write_at(&d,0x6423,0,CO_NMT_PRE_OPERATIONAL,0,2)==CO_ERR_OD_LENGTH);
    CHECK(read_at(&d,0x6423,0)==1);
    CHECK(write_at(&d,0x6423,0,CO_NMT_OPERATIONAL,0,1)==CO_OK);
    /* 单极性左对齐：中点不能变成负数，最大值低三位为0。 */
    CHECK(co_device_od_update_inputs(&d,0,2048,4095)==CO_OK);
    CHECK(read_at(&d,0x6401,1)==0x4000 && read_at(&d,0x6401,2)==0x7ff8);
    {
        uint8_t bytes[2]={0};
        CHECK(co_od_find(&d.table,0x6401,2,&e)==CO_OK);
        CHECK(co_od_read(e,bytes,2)==CO_OK && bytes[0]==0xf8 && bytes[1]==0x7f);
    }
    CHECK(co_device_od_update_inputs(&d,0,0,1)==CO_OK);
    CHECK(read_at(&d,0x6401,1)==0 && read_at(&d,0x6401,2)==8);
    /* 通用BOOLEAN校验不能仅依赖条目自定义上限。 */
    {
        co_od_entry_t boolean_entry;
        uint32_t before;
        uint8_t invalid=2;
        CHECK(co_od_find(&d.table,0x6423,0,&e)==CO_OK);
        boolean_entry=*e;
        boolean_entry.max_value=255;
        before=d.fields[35].writes;
        CHECK(co_od_write(&boolean_entry,CO_NMT_PRE_OPERATIONAL,&invalid,1)==CO_ERR_OD_VALUE);
        CHECK(d.fields[35].writes==before);
    }
    /* 8. 错误寄存器：合法位可由应用更新；保留bit6置位失败，旧错误值不变。 */
    CHECK(co_device_od_set_error(&d,0x10)==CO_OK);
    CHECK(read_at(&d,0x1001,0)==0x11);
    CHECK(co_device_od_set_error(&d,0x20)==CO_ERR_OD_VALUE);
    CHECK(co_device_od_set_error(&d,0x40)==CO_ERR_OD_VALUE);
    CHECK(read_at(&d,0x1001,0)==0x11);
    /* 9. 第二台设备：最大节点号127，自定义身份不串到d，DO存储也彼此独立。 */
    CHECK(co_device_od_init(&other,127,&identity)==CO_OK);
    CHECK(read_at(&other,0x1400,1)==0x27f && read_at(&other,0x1800,1)==0x400001ff);
    CHECK(read_at(&other,0x1801,1)==0x400002ff && read_at(&d,0x1400,1)==0x201);
    CHECK(read_at(&other,0x1018,1)==123 && read_at(&other,0x1018,4)==321);
    CHECK(read_at(&other,0x6200,1)==0 && read_at(&d,0x6200,1)==15);
    /* 10. 重新初始化：恢复安全DO=0、心跳默认值；最后检查各输出指针为NULL。 */
    CHECK(co_device_od_init(&d,1,NULL)==CO_OK);
    CHECK(read_at(&d,0x6200,1)==0 && read_at(&d,0x1017,0)==1000);
    CHECK(read_at(&d,0x6423,0)==0);
    CHECK(write_at(&d,0x6200,1,CO_NMT_OPERATIONAL,5,1)==CO_OK);
    CHECK(co_device_od_get_outputs(&d,&output)==CO_OK && output==5);
    CHECK(co_device_od_force_safe_outputs(&d)==CO_OK);
    CHECK(co_device_od_get_outputs(&d,&output)==CO_OK && output==0);
    CHECK(read_at(&d,0x6200,1)==0);
    CHECK(co_device_od_force_safe_outputs(NULL)==CO_ERR_ARGUMENT);
    CHECK(co_device_od_get_outputs(&d,NULL)==CO_ERR_ARGUMENT);
    CHECK(co_device_od_get_heartbeat(&d,NULL,&writes)==CO_ERR_ARGUMENT);
    CHECK(co_device_od_get_heartbeat(&d,&period,NULL)==CO_ERR_ARGUMENT);
    CHECK(co_device_od_update_inputs(NULL,0,0,0)==CO_ERR_ARGUMENT);
    CHECK(co_device_od_set_error(NULL,0)==CO_ERR_ARGUMENT);
    return 0;
}

/* 用有符号边界验证通用写检查；回调只保存两个字节，不替代范围检查。 */
static co_status_t signed_write(void *user,const uint8_t *data,uint8_t length)
{
    (void)length; /* 本回调固定复制2字节；显式标记参数未用，避免编译警告 */
    memcpy(user,data,2); /* user是saved数组首地址；保存收到的字节，不自行解码 */
    return CO_OK;
}
/* 用途：验证新增INTEGER16类型的正负边界、范围限制与失败不调用回调。
 * 这里临时构造0x2000测试条目，不把它加入设备实际对象表或EDS。 */
static int test_signed(void)
{
    /* 小端补码：00 80表示0x8000即-32768；FF 7F表示0x7FFF即32767。 */
    uint8_t saved[2]={0}, negative[2]={0,0x80}, positive[2]={0xff,0x7f};
    co_od_entry_t e={0x2000,0,CO_OD_INTEGER16,CO_OD_READ_WRITE,2,
                     -32768,32767,0,NULL,signed_write,saved};
    CHECK(co_od_write(&e,CO_NMT_PRE_OPERATIONAL,negative,2)==CO_OK);
    CHECK(saved[1]==0x80);
    CHECK(co_od_write(&e,CO_NMT_PRE_OPERATIONAL,positive,2)==CO_OK);
    CHECK(saved[0]==0xff && saved[1]==0x7f);
    e.min_value=0; e.max_value=4095; /* 收紧为ADC范围，两种边界值此时都应被拒绝 */
    CHECK(co_od_write(&e,CO_NMT_PRE_OPERATIONAL,negative,2)==CO_ERR_OD_VALUE);
    CHECK(co_od_write(&e,CO_NMT_PRE_OPERATIONAL,positive,2)==CO_ERR_OD_VALUE);
    CHECK(co_od_write(&e,CO_NMT_PRE_OPERATIONAL,positive,1)==CO_ERR_OD_LENGTH);
    CHECK(saved[1]==0x7f); /* 三次失败都没有执行signed_write，保存值仍是上次成功结果 */
    return 0;
}

/* 用途：初始化指定节点，并把真实读出的对象表输出为逗号分隔文本。
 * 参数node为1..127；返回0表示全部导出，CHECK失败为1。
 * test_eds.py通过命令行调用dump，捕获标准输出与EDS逐项比较。
 * 列顺序：索引、子索引、EDS类型、字节数、访问标记、当前值、下限、上限。 */
static int dump(unsigned node)
{
    co_device_od_t d;
    size_t i;
    CHECK(node>=1 && node<=127);
    CHECK(co_device_od_init(&d,(uint8_t)node,NULL)==CO_OK);
    for(i=0;i<d.table.count;++i) {
        const co_od_entry_t *e=&d.entries[i];
        /* 内部枚举不是EDS类型号：INTEGER16=3，U8=5，U16=6，U32=7。 */
        unsigned type=e->type==CO_OD_BOOLEAN ? 1u : e->type==CO_OD_INTEGER16 ? 3u :
                      e->type==CO_OD_UNSIGNED8 ? 5u : e->type==CO_OD_UNSIGNED16 ? 6u : 7u;
        /* PRIu32/PRId64由inttypes.h提供，与固定宽度整数匹配且便于跨平台。 */
        printf("%04X,%u,%u,%u,%u,%" PRIu32 ",%" PRId64 ",%" PRId64 "\n",
               e->index,e->subindex,type,e->length,(unsigned)e->access,
               read_at(&d,e->index,e->subindex),e->min_value,e->max_value);
    }
    return 0;
}
/* 命令行入口：argc含程序名，argv[1]为第一个附加参数。
 * test_co_device_od.exe             -> 运行设备对象测试。
 * test_co_device_od.exe signed      -> 运行有符号类型测试。
 * test_co_device_od.exe dump 1      -> 导出节点1对象表供Python比较。
 * strtoul按十进制解析节点号；这是受控测试入口，不是产品命令行解析器。
 * 其余参数组合在当前实现中也会进入test_device；返回值交给CTest。 */
int main(int argc,char **argv)
{
    if(argc==3 && strcmp(argv[1],"dump")==0) return dump((unsigned)strtoul(argv[2],NULL,10));
    if(argc==2 && strcmp(argv[1],"signed")==0) return test_signed();
    return test_device();
}
