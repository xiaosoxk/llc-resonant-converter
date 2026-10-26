/**
 * llc_config.h — 全桥LLC 数字控制核心参数（全工程唯一参数源）
 * 规格(设计目标): 母线 360~430V -> 输出 48V/300W, PFM 变频 + 同步整流
 */
#ifndef LLC_CONFIG_H
#define LLC_CONFIG_H

/* ---------------- 功率级与谐振腔(与 fha.py 设计示例一致) ---------------- */
#define LLC_FR_HZ          100000.0f  /* 串联谐振频率 fr */
#define LLC_F_MIN_HZ       55000.0f   /* 下限频率: 必须高于满载峰值增益频率(52.5kHz) */
#define LLC_F_MAX_HZ       150000.0f  /* 上限频率(降压区/轻载) */
#define LLC_K_LM_LR        6.0f       /* k = Lm/Lr */
#define LLC_Q_FULL         0.45f      /* 满载品质因数(设计值) */
#define LLC_VOUT_SET       48.0f      /* 输出设定 V */
#define LLC_VOUT_RIPPLE_MV 480.0f     /* 输出纹波设计值 mVpp */
#define LLC_PO_W           300.0f     /* 额定功率 W */
#define LLC_N_TRANS        8.0f       /* 变压器匝比 n:1 */

/* ---------------- PWM 与死区 ---------------- */
#define LLC_TIM_CLK_HZ     168000000U /* APB2 定时器时钟(168MHz, 不分频) */
#define LLC_DEADTIME_DTG   50U        /* BDTR.DTG 编码: 50 * t_DTS ≈ 298ns */
#define LLC_PWM_CHANNELS   3U         /* CH1/2=原边两桥臂, CH3/3N=副边SR */

/* ---------------- 采样与标定 ---------------- */
#define LLC_VREF_V         3.3f       /* ADC 参考 */
#define LLC_ADC_FS         4095.0f
#define LLC_VOUT_GAIN      33.0f      /* 分压比 x 比例(标定项)  V/V */
#define LLC_VOUT_OFFSET    0.0f       /* 偏置(标定项) */
#define LLC_IRES_GAIN      0.05f      /* CT 变比+负载电阻+调理(A/V) */
#define LLC_IRES_OFFSET    2048.0f    /* CT 中点码值 */
#define LLC_VIN_GAIN       100.0f     /* 母线分压比 */

/* ---------------- 控制环 ---------------- */
#define LLC_LOOP_DIV       4U         /* 每4个开关周期跑一次电压环 */
#define LLC_KP             0.004f     /* 电压环比例(整定起点, 见5.4节) */
#define LLC_KI             0.0002f    /* 电压环积分 */
#define LLC_PI_OUT_MAX     (LLC_FR_HZ - LLC_F_MIN_HZ)  /* 输出=降频深度 */
#define LLC_SS_MS          50U        /* 软启动基准斜坡时间 */
#define LLC_BURST_HYS_V    0.5f       /* 轻载 burst 迟滞 */

/* ---------------- 保护 ---------------- */
#define LLC_VOUT_OVP_V     56.0f      /* 输出过压(锁存) */
#define LLC_IRES_OCP_A     8.0f       /* 谐振电流峰值(硬件BKIN阈值与此一致) */
#define LLC_VIN_UVLO_V     300.0f     /* 母线欠压(禁止/停止) */
#define LLC_NTC_OTP_C      85.0f      /* 变压器/散热器过温 */
#define LLC_FAULT_RETRY    3U         /* 故障自动重试次数, 超过则锁死等断电 */
#define LLC_RETRY_COOLDOWN_MS 500U

/* ---------------- SR 时序 ---------------- */
#define LLC_SR_EDGE_MARGIN_NS  150U   /* SR 两边沿向内收缩裕量(防共导) */
#define LLC_SR_ZCD_GUARD       1U     /* 副边电流过零提前封锁使能 */

#endif /* LLC_CONFIG_H */
