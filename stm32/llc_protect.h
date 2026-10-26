/**
 * llc_protect.h — 分级保护对外接口
 */
#ifndef LLC_PROTECT_H
#define LLC_PROTECT_H

#include "stm32f4xx_hal.h"

typedef enum {
    LLC_ST_STANDBY = 0,   /* 等待母线/使能 */
    LLC_ST_RUN,           /* 闭环运行 */
    LLC_ST_FAULT_RETRY,   /* 故障冷却, 准备重试 */
    LLC_ST_FAULT_LATCH,   /* 重试耗尽, 锁死(断电复位) */
} LlcState;

void      LLC_Protect_Init(void);
/* 1ms 调用: 检查 OVP/UVLO/OTP, 管理重试状态机, 返回当前状态 */
LlcState  LLC_Protect_Tick(float vout, float vin, float ires_pk, float ntc_c);
/* 硬件 OCP(BKIN 中断) 落点: 立即锁存故障 */
void      LLC_Protect_OnHardwareOCP(void);
LlcState  LLC_Protect_GetState(void);
uint8_t   LLC_Protect_FaultCode(void);

#endif /* LLC_PROTECT_H */
