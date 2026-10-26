/**
 * llc_sr.h — 副边同步整流时序对外接口
 */
#ifndef LLC_SR_H
#define LLC_SR_H

/* 每个电压环节拍调用: 依据当前频率与谐振电流包络整定 SR 脉宽
 *  (跟随式 SR: 两边沿相对原边脉冲向内收缩 margin, 轻载自动加深) */
void    LLC_SR_Update(float f_hz, float ires_env, uint8_t running);
/* 当前 SR 收缩裕量(ns), 供遥测/调试 */
float   LLC_SR_GetMarginNs(void);

#endif /* LLC_SR_H */
