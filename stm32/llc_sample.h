/**
 * llc_sample.h — ADC 采样链对外接口
 */
#ifndef LLC_SAMPLE_H
#define LLC_SAMPLE_H

#include "stm32f4xx_hal.h"
#include "llc_control.h"

HAL_StatusTypeDef LLC_Sample_Init(void);
/* 轮询 DMA 乒乓标志: 返回0=无新数据; 1=前半块; 2=后半块(读后自动清除) */
uint8_t           LLC_Sample_Poll(void);
/* 取对应半区的 3 通道原始码指针(Vout/Ires/Vin) */
const uint16_t   *LLC_Sample_Block(uint8_t half);
/* 处理一个开关周期的 3 通道原始码, 内部完成
 * 归一化->标定->IIR滤波->峰值保持 */
void              LLC_Sample_OnAdcBlock(const uint16_t *raw3);
/* 取最近一次处理好的样本并复位峰值保持器(电压环节拍调用) */
void              LLC_Sample_Get(LlcSample *out);
/* 最近一次谐振电流包络幅值(A), 供 SR 轻载(DCM)检测 */
float             LLC_Sample_IresEnvelope(void);

#endif /* LLC_SAMPLE_H */
