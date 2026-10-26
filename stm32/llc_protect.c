/**
 * llc_protect.c — 分级保护: 硬件逐周期 + 软件锁存 + 受控重试
 *
 * 分级思想(答辩素材):
 *   L1 硬件逐周期: 外部比较器(i_res峰值) -> TIM1 BKIN, <1us 封锁六路,
 *      不经软件 —— 炸管速度远快于任何中断响应;
 *   L2 软件快速(每周期采样): 软件过流/过压预判, 提前降频或停机;
 *   L3 慢速(1ms): OVP/UVLO/OTP, 状态机管理故障锁存与重试。
 * 重试策略: 冷却 500ms -> 软启动重试, 最多 3 次(重启可自愈的瞬态故障),
 *   超过后永久锁死, 必须断电复位 —— 让"能自动好"和"必须人查"有明确边界。
 */
#include "llc_protect.h"
#include "llc_config.h"
#include "llc_pwm.h"
#include "llc_control.h"

typedef enum { F_NONE = 0, F_OCP_HW, F_OCP_SW, F_OVP, F_UVLO, F_OTP } FaultCode;

static LlcState  s_state = LLC_ST_STANDBY;
static FaultCode s_code  = F_NONE;
static uint8_t   s_retry = 0;
static uint16_t  s_cooldown_ms = 0;
static uint8_t   s_hw_ocp_flag = 0;

void LLC_Protect_Init(void)
{
    s_state = LLC_ST_STANDBY; s_code = F_NONE;
    s_retry = 0; s_cooldown_ms = 0;
}

void LLC_Protect_OnHardwareOCP(void)
{
    s_hw_ocp_flag = 1;                 /* 中断上下文: 只置旗, 处理在 Tick */
}

LlcState LLC_Protect_GetState(void)   { return s_state; }
uint8_t  LLC_Protect_FaultCode(void)  { return (uint8_t)s_code; }

static void fault_enter(FaultCode c)
{
    s_code = c;
    LLC_PWM_Break();                   /* 六路进入安全态 */
    s_cooldown_ms = LLC_RETRY_COOLDOWN_MS;
    s_state = LLC_ST_FAULT_RETRY;
}

LlcState LLC_Protect_Tick(float vout, float vin, float ires_pk, float ntc_c)
{
    /* 1) 硬件 OCP 旗(BKIN 已由硬件封锁输出) */
    if (s_hw_ocp_flag) {
        s_hw_ocp_flag = 0;
        fault_enter(F_OCP_HW);
    }

    switch (s_state) {
    case LLC_ST_STANDBY:
        if (vin > LLC_VIN_UVLO_V && ntc_c < LLC_NTC_OTP_C) {
            s_state = LLC_ST_RUN;
        }
        break;

    case LLC_ST_RUN:
        if (vout > LLC_VOUT_OVP_V)            fault_enter(F_OVP);
        else if (vin < LLC_VIN_UVLO_V)        fault_enter(F_UVLO);
        else if (ntc_c > LLC_NTC_OTP_C)       fault_enter(F_OTP);
        else if (ires_pk > LLC_IRES_OCP_A)    fault_enter(F_OCP_SW);  /* 软件后备 */
        break;

    case LLC_ST_FAULT_RETRY:
        if (s_code == F_OVP || s_code == F_OTP) {
            /* 破坏性故障不允许自动重试 */
            s_state = LLC_ST_FAULT_LATCH;
            break;
        }
        if (--s_cooldown_ms == 0U) {
            s_retry++;
            if (s_retry > LLC_FAULT_RETRY) {
                s_state = LLC_ST_FAULT_LATCH;
            } else {
                LLC_Control_ResetSoftStart();
                LLC_PWM_ReleaseBreak();
                s_state = (vin > LLC_VIN_UVLO_V) ? LLC_ST_RUN : LLC_ST_STANDBY;
            }
        }
        break;

    case LLC_ST_FAULT_LATCH:
    default:
        LLC_PWM_Break();               /* 锁死: 仅断电可复位 */
        break;
    }
    return s_state;
}
