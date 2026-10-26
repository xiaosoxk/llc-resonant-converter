/**
 * main.c — 全桥LLC 数字控制主程序（超级循环 + 分层节拍）
 *
 * 调度分层:
 *   每开关周期(10~20us): DMA 半区回调 -> 样本处理(标定/IIR/峰值)      [硬件节拍]
 *   每 LLC_LOOP_DIV 周期(~40us): 电压外环 PI -> PFM 变频               [控制节拍]
 *   每 1ms: 保护状态机 / SR 时序自适应 / burst / 软启动基准            [管理节拍]
 *   每 100ms: 串口遥测(与谐波项目同一 AA55 帧协议家族, TYPE=0x10)      [观测节拍]
 * 上电状态机: STANDBY(等母线) -> RUN(软启动->闭环) -> FAULT_RETRY/LATCH
 *
 * !! 安全提醒(必读) !!
 *   母线 400V, 母线电容储能致命; 首次上电必须串 100W 白炽灯限流,
 *   断电后用放电电阻放电并用万用表确认 <36V 才允许接触。
 */
#include "stm32f4xx_hal.h"
#include "llc_config.h"
#include "llc_pwm.h"
#include "llc_sample.h"
#include "llc_control.h"
#include "llc_sr.h"
#include "llc_protect.h"

extern void SystemClock_Config_168MHz(void);   /* CubeMX 生成(略) */

static uint8_t  s_blk_flag_prev = 0;
static uint32_t s_loop_cnt = 0;        /* 开关周期计数 */
static uint32_t s_tick1ms  = 0;
static uint32_t s_tick100ms = 0;
static LlcSample s_smp;
static float    s_fcmd = LLC_F_MAX_HZ;
static float    s_ntc_c = 25.0f;       /* NTC 换算(略): 演示占位 */

int main(void)
{
    HAL_Init();
    SystemClock_Config_168MHz();

    LLC_PWM_Init();
    LLC_Sample_Init();
    LLC_Control_Init();
    LLC_Protect_Init();

    while (1) {
        /* ---- 硬件节拍: DMA 乒乓半区数据就绪 ---- */
        uint8_t half = LLC_Sample_Poll();
        if (half) {
            LLC_Sample_OnAdcBlock(LLC_Sample_Block(half));
            if (++s_loop_cnt % LLC_LOOP_DIV == 0U) {
                LLC_Sample_Get(&s_smp);
                uint8_t running = (LLC_Protect_GetState() == LLC_ST_RUN);
                s_fcmd = LLC_Control_Update(&s_smp, running);
            }
        }

        /* ---- 管理节拍: 1ms ---- */
        uint32_t now = HAL_GetTick();
        if (now != s_tick1ms) {
            s_tick1ms = now;
            LlcState st = LLC_Protect_Tick(s_smp.vout, s_smp.vin,
                                           s_smp.ires_pk, s_ntc_c);
            LLC_SR_Update(s_fcmd, LLC_Sample_IresEnvelope(),
                          st == LLC_ST_RUN);
            LLC_Control_BurstTick(s_smp.vout, s_fcmd);

            /* ---- 观测节拍: 100ms 遥测(帧族与谐波项目一致) ---- */
            if (now - s_tick100ms >= 100U) {
                s_tick100ms = now;
                llc_telemetry_send(&s_smp, s_fcmd, LLC_Protect_GetState());
            }
        }
    }
}

/* ---- 说明性辅助(接线与帧族细节见 README) ----
 * 1) s_raw_block(half): 返回 DMA 乒乓缓冲对应半区指针;
 * 2) g_llc_blk_flag 由 HAL_ADC_ConvHalf/CpltCallback 置位;
 * 3) llc_telemetry_send: 复用 AA55|TYPE|LEN|SEQ|PAYLOAD|CRC16 帧格式,
 *    TYPE=0x10, 载荷 = {float vout, ires_pk, vin, fcmd, u8 state, u8 retry}。
 * 4) NTC 换算: V_ntc -> R_ntc -> 温度(查表或 B 值公式), 此处占位 25C。 */
