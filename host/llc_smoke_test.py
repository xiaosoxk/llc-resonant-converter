# -*- coding: utf-8 -*-
"""llc_smoke_test.py — FHA 计算器回归测试(无硬件可跑)"""
import sys, os, math
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fha import gain_m, rac_of, design, fn_peak

# T1: 谐振点增益恒为 1(与 k/Q/负载无关)
for k in (3.0, 6.0, 10.0):
    for q in (0.2, 0.45, 1.0):
        assert abs(gain_m(1.0, k, q) - 1.0) < 1e-9, "M(1) != 1"

# T2: 渐近线 —— 有限 Q 时高频增益 -> 0(Lr 主导); 空载(Q->0)渐近 k/(k+1)
for k in (3.0, 6.0, 10.0):
    assert gain_m(50.0, k, 0.45) < 0.1, "finite-Q high-fn should decay, k=%s" % k
    m_nl = gain_m(20.0, k, 0.001)                 # 空载曲线
    assert abs(m_nl - k / (k + 1.0)) < 5e-3, "no-load asymptote mismatch k=%s" % k

# T3: 参数设计自洽: sqrt(Lr/Cr) == Q*Rac, Lm == k*Lr
d = design(po=300, vout=48, vin_nom=400, n=8, fr=100e3, k=6.0, q=0.45)
assert abs(math.sqrt(d["lr"] / d["cr"]) - d["zr"]) / d["zr"] < 1e-9
assert abs(d["lm"] - 6.0 * d["lr"]) / d["lm"] < 1e-9
# 文档第3章引用的设计示例值(自检: 公式与文字一致)
assert 270e-6 < d["lr"] < 300e-6, "Lr=%.1fuH 超出设计示例区间" % (d["lr"] * 1e6)
assert 8.0e-9 < d["cr"] < 10.0e-9, "Cr=%.2fnF 超出设计示例区间" % (d["cr"] * 1e9)

# T4: 峰值点在 fn<1 且峰值增益>1(Q 合理时); Q 越大峰值越低
fp1 = fn_peak(6.0, 0.45)
assert fp1 < 1.0 and gain_m(fp1, 6.0, 0.45) > 1.0
assert fn_peak(6.0, 1.0) > fn_peak(6.0, 0.45)          # Q大 -> 峰右移
assert gain_m(fn_peak(6.0, 1.0), 6.0, 1.0) < gain_m(fp1, 6.0, 0.45)  # Q大 -> 峰值低

# T5: 励磁峰值电流与手算一致: Vin/(4*Lm*fr) = 400/(4*1.711m*100k) ≈ 0.584A
assert abs(d["lm_pk_current"] - 0.584) < 0.01

print("ALL FHA SMOKE TESTS PASSED")
print("设计示例: Lr=%.1fuH Cr=%.2fnF Lm=%.2fmH Rac=%.0fZr=%.1f Ip_Lm=%.3fA fn_peak=%.3f"
      % (d["lr"] * 1e6, d["cr"] * 1e9, d["lm"] * 1e3, d["rac"], d["zr"],
         d["lm_pk_current"], fp1))
