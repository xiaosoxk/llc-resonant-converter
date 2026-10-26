/**
 * llc_sample.c — ADC 采样链: 每开关周期一次, TRGO 中点触发 + DMA 双缓冲 + IIR
 *
 * 采样点选择: TRGO = TIM1 OC4REF(计数=ARR/4), 距两个开关沿各约1/4周期,
 *   避开开关瞬间的振铃与地弹 —— 采样时刻是"看得见的EMC对策"(答辩素材)。
 * i_res 经电流互感器+整流取样为包络幅值(CT 不传直流): 每周期峰值用于
 *   逐周期限流与 OCP, 带符号方向在 SR 模块中由半周期计数与 DCM 检测重建。
 * 一阶 IIR: y += (x-y)*0.25, 等效带宽约为采样率的 4%, 压振铃不拖环路。
 * DMA: ADC1 -> DMA2_Stream4(Ch0), 循环模式, 半字, 缓冲 2x3 乒乓。
 */
#include "llc_sample.h"
#include "llc_config.h"

ADC_HandleTypeDef hadc1;
static DMA_HandleTypeDef s_hdma;

static uint16_t s_raw[6];                 /* [3通道]x2 乒乓 */
static volatile uint8_t s_blk_flag = 0;   /* 1=前半块就绪 2=后半块就绪 */
static LlcSample s_flt;
static float     s_ires_env = 0.0f;

#define IIR_A   0.25f

static float vout_of_raw(uint16_t r)
{
    float v = ((float)r / LLC_ADC_FS * LLC_VREF_V - LLC_VOUT_OFFSET) * LLC_VOUT_GAIN;
    return (v < 0.0f) ? 0.0f : v;
}

HAL_StatusTypeDef LLC_Sample_Init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;   /* PA5 Vout / PA6 Ires / PA7 Vin */
    g.Mode = GPIO_MODE_ANALOG; g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = ENABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_RISING;
    hadc1.Init.ExternalTrigConv      = ADC_EXTERNALTRIGCONV_T1_TRGO;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 3;
    hadc1.Init.DMAContinuousRequests = ENABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SEQ_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) return HAL_ERROR;

    ADC_ChannelConfTypeDef ch = {0};
    ch.SamplingTime = ADC_SAMPLETIME_15CYCLES;
    ch.Channel = ADC_CHANNEL_5; ch.Rank = 1; HAL_ADC_ConfigChannel(&hadc1, &ch);
    ch.Channel = ADC_CHANNEL_6; ch.Rank = 2; HAL_ADC_ConfigChannel(&hadc1, &ch);
    ch.Channel = ADC_CHANNEL_7; ch.Rank = 3; HAL_ADC_ConfigChannel(&hadc1, &ch);

    s_hdma.Instance                 = DMA2_Stream4;
    s_hdma.Init.Channel             = DMA_CHANNEL_0;
    s_hdma.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    s_hdma.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_hdma.Init.MemInc              = DMA_MINC_ENABLE;
    s_hdma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    s_hdma.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    s_hdma.Init.Mode                = DMA_CIRCULAR;
    s_hdma.Init.Priority            = DMA_PRIORITY_HIGH;
    s_hdma.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&s_hdma) != HAL_OK) return HAL_ERROR;
    __HAL_LINKDMA(&hadc1, DMA_Handle, s_hdma);

    HAL_NVIC_SetPriority(DMA2_Stream4_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream4_IRQn);
    return HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_raw, 6U);
}

void LLC_Sample_OnAdcBlock(const uint16_t *raw3)
{
    /* 电压: IIR; 电流: 峰值保持(电压环节拍取走后清零); 母线: 直读 */
    float v = vout_of_raw(raw3[0]);
    s_flt.vout += (v - s_flt.vout) * IIR_A;

    float ia = ((float)raw3[1] - LLC_IRES_OFFSET) * LLC_VREF_V / LLC_ADC_FS * LLC_IRES_GAIN;
    if (ia < 0.0f) ia = -ia;
    s_ires_env = ia;
    if (ia > s_flt.ires_pk) s_flt.ires_pk = ia;

    s_flt.vin = (float)raw3[2] / LLC_ADC_FS * LLC_VREF_V * LLC_VIN_GAIN;
}

void LLC_Sample_Get(LlcSample *out)
{
    *out = s_flt;
    s_flt.ires_pk = 0.0f;                    /* 峰值保持器复位 */
}

float LLC_Sample_IresEnvelope(void) { return s_ires_env; }

/* ---- DMA 回调: 半传输=前半块, 全传输=后半块 ---- */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *h) { (void)h; s_blk_flag = 1; }
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h)     { (void)h; s_blk_flag = 2; }

uint8_t LLC_Sample_Poll(void)
{
    uint8_t f = s_blk_flag;
    s_blk_flag = 0;
    return f;
}

const uint16_t *LLC_Sample_Block(uint8_t half)
{
    return (half == 1U) ? &s_raw[0] : &s_raw[3];
}

void DMA2_Stream4_IRQHandler(void) { HAL_DMA_IRQHandler(&s_hdma); }
