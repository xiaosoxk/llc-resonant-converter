/**
 * llc_pwm.h — TIM1 六路互补 PWM(PFM 变频) 对外接口
 */
#ifndef LLC_PWM_H
#define LLC_PWM_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

HAL_StatusTypeDef LLC_PWM_Init(void);
/* 设置开关频率(Hz): 同步更新 ARR 与各通道 CCR(预装载, 更新事件生效) */
void              LLC_PWM_SetFrequency(float f_hz);
/* 紧急封锁六路输出(硬件BKIN自动触发时无需调用); 解锁恢复 */
void              LLC_PWM_Break(void);
void              LLC_PWM_ReleaseBreak(void);
/* 轻载 burst: 周期性使能/禁止输出(占空比门控), on_us/off_us 为窗口 */
void              LLC_PWM_BurstWindow(uint16_t on_cycles, uint16_t off_cycles);
void              LLC_PWM_BurstDisable(void);
/* 当前频率(供遥测) */
float             LLC_PWM_GetFrequency(void);

#endif /* LLC_PWM_H */
