#include "canopen_app.h"

#include <stddef.h>

#include "bsp_can.h"
#include "co_core.h"
#include "co_device_od.h"
#include "co_emcy.h"
#include "co_hb.h"
#include "co_nmt.h"
#include "co_pdo.h"
#include "co_sdo.h"

/*
这些是应用层长期保存的协议对象：
- node：节点基础上下文
- device：对象字典
- nmt：NMT 管理
- heartbeat：Heartbeat
- sdo：SDO 服务
- pdo：PDO 服务
static 表示它们只在当前 .c 文件中可见，并且整个程序运行期间一直存在。不能定义在初始化函数内部，否则函数结束后就失效。
*/
static co_context_t node;
static co_device_od_t device;
static co_nmt_t nmt;
static co_hb_t heartbeat;
static co_sdo_t sdo;
static co_pdo_t pdo;
static co_emcy_t emcy;
static uint8_t safe_output_active;
static uint8_t safe_output_applied;

/*保存最近一次错误状态。volatile 表示这个变量可能被任务、中断或调试器观察，编译器不要擅自优化掉读取。*/
volatile co_status_t canopen_last_error = CO_OK;

co_status_t Canopen_App_Init(void)
{
    co_status_t status;

    status = co_init(&node, 1u, CAN_SendFrame, NULL);
    if (status != CO_OK)
        return status;
    status = co_device_od_init(&device, 1u, NULL);
    if (status != CO_OK)
        return status;
    status = co_nmt_init(&nmt, &node, &device, NULL, NULL);
    if (status != CO_OK)
        return status;
    status = co_hb_init(&heartbeat, &node, &device);
    if (status != CO_OK)
        return status;
    status = co_sdo_init(&sdo, &node, &device);
    if (status != CO_OK)
        return status;
    status = co_pdo_init(&pdo, &node, &device);
    if (status != CO_OK)
        return status;
    status = co_emcy_init(&emcy, &node, &device);
    if (status != CO_OK)
        return status;
    safe_output_active = 0u;
    safe_output_applied = 0u;

    /* Boot-up 必须在 CAN 外设已经启动后发送。 */
    return co_nmt_bootup(&nmt);
}

co_status_t Canopen_App_NmtReceive(const can_frame_t *frame)
{
    return co_nmt_receive(&nmt, frame);
}

co_status_t Canopen_App_SdoReceive(const can_frame_t *frame)
{
    return co_sdo_receive(&sdo, frame);
}

co_status_t Canopen_App_PdoReceive(const can_frame_t *frame)
{
    return co_pdo_receive(&pdo, frame);
}

co_status_t Canopen_App_ProcessHeartbeat(uint32_t elapsed_ms)
{
    return co_hb_process(&heartbeat, elapsed_ms);
}

co_status_t Canopen_App_ProcessPdo(uint32_t elapsed_ms)
{
    return co_pdo_process(&pdo, elapsed_ms);
}

co_status_t Canopen_App_UpdateInputs(uint8_t di, uint16_t ai1, uint16_t ai2)
{
    return co_device_od_update_inputs(&device, di, ai1, ai2);
}

co_status_t Canopen_App_GetOutputs(uint8_t *outputs)
{
    co_status_t status;

    if (outputs == NULL)
        return CO_ERR_ARGUMENT;
    if (safe_output_active != 0u)
    {
        status = co_device_od_force_safe_outputs(&device);
        if (status != CO_OK)
            return status;
        safe_output_applied = 1u;
        *outputs = 0u;
        return CO_OK;
    }
    if (node.state != CO_NMT_OPERATIONAL)
    {
        if (safe_output_applied == 0u)
        {
            status = co_device_od_force_safe_outputs(&device);
            if (status != CO_OK)
                return status;
            safe_output_applied = 1u;
        }
        *outputs = 0u;
        return CO_OK;
    }
    safe_output_applied = 0u;
    return co_device_od_get_outputs(&device, outputs);
}

uint8_t Canopen_App_IsOperational(void)
{
    return (uint8_t)(node.state == CO_NMT_OPERATIONAL);
}

void Canopen_App_SetSafeOutput(uint8_t active)
{
    safe_output_active = (active != 0u) ? 1u : 0u;
}

co_status_t Canopen_App_ReportEmcy(uint16_t error_code, uint8_t error_register,
                                   const uint8_t manufacturer[CO_EMCY_MANUFACTURER_LENGTH])
{
    return co_emcy_report(&emcy, error_code, error_register, manufacturer);
}

co_status_t Canopen_App_ClearEmcy(void)
{
    return co_emcy_clear(&emcy);
}

co_status_t Canopen_App_ProcessEmcy(void)
{
    return co_emcy_process(&emcy);
}
