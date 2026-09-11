#!/usr/bin/env python3
"""check_log.py —— 给一个 .stglog 体检：它是不是一份能用的观察/动作日志。

接一个新后端（新作品、新抽取器）之后第一件该跑的事。判据都挑「错了就一定看得出来」的：
帧号连不连续、自机在不在可动区里、一帧能不能瞬移、人类帧有没有真的录到按键。

    python3 tools/check_log.py <文件.stglog>
"""
import sys

import numpy as np

from stgagent import read_log, ACT_HUMAN, ACT_PASSTHROUGH
from stgagent.fixed import fx_to_float


def main(path):
    hello, frames = read_log(path)
    if not frames:
        print("❌ 一帧都没有——游戏没进关卡，或日志刚建就退出了")
        return 1

    print(f"backend={hello.backend} policy={hello.policy} obs_timing={hello.obs_timing} "
          f"tick_hz={hello.tick_hz}")
    print(f"可动区 {hello.move_area}")
    print(f"表 {[t.name for t in hello.tables.values()]}")
    print(f"帧数 {len(frames)}")

    fails, warns = [], []

    # ---- 帧号 ----
    fr = np.array([f.obs.frame for f in frames], dtype=np.int64)
    d = np.diff(fr)
    if len(d) and not (d == 1).all():
        bad = int((d != 1).sum())
        fails.append(f"帧号不连续：{bad} 处断档，最大跳 {int(d.max())}（掉帧或编码器拒过帧）")

    # ---- 自机 ----
    xs = np.array([fx_to_float(int(f.obs.tables["player"]["x"][0])) for f in frames])
    ys = np.array([fx_to_float(int(f.obs.tables["player"]["y"][0])) for f in frames])
    a = hello.move_area
    if a:
        oob = int(((xs < a["xmin"] - 1) | (xs > a["xmax"] + 1) |
                   (ys < a["ymin"] - 1) | (ys > a["ymax"] + 1)).sum())
        if oob:
            fails.append(f"自机跑出可动区 {oob} 帧（坐标系或平移量错了）")
    # 一帧最大位移：正交速度的上界，留一点余量
    spd = np.array([fx_to_float(int(f.obs.tables["player"]["speed"][0])) for f in frames])
    cap = float(max(spd.max(), 1.0)) * 1.5 + 1.0
    step = np.hypot(np.diff(xs), np.diff(ys))
    if len(step) and step.max() > cap:
        fails.append(f"自机一帧瞬移 {step.max():.1f}px（上限约 {cap:.1f}）——多半是坐标读错了字段")

    # ---- 动作 ----
    human = [f for f in frames if f.act and f.act[2] & ACT_HUMAN]
    thru = [f for f in frames if f.act and f.act[2] & ACT_PASSTHROUGH]
    noact = [f for f in frames if f.act is None]
    if noact:
        warns.append(f"{len(noact)} 帧只有观察没有动作（日志被截断在半帧？）")
    if human and not any(f.act[1] for f in human):
        fails.append(f"{len(human)} 个人类帧的按键全是 0——反映射没接对")

    # ---- 各表规模 ----
    for name in ("bullets", "enemies", "lasers"):
        if name in frames[0].obs.tables:
            n = [len(f.obs.tables[name]) for f in frames]
            print(f"{name}: 最多 {max(n)}，均值 {sum(n) / len(n):.1f}")
    print(f"人类帧 {len(human)}，直通帧 {len(thru)}，算法帧 {len(frames) - len(human) - len(thru)}")

    for w in warns:
        print(f"⚠️  {w}")
    for f in fails:
        print(f"❌ {f}")
    if not fails:
        print("✅ 体检通过")
    return 1 if fails else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
