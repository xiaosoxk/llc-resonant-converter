# 数字控制全桥LLC谐振DC-DC变换器


## 目录结构
```
code/llc/
  stm32/            STM32F407 数字控制固件(HAL)
    llc_config.h      全局参数(谐振点/限幅/保护阈值/标定)
    llc_pwm.c/.h      TIM1 六路互补 PWM: 全桥两臂(CH2极性翻转技巧)+SR(CH3/3N)
    llc_control.c/.h  电压外环 PI(抗饱和) + 软启动 + 轻载 burst
    llc_sample.c/.h   ADC 每开关周期采样(TRGO 中点触发, DMA 双缓冲)
    llc_sr.c/.h       同步整流时序(两边向内收缩 + 轻载自适应裕量)
    llc_protect.c/.h  分级保护: BKIN硬件逐周期 + 软件锁存 + 受控重试
    main.c            状态机 STANDBY/RUN/FAULT + 分层节拍
  host/             设计与验证工具
    fha.py            FHA 增益/参数设计计算器(CLI, 可出图)
    llc_smoke_test.py 公式回归测试(M(1)=1/渐近线/参数自洽/峰值点)
```

## 快速开始
```bash
# 1) 谐振腔参数设计(先于硬件!)
cd code/llc/host
python fha.py --po 300 --vout 48 --vin 400 --n 8 --fr 100e3 --k 6 --q 0.45
python llc_smoke_test.py

# 2) LTspice/PLECS 仿真: 用 fha.py 输出的 Lr/Cr/Lm 搭开环模型,
#    验证增益曲线与 FHA 误差(见论文第6章), 再做扫频/阶跃

# 3) 固件: CubeMX 生成 F407 工程(仅时钟), 加入 stm32/*.c/h
#    先开环定频验证驱动/死区, 再闭环(每一步见 README 安全规程)

# !! 安全: 母线 400V 储能致命, 串灯泡上电, 断电放电并验证 <36V !!
```

## 调试安全规程(不可跳过)
1. 上电顺序: 串灯泡 -> 辅助电 -> 确认 3.3V/驱动波形 -> 母线缓升;
2. 开环定频 fr 测试用 30V 低压母线起步(增益特性不变, 风险降级);
3. 示波器测母线/桥臂必须差分探头或隔离通道;
4. 任何改参数后先跑 fha.py 复核增益区间, 再上电。
