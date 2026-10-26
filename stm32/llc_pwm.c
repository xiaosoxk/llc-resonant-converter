/**
 * llc_pwm.c — TIM1 六路互补 PWM: 全桥两桥臂 + 副边同步整流, PFM 变频
 *
 * 接线/通道规划(全桥对角导通):
 *   桥臂A: Q1=CH1(PA8),  Q2=CH1N(PA7? 实际PB13/PA12按板定)  -> 高/低侧
 *   桥臂B: Q3=CH2,       Q4=CH2N                            -> 高/低侧
 *   副边:  SR1=CH3,      SR2=CH3N                           -> 两颗同步整流MOS
 * 桥臂B反相技巧: CH2/CH2N 输出极性整体取反(CC2P+CC2NP), 使 CH2=NOT(CH1),
 *   CH2N=CH1 -> Q1&Q4 与 Q2&Q3 对角成对导通, 全桥只需一个高级定时器。
 *   (实务替代: 主从双定时器, 可独立调死区; 单定时器方案死区两臂共用。)
 * 中心对齐模式: 开关沿位于计数器峰/谷, 采样触发在半周期中点 -> 避开关噪声。
 * PFM 实现: 变频即改 ARR(预装载), CCR 同步改为 (ARR+1)/2 保持50%占空;
 *   分辨率 = f/(TIM_CLK) -> 100kHz时约 0.06%, F407 无 HRPWM(C2000有),
 *   高分辨率需求见论文选型对比一节。
 * 保护: PB12 = TIM1_BKIN(外部比较器OCP), 制动即六路进入安全态(OSSI)。
 */
#include "llc_pwm.h"
#include "llc_config.h"

static TIM_HandleTypeDef s_htim1;
static float s_f_now = LLC_F_MAX_HZ;    /* 上电从最高频起(增益最低, 最安全) */

/* DTG 编码: CKD=DIV1, t_DTS=5.95ns, 死区=DTG[5:0]*t_DTS
 * 注意: 中心对齐模式计数器一升一降为一个开关周期 -> 周期 = 2*ARR*t_clk */
static uint32_t arr_for_freq(float f)
{
    uint32_t arr = (uint32_t)((float)LLC_TIM_CLK_HZ / (2.0f * f) + 0.5f) - 1U;
    if (arr < 560U)  arr = 560U;                  /* fmax 上限保护 */
    if (arr > 1680U) arr = 1680U;                 /* fmin 下限保护 */
    return arr;
}

HAL_StatusTypeDef LLC_PWM_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PWM 输出脚: PA8=CH1, PA9=CH2, PA10=CH3, PB13=CH1N, PB14=CH2N, PB15=CH3N */
    g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_PULLDOWN; g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF1_TIM1;
    g.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &g);
    /* BKIN 输入脚 PB12: 外部OCP比较器, 低有效 */
    g.Pin = GPIO_PIN_12; g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_PULLUP;
    g.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOB, &g);

    s_htim1.Instance               = TIM1;
    s_htim1.Init.Prescaler         = 0;
    s_htim1.Init.CounterMode       = TIM_COUNTERMODE_CENTERALIGNED1;
    s_htim1.Init.Period            = arr_for_freq(s_f_now);
    s_htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_htim1.Init.RepetitionCounter = 0;
    s_htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&s_htim1) != HAL_OK) return HAL_ERROR;

    /* 三通道同为50%宽; CH2极性翻转实现桥臂B反相 */
    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = (s_htim1.Init.Period + 1U) / 2U;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&s_htim1, &oc, TIM_CHANNEL_1) != HAL_OK) return HAL_ERROR;
    oc.OCPolarity  = TIM_OCPOLARITY_LOW;    /* CH2 取反 */
    oc.OCNPolarity = TIM_OCNPOLARITY_LOW;   /* CH2N 取反 */
    if (HAL_TIM_PWM_ConfigChannel(&s_htim1, &oc, TIM_CHANNEL_2) != HAL_OK) return HAL_ERROR;
    oc.OCPolarity  = TIM_OCPOLARITY_HIGH;   /* CH3 回到正常(副边SR) */
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    if (HAL_TIM_PWM_ConfigChannel(&s_htim1, &oc, TIM_CHANNEL_3) != HAL_OK) return HAL_ERROR;

    /* 死区(两臂共用) + 刹车: BKIN低有效, 滤波8, 主输出使能, 空闲安全态 */
    TIM_BreakDeadTimeConfigTypeDef bd = {0};
    bd.DeadTime       = LLC_DEADTIME_DTG;
    bd.BreakState     = TIM_BREAK_ENABLE;
    bd.BreakPolarity  = TIM_BREAKPOLARITY_LOW;
    bd.BreakFilter    = 8;
    bd.OffStateRunMode  = TIM_OSSR_ENABLE;   /* 运行中封锁: 输出按安全态 */
    bd.OffStateIDLEMode = TIM_OSSI_ENABLE;   /* 空闲: 主输出保持安全态 */
    bd.LockLevel        = TIM_LOCKLEVEL_OFF;
    bd.AutomaticOutput  = TIM_AUTOMATICOUTPUT_DISABLE; /* 手动MOE, 便于软启 */
    if (HAL_TIMEx_ConfigBreakDeadTime(&s_htim1, &bd) != HAL_OK) return HAL_ERROR;

    /* TRGO = OC4REF: CH4只做比较不出脚, 布点在半周期中点 -> ADC采样避开开关沿 */
    TIM_OC_InitTypeDef oc4 = {0};
    oc4.OCMode = TIM_OCMODE_PWM1;
    oc4.Pulse  = (s_htim1.Init.Period + 1U) / 4U;   /* CNT=ARR/4 处触发 */
    oc4.OCPolarity = TIM_OCPOLARITY_HIGH;
    HAL_TIM_PWM_ConfigChannel(&s_htim1, &oc4, TIM_CHANNEL_4);

    TIM_MasterConfigTypeDef mst = {0};
    mst.MasterOutputTrigger = TIM_TRGO_OC4REF;
    mst.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&s_htim1, &mst);

    HAL_NVIC_SetPriority(TIM1_BRK_TIM9_IRQn, 2, 0);  /* 制动中断: 记录故障 */
    HAL_NVIC_EnableIRQ(TIM1_BRK_TIM9_IRQn);
    return HAL_OK;
}

