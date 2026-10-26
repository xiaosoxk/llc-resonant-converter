/**
 * llc_control.c — 电压外环 PI(抗饱和) + 软启动 + 轻载 burst
 *
 * PFM 调节方向: Vout 偏低 -> 需要更高增益 -> 频率向 fr 以下调(欠谐振区升压);
 *   Vout 偏高 -> 频率上抬(fr 以上增益<1, 降压区)。PI 输出直接定义为
 *   "降频深度 df", f_cmd = fr - df, df∈[0, fr-fmin] -> 积分器天然带物理限幅,
 *   抗饱和就是把 df 的积分钳位在该区间, 避免大扰动后长时间"解饱和"。
 * 采样->执行延迟约 1.5 个开关周期(采样半拍+计算半拍), 环路带宽取 fs/50 量级,
 *   PI 参数整定见论文 5.4 节(负载阶跃法)。
 * 轻载: 频率推到 fmax 仍过高 -> burst 打嗝(窗口式门控), 防止进入容性区。
 */
#include "llc_control.h"
#include "llc_config.h"
#include "llc_pwm.h"

static float s_vref      = 0.0f;          /* 软启动基准 */
static float s_integ     = 0.0f;          /* PI 积分项(=稳态降频深度) */
static uint32_t s_ss_ms  = 0;
static uint8_t  s_burst_on = 0;           /* burst 窗口状态 */
static uint16_t s_burst_cnt = 0;

void LLC_Control_Init(void)
{
    s_vref = 0.0f; s_integ = 0.0f; s_ss_ms = 0;
    s_burst_on = 0; s_burst_cnt = 0;
    LLC_PWM_SetFrequency(LLC_F_MAX_HZ);
}

void LLC_Control_ResetSoftStart(void)
{
    s_vref = 0.0f; s_ss_ms = 0;
}

/* 每毫秒调用: 基准斜坡 */
static void softstart_tick(void)
{
    if (s_ss_ms < LLC_SS_MS) s_ss_ms++;
    s_vref = LLC_VOUT_SET * (float)s_ss_ms / (float)LLC_SS_MS;
}

float LLC_Control_Update(const LlcSample *s, uint8_t running)
{
    if (!running) { LLC_PWM_SetFrequency(LLC_F_MAX_HZ); return LLC_F_MAX_HZ; }

    /* PI: err>0 表示输出偏低 -> 增大降频深度 */
    float err = s->vref - s->vout;
    s_integ += LLC_KI * err;
    if (s_integ > LLC_PI_OUT_MAX) s_integ = LLC_PI_OUT_MAX;   /* 抗饱和钳位 */
    if (s_integ < 0.0f)           s_integ = 0.0f;
    float df = LLC_KP * err + s_integ;
    if (df > LLC_PI_OUT_MAX) df = LLC_PI_OUT_MAX;
    if (df < 0.0f)           df = 0.0f;

    float f_cmd = LLC_FR_HZ - df;
    LLC_PWM_SetFrequency(f_cmd);
    return f_cmd;
}

/* 每1ms调用: 软启动基准 + 轻载 burst
 * 判据: 频率已顶到 fmax 且输出仍超设定 -> 间歇停脉冲(打嗝)而不是进入
 * 失控的更高频段; 恢复用迟滞防止抖动 */
void LLC_Control_BurstTick(float vout, float fcmd)
{
    softstart_tick();
    if (fcmd >= LLC_F_MAX_HZ - 1.0f && vout > LLC_VOUT_SET + LLC_BURST_HYS_V) {
        s_burst_on = 1;
    } else if (vout < LLC_VOUT_SET - LLC_BURST_HYS_V) {
        s_burst_on = 0;
    }
    if (s_burst_on) {
        if (++s_burst_cnt >= 100U) s_burst_cnt = 0;   /* 100ms 窗口 */
        if (s_burst_cnt < 60U)  LLC_PWM_Break();       /* 60ms 停脉冲 */
        else                    LLC_PWM_ReleaseBreak();
    } else {
        s_burst_cnt = 0;
        LLC_PWM_BurstDisable();
    }
}
