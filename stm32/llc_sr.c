/**
 * llc_sr.c — 副边同步整流时序(学习型实现)
 *
 * 通道规划: SR1=TIM1_CH3, SR2=CH3N(互补), 中心对齐模式下 CCR3 决定脉冲宽度,
 *   脉冲中心与原边半周期中心对齐 —— 收窄 CCR3 即同时"延迟开通+提前关断"。
 *
 * 为什么必须两边向内收(核心答辩点):
 *   1) 开通提前 -> 副边电流尚未建立反向, SR 与原边形成环流(倒灌), 母线抽空;
 *   2) 关断滞后  -> i_sec 已过零反向, SR MOS 体二极管承受反压+损耗陡增。
 * 裕量自适应: 负载越轻, 副边电流越早过零(DCM), 需要的提前关断量越大 ——
 *   用谐振电流包络做慢速自适应(ires_env 低于阈值则 margin 加深)。
 * 升级路径(生产方案): ZCD 型 SR 控制器(如 NCP4306)或体二极管压降检测,
 *   数字侧只保留时序微调 —— 本模块的 margin 机制即该微调的数字版本。
 */
#include "llc_sr.h"
#include "llc_config.h"
#include "llc_pwm.h"
#include "stm32f4xx_hal.h"

#define TIM_CLK_HZ      168000000U
#define SR_MARGIN_MIN_NS 60.0f     /* 下限: 不能贴着换向点 */
#define SR_MARGIN_MAX_NS 900.0f    /* 上限: 过深则 MOS 不再导通, 退回二极管 */
#define SR_DCM_ENV_A     0.8f      /* 包络低于此值判定轻载断续 */
#define SR_ADAPT_A_PER_S 400.0f    /* margin 自适应速率 ns/ms */

static float s_margin_ns = (float)LLC_SR_EDGE_MARGIN_NS;

void LLC_SR_Update(float f_hz, float ires_env, uint8_t running)
{
    if (!running) return;

    /* 轻载自适应: 包络小 -> margin 加深(提前关断更多); 重载回收 */
    float dir = (ires_env < SR_DCM_ENV_A) ? 1.0f : -1.0f;
    s_margin_ns += dir * SR_ADAPT_A_PER_S * 0.001f;   /* 1ms 节拍 */
    if (s_margin_ns > SR_MARGIN_MAX_NS) s_margin_ns = SR_MARGIN_MAX_NS;
    if (s_margin_ns < SR_MARGIN_MIN_NS) s_margin_ns = SR_MARGIN_MIN_NS;

    /* 当前定时器 ARR(由 PFM 模块维护), 反推计数 */
    TIM_HandleTypeDef *ht = (TIM_HandleTypeDef *)TIM1;
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(ht);
    uint32_t margin_cnt = (uint32_t)(s_margin_ns * 1e-9f * (float)TIM_CLK_HZ);
    /* 中心对齐: 脉冲宽度=2*CCR*t_clk; 半周期宽度=(ARR+1)*t_clk
     * SR 宽度 = 半周期 - 2*margin -> CCR3 = (半周期 - 2*margin)/2 */
    int32_t ccr = (int32_t)((arr + 1U) >> 1) - (int32_t)margin_cnt;
    if (ccr < 10) ccr = 10;                    /* 保留最小脉宽, 完全关断走 Break */
    __HAL_TIM_SET_COMPARE(ht, TIM_CHANNEL_3, (uint32_t)ccr);
}

float LLC_SR_GetMarginNs(void) { return s_margin_ns; }