void LLC_PWM_SetFrequency(float f_hz)
{
    if (f_hz > LLC_F_MAX_HZ) f_hz = LLC_F_MAX_HZ;
    if (f_hz < LLC_F_MIN_HZ) f_hz = LLC_F_MIN_HZ;
    uint32_t arr = arr_for_freq(f_hz);
    __HAL_TIM_SET_AUTORELOAD(&s_htim1, arr);
    /* 三通道同步收窄/展开, 保持50%与SR时序比例 */
    uint32_t ccr12 = (arr + 1U) / 2U;
    __HAL_TIM_SET_COMPARE(&s_htim1, TIM_CHANNEL_1, ccr12);
    __HAL_TIM_SET_COMPARE(&s_htim1, TIM_CHANNEL_2, ccr12);
    __HAL_TIM_SET_COMPARE(&s_htim1, TIM_CHANNEL_3, ccr12);   /* SR宽度由SR模块细调 */
    __HAL_TIM_SET_COMPARE(&s_htim1, TIM_CHANNEL_4, (arr + 1U) / 4U);
    s_f_now = f_hz;
}
void LLC_PWM_Break(void)         { __HAL_TIM_MOE_DISABLE(&s_htim1); }
void LLC_PWM_ReleaseBreak(void)  { __HAL_TIM_MOE_ENABLE(&s_htim1); }

/* 轻载 burst: v1 基础实现 = 软件按窗口开关主输出; 升级路径: 用TIM1 CH5? 不存在,
 * 改用第二定时器门控 MOE 或 RC 阻塞振荡; 数字实现思路一致 */
void LLC_PWM_BurstWindow(uint16_t on_cycles, uint16_t off_cycles)
{
    static uint16_t cnt = 0;
    cnt++;
    if (cnt < on_cycles)            { if (!(__HAL_TIM_IS_TIM_COUNTING_ENABLED(&s_htim1))) LLC_PWM_ReleaseBreak(); }
    else if (cnt < on_cycles + off_cycles) { LLC_PWM_Break(); }
    else cnt = 0;
}

void LLC_PWM_BurstDisable(void)
{
    if (!(__HAL_TIM_IS_TIM_COUNTING_ENABLED(&s_htim1))) LLC_PWM_ReleaseBreak();
}

float LLC_PWM_GetFrequency(void) { return s_f_now; }

/* 制动中断: BKIN 触发(硬件OCP), 记录并交由软件故障状态机处理 */
void TIM1_BRK_TIM9_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&s_htim1, TIM_FLAG_BREAK)) {
        __HAL_TIM_CLEAR_FLAG(&s_htim1, TIM_FLAG_BREAK);
        extern void LLC_Protect_OnHardwareOCP(void);
        LLC_Protect_OnHardwareOCP();
    }
}
