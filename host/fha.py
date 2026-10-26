# -*- coding: utf-8 -*-
"""
fha.py — LLC 谐振腔 FHA(基波近似) 增益与参数设计计算器

公式(FHA 一阶近似):
  fn = fs/fr,  k = Lm/Lr,  Q = Zr/Rac,  Zr = sqrt(Lr/Cr)
  Rac = (8/π²)·n²·R_L                    (副边负载折算到原边的等效交流电阻)
  M(fn) = 1 / sqrt( (1 + 1/k - 1/(k·fn²))² + Q²·(fn - 1/fn)² )
关键性质(测试断言的依据):
  M(1) = 1(谐振点增益与负载无关);
  有限 Q 下 fn→∞ 时 M→0(串联 Lr 主导);
  Q→0(空载)时曲线趋于水平渐近线 M = k/(k+1) —— 这正是轻载调压困难、
  需要 burst 模式的理论根源;
  峰值增益点在 fn<1 处, Q 越大峰值越低越靠右; 峰值左侧为容性区(丧失ZVS)。
用法:
  python fha.py --po 300 --vout 48 --vin 400 --n 8 --fr 100e3 --k 6 --q 0.45
  python fha.py --plot out.png            # 生成增益曲线族
"""
from __future__ import annotations
import argparse, math


def gain_m(fn: float, k: float, q: float) -> float:
    if fn <= 0:
        raise ValueError("fn must be > 0")
    a = 1.0 + 1.0 / k - 1.0 / (k * fn * fn)
    b = q * (fn - 1.0 / fn)
    return 1.0 / math.sqrt(a * a + b * b)


def rac_of(n: float, r_load: float) -> float:
    """副边负载折算到原边的 FHA 等效交流电阻"""
    return 8.0 / (math.pi ** 2) * n * n * r_load


def design(po: float, vout: float, vin_nom: float, n: float,
           fr: float, k: float, q: float) -> dict:
    """由 k/Q/fr 反解谐振腔参数(与论文第3章设计流程一致)"""
    r_load = vout * vout / po
    rac = rac_of(n, r_load)
    zr = q * rac
    lr = zr / (2.0 * math.pi * fr)
    cr = 1.0 / ((2.0 * math.pi * fr) ** 2 * lr)
    lm = k * lr
    return dict(r_load=r_load, rac=rac, zr=zr, lr=lr, cr=cr, lm=lm,
                n_nom_gain=vin_nom / vout,
                lm_pk_current=vin_nom / (4.0 * lm * fr))  # 励磁峰值


def fn_peak(k: float, q: float) -> float:
    """数值法求峰值增益点(fn<1), 用于确认 fmin 不落入容性区"""
    lo, hi = 1e-3, 1.0
    for _ in range(80):                     # 三分搜索(单峰)
        m1 = lo + (hi - lo) / 3.0
        m2 = hi - (hi - lo) / 3.0
        if gain_m(m1, k, q) < gain_m(m2, k, q):
            lo = m1
        else:
            hi = m2
    return (lo + hi) / 2.0


def main():
    ap = argparse.ArgumentParser(description="LLC FHA calculator")
    ap.add_argument("--po", type=float, default=300.0)
    ap.add_argument("--vout", type=float, default=48.0)
    ap.add_argument("--vin", type=float, default=400.0)
    ap.add_argument("--n", type=float, default=8.0)
    ap.add_argument("--fr", type=float, default=100e3)
    ap.add_argument("--k", type=float, default=6.0)
    ap.add_argument("--q", type=float, default=0.45)
    ap.add_argument("--plot", type=str, default=None, help="输出增益曲线 PNG")
    args = ap.parse_args()

    d = design(args.po, args.vout, args.vin, args.n, args.fr, args.k, args.q)
    fp = fn_peak(args.k, args.q)
    print("== LLC 谐振腔设计(设计目标, 以实际样机校准为准) ==")
    print("R_load = %.2f ohm,  Rac = %.1f ohm,  Zr = %.1f ohm" %
          (d["r_load"], d["rac"], d["zr"]))
    print("Lr = %.1f uH,  Cr = %.2f nF,  Lm = k*Lr = %.2f mH" %
          (d["lr"] * 1e6, d["cr"] * 1e9, d["lm"] * 1e3))
    print("励磁电流峰值 Ip_Lm = Vin/(4*Lm*fr) = %.3f A(原边)" % d["lm_pk_current"])
    print("峰值增益点 fn_peak = %.3f (M=%.2f) -> fmin 必须高于 %.1f kHz" %
          (fp, gain_m(fp, args.k, args.q), fp * args.fr / 1000.0))
    print("增益@fn: " + "  ".join(
        "M(%.2f)=%.3f" % (fn, gain_m(fn, args.k, args.q))
        for fn in (0.6, 0.8, 1.0, 1.2, 1.5)))

    if args.plot:
        import numpy as np
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fn = np.linspace(0.3, 2.0, 800)
        for q, c in zip((0.25, 0.45, 0.7, 1.0),
                        ("#6FA3AD", "#1B6B7A", "#C49B72", "#B0433B")):
            m = [gain_m(x, args.k, q) for x in fn]
            plt.plot(fn, m, color=c, label="Q=%.2f" % q)
        plt.grid(alpha=0.3); plt.legend(prop=FontPropertiesCN())
        plt.xlabel("fn = fs/fr"); plt.ylabel("M")
        plt.savefig(args.plot, dpi=200, bbox_inches="tight")
        print("plot ->", args.plot)


def FontPropertiesCN():
    """中文图例(有 SimHei 用 SimHei, 无则默认)"""
    from matplotlib.font_manager import FontProperties
    try:
        return FontProperties(fname=r"C:\Windows\Fonts\simhei.ttf")
    except Exception:
        return {"fontsize": 9}


if __name__ == "__main__":
    main()
