/**
 * llc_control.h — 电压外环 PI(抗饱和) + 软启动 + 轻载 burst 状态机
 */
#ifndef LLC_CONTROL_H
#define LLC_CONTROL_H

#include "stm32f4xx_hal.h"

typedef struct {
    float vout;        /* 滤波后输出电压 */
    float ires_pk;     /* 本半周期谐振电流峰值 */
    float vin;
} LlcSample;

void  LLC_Control_Init(void);
/* 电压环: 每LLC_LOOP_DIV个开关周期调用一次(约25kHz), 返回新频率指令 */
float LLC_Control_Update(const LlcSample *s, uint8_t running);
/* 软启动基准复位(故障重试时调用) */
void  LLC_Control_ResetSoftStart(void);
/* 轻载 burst 状态机(1ms 调用) */
void  LLC_Control_BurstTick(float vout, float fcmd);

#endif /* LLC_CONTROL_H */
