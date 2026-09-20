#!/usr/bin/env python3
"""check_model_parity.py —— `onnx` 策略后端的逐帧对拍闸门。

读一段 DLL 录的 `.stglog`，用同一张 ONNX 图、同一套锚点与「上一步动作」规则在 Python 侧重算
每一帧的动作，与日志里 `ACT.buttons` 逐帧比。**这是 onnx 后端唯一的硬闸门**（设计 §8）。

它抓的是「C 侧接错了」：列序写反、x/y 调包、包络过滤写错边界、动作表错位、`prev_action`
没在该清的时候清 —— 任何一条都会让某些帧的动作对不上。它抓不了「模型本身好不好」，
那靠目验（设计 D7）。

**录制条件**（不满足闸门就没有意义）：

    policy = onnx · safety_net = 0 · BYPASS 模式（鼠标不参与）· `--anchor` = ini 的 bypass_anchor（默认 free）

`.stglog` 里**没有锚点这一路信息**（协议里没有这个字段），所以锚点必须是世界状态的确定性函数，
且这里的 `--anchor` 要与录制时 ini 的 `anchor` 一致 —— 由人保证。`safety_net = 1` 录的日志里
会有额外的 BOMB 位，对不上；鼠标锚点更是无从复现。

用法：

    python3 tools/check_model_parity.py <日志.stglog> --model j-best.onnx [--anchor under_boss]
                                        [--anchor-y 384] [--hysteresis 0] [--eps 1e-3]

退出码 0 = 全帧一致（或不一致都在浮点近平局内）；1 = 有真正的分歧。
"""
import argparse
import sys

import numpy as np

from stgagent import ACT_HUMAN, ACT_PASSTHROUGH, read_log
from stgagent.consts import (
    BTN_BOMB,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_SHOT,
    BTN_SLOW,
    BTN_UP,
    PHASE_PLAYER_CONTROLLABLE,
    PHASE_REPLAY_PLAYBACK,
)
from stgagent.fixed import fx_to_float

# ---- 图签名与口径，逐项对着 c/sa_model.h ----
BULLET_ROWS, BULLET_COLS = 640, 5
ENEMY_ROWS, ENEMY_COLS = 256, 6      # x, y, hit_w, boss, vx, vy（图版本 2）
TELEPORT_PX = 16.0                    # = SA_MODEL_TELEPORT_PX
PLAYER_COLS = 5
NUM_ACTIONS = 18
ENV_HALF_W, ENV_Y_MIN, ENV_Y_MAX = 256.0, -64.0, 512.0
FIELD_HALF_W = 192.0
ANCHOR_MARGIN = 16.0
INPUT_NAMES = ("bullets", "bullets_mask", "enemies", "enemies_mask", "player", "target", "prev_action")
HELD_INPUT = "dir_held"               # 只有图版本 3（手部运动层下练的模型）才有
HELD_NEVER = 1 << 20                  # = SA_MOTOR_HELD_NEVER

BULLET_FLAG_COLLIDABLE = 0x01
ENEMY_FLAG_BOSS = 0x01
ENEMY_FLAG_COLLIDABLE = 0x10

# 动作表 v1，逐项对着 c/sa_model.c 的 SA_DIR_BUTTONS 与训练仓 actions.py
DIR_BUTTONS = (
    0,
    BTN_UP,
    BTN_UP | BTN_RIGHT,
    BTN_RIGHT,
    BTN_DOWN | BTN_RIGHT,
    BTN_DOWN,
    BTN_DOWN | BTN_LEFT,
    BTN_LEFT,
    BTN_UP | BTN_LEFT,
)


def action_buttons(action_id: int) -> int:
    d, slow = divmod(int(action_id), 2)
    return DIR_BUTTONS[d] | BTN_SHOT | (BTN_SLOW if slow else 0)


def buttons_to_action(buttons: int):
    """按钮位 → 动作 id；不是动作表 v1 能产出的组合就返回 None。

    动作表可逆这件事是这个闸门能「每帧独立比」的前提：`prev_action` 从**上一帧日志里的**
    按钮位反推，而不是从重算结果递推 —— 否则一帧分歧会级联污染后面所有帧，报告里全是假分歧。
    BOMB 位忽略（safety_net 开着时会有，它不改方向也不改 prev_action）。
    """
    b = int(buttons) & ~BTN_BOMB
    if not (b & BTN_SHOT):
        return None                      # 动作表恒按 SHOT
    slow = 1 if (b & BTN_SLOW) else 0
    d_bits = b & ~(BTN_SHOT | BTN_SLOW)
    for d, want in enumerate(DIR_BUTTONS):
        if d_bits == want:
            return d * 2 + slow
    return None


def pick(logits, prev_action: int, hysteresis: float) -> int:
    """与 c/sa_model.c 的 sa_model_pick / 训练仓 evaluate.py 的 hysteresis_action 同义。"""
    best = int(np.argmax(logits))
    if hysteresis > 0.0 and 0 <= prev_action < NUM_ACTIONS:
        if logits[best] - logits[prev_action] <= hysteresis:
            return prev_action
    return best


def anchor(obs, mode: str, anchor_x: float, anchor_y: float) -> tuple:
    """与 policy_model.c 的 ap_policy_model_anchor 同义（不含鼠标那一档 —— 无从复现）。"""
    lim = max(0.0, FIELD_HALF_W - ANCHOR_MARGIN)
    if mode == "free":                       # 无目标点：锚点锁自机，不钳、不用 anchor_y
        p = obs.tables["player"][0]
        return float(fx_to_float(p["x"])), float(fx_to_float(p["y"]))
    if mode == "fixed":
        x = anchor_x
    elif mode == "under_boss":
        x = 0.0
        e = obs.tables.get("enemies")
        if e is not None and len(e):
            flags = np.asarray(e["flags"])
            live = ((flags & ENEMY_FLAG_BOSS) != 0) & ((flags & ENEMY_FLAG_COLLIDABLE) != 0)
            idx = np.flatnonzero(live)
            if len(idx):                     # 池序最前那只在场 boss
                x = float(fx_to_float(e["x"][idx[0]]))
    else:                                    # field_bottom
        x = 0.0
    return float(np.clip(x, -lim, lim)), float(anchor_y)


class EnemyTrack:
    """敌人速度的跨帧状态，与 c/sa_model.h 的 `sa_model_track_t` 同义。

    记的是上一次 `fill_inputs` 看到的**全部**敌人（不论 collidable）的 id → 坐标；
    id 重复时留池序最前那只（C 侧 `track_find` 的线性查找也是先到先得）。
    """

    def __init__(self):
        self.reset()

    def reset(self):
        self.frame = None
        self.pos = {}

    def velocity(self, frame: int, ids, x, y):
        """当前帧每只敌的 (vx, vy)。对不上 / 断帧 / 任一轴位移 > TELEPORT_PX 记 0。"""
        v = np.zeros((len(ids), 2), dtype=np.float32)
        if self.frame is not None and frame == (self.frame + 1) & 0xFFFFFFFF:
            for i, k in enumerate(ids):
                prev = self.pos.get(int(k))
                if prev is None:
                    continue
                dx, dy = np.float32(x[i]) - prev[0], np.float32(y[i]) - prev[1]
                if abs(dx) <= TELEPORT_PX and abs(dy) <= TELEPORT_PX:
                    v[i] = (dx, dy)
        return v

    def update(self, frame: int, ids, x, y):
        self.pos = {}
        for i, k in enumerate(ids):
            self.pos.setdefault(int(k), (np.float32(x[i]), np.float32(y[i])))
        self.frame = frame


def next_held(prev_action: int, action: int, held: int) -> int:
    """与 c/sa_motor.c 的 sa_motor_next_held 同义：方向换了 → 1，否则 +1（封顶）。"""
    if prev_action // 2 != action // 2:
        return 1
    return min(held + 1, HELD_NEVER)


def parse_motor(spec: str):
    """`--motor`：off → None；`hold=a,b[;delay=c,d]` → (hold_hi, delay_hi)，只用得到上界。"""
    if spec == "off":
        return None
    got = {"hold": (0, 0), "delay": (0, 0)}
    for part in spec.split(";"):
        key, _, val = part.partition("=")
        lo, hi = (int(v) for v in val.split(","))
        got[key.strip()] = (lo, hi)
    return got["hold"][1], got["delay"][1]


def fill_inputs(obs, target, prev_action: int, track: "EnemyTrack | None" = None,
                dir_held: int = HELD_NEVER) -> dict:
    """OBS 的表 → 图的七个输入。与 c/sa_model.c 的 sa_model_fill **逐条同口径**。

    这是一份独立重写，不是抄 C 的结果 —— 两边各自算、对拍才有意义。
    `track` 为 None 时敌人速度两列恒 0（与 C 侧传 NULL 同义）。
    """
    bullets = np.zeros((BULLET_ROWS, BULLET_COLS), dtype=np.float32)
    bmask = np.zeros(BULLET_ROWS, dtype=bool)
    b = obs.tables.get("bullets")
    if b is not None and len(b):
        x, y = fx_to_float(b["x"]), fx_to_float(b["y"])
        keep = ((np.asarray(b["flags"]) & BULLET_FLAG_COLLIDABLE) != 0) \
            & (x >= -ENV_HALF_W) & (x <= ENV_HALF_W) & (y >= ENV_Y_MIN) & (y <= ENV_Y_MAX)
        idx = np.flatnonzero(keep)[:BULLET_ROWS]
        n = len(idx)
        bullets[:n, 0] = x[idx]
        bullets[:n, 1] = y[idx]
        bullets[:n, 2] = fx_to_float(b["vx"][idx])
        bullets[:n, 3] = fx_to_float(b["vy"][idx])
        bullets[:n, 4] = fx_to_float(b["radius"][idx])
        bmask[:n] = True

    enemies = np.zeros((ENEMY_ROWS, ENEMY_COLS), dtype=np.float32)
    emask = np.zeros(ENEMY_ROWS, dtype=bool)
    e = obs.tables.get("enemies")
    if e is not None and len(e):
        flags = np.asarray(e["flags"])
        ex = np.asarray(fx_to_float(e["x"]), dtype=np.float32)
        ey = np.asarray(fx_to_float(e["y"]), dtype=np.float32)
        ids = np.asarray(e["id"])
        idx = np.flatnonzero((flags & ENEMY_FLAG_COLLIDABLE) != 0)[:ENEMY_ROWS]
        n = len(idx)
        enemies[:n, 0] = ex[idx]
        enemies[:n, 1] = ey[idx]
        enemies[:n, 2] = fx_to_float(e["hit_w"][idx])
        enemies[:n, 3] = ((flags[idx] & ENEMY_FLAG_BOSS) != 0).astype(np.float32)
        if track is not None:
            enemies[:n, 4:6] = track.velocity(obs.frame, ids, ex, ey)[idx]
            track.update(obs.frame, ids, ex, ey)
        emask[:n] = True
    elif track is not None:
        track.update(obs.frame, [], [], [])

    p = obs.tables["player"][0]
    player = np.array([
        fx_to_float(p["x"]), fx_to_float(p["y"]), fx_to_float(p["hit_radius"]),
        fx_to_float(p["speed"]),          # 高速档，不是 speed_focus
        1.0 if p["focus"] else 0.0,
    ], dtype=np.float32)

    return {
        "bullets": bullets, "bullets_mask": bmask,
        "enemies": enemies, "enemies_mask": emask,
        "player": player,
        "target": np.asarray(target, dtype=np.float32),
        "prev_action": np.array([max(0, min(NUM_ACTIONS - 1, int(prev_action)))], dtype=np.int64),
        HELD_INPUT: np.array([max(0, int(dir_held))], dtype=np.int64),
    }


def check_signature(sess) -> list:
    """图的输入签名要与本文件的常量相符 —— 装错版本的图应当在这里就被拦住。七输入 = 图版本 2，八输入 = 版本 3。"""
    want = {
        "bullets": (BULLET_ROWS, BULLET_COLS), "bullets_mask": (BULLET_ROWS,),
        "enemies": (ENEMY_ROWS, ENEMY_COLS), "enemies_mask": (ENEMY_ROWS,),
        "player": (PLAYER_COLS,), "target": (2,), "prev_action": (1,), HELD_INPUT: (1,),
    }
    bad = []
    got = {i.name: tuple(i.shape) for i in sess.get_inputs()}
    if tuple(got) not in (INPUT_NAMES, INPUT_NAMES + (HELD_INPUT,)):
        bad.append(f"输入名/顺序是 {tuple(got)}，应为 {INPUT_NAMES}（图版本 2）或再加 {HELD_INPUT}（图版本 3）")
    for name, shape in want.items():
        if name in got and got[name] != shape:
            bad.append(f"输入 {name} 形状是 {got[name]}，应为 {shape}")
    return bad


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="onnx 策略后端的逐帧对拍")
    ap.add_argument("log")
    ap.add_argument("--model", required=True, help=".onnx 图（与 DLL 里装的那份同一个文件）")
    ap.add_argument("--anchor", default="under_boss", choices=("under_boss", "field_bottom", "fixed", "free"),
                    help="必须与录制时**实际生效**的那一档一致（BYPASS 模式看 ini 的 bypass_anchor，默认 free）"
                         " —— 日志里没有这一路信息")
    ap.add_argument("--anchor-x", type=float, default=0.0)
    ap.add_argument("--anchor-y", type=float, default=384.0)
    ap.add_argument("--hysteresis", type=float, default=0.0, help="必须与录制时的 hysteresis_pct/100 一致")
    ap.add_argument("--eps", type=float, default=1e-3,
                    help="不一致帧允许的 top-2 logit 差上限：小于它算浮点近平局，不算分歧")
    ap.add_argument("--motor", default="off",
                    help="录制时 DLL 的手部运动层：off（严格逐帧比）/ hold=a,b[;delay=c,d]（与 ini 一致）。"
                         "开着运动层时日志里只有**执行的**动作、没有模型**想按的**，方向分歧只能判「运动层解释得了 / 解释不了」；"
                         "要严格对拍请用 motor = 0 录")
    ap.add_argument("--max-report", type=int, default=20)
    a = ap.parse_args(argv)

    try:
        import onnxruntime as ort
    except ImportError:
        print("❌ 需要 onnxruntime：python3 -m pip install onnxruntime", file=sys.stderr)
        return 2

    hello, frames = read_log(a.log)
    if not frames:
        print("❌ 日志里一帧都没有")
        return 1
    print(f"backend={hello.backend} policy={hello.policy} obs_timing={hello.obs_timing} 帧数={len(frames)}")
    print(f"图={a.model} 锚点={a.anchor}(x={a.anchor_x} y={a.anchor_y}) τ={a.hysteresis} eps={a.eps}")

    sess = ort.InferenceSession(a.model, providers=["CPUExecutionProvider"])
    bad_sig = check_signature(sess)
    if bad_sig:
        for m in bad_sig:
            print(f"❌ {m}")
        return 1

    names = tuple(i.name for i in sess.get_inputs())
    motor = parse_motor(a.motor)
    print(f"图版本 {'3（带 dir_held）' if HELD_INPUT in names else '2'} · 运动层 {a.motor}")
    prev_action, held = 0, HELD_NEVER
    explained = run_len = 0
    run_want = -1
    track = EnemyTrack()
    compared = mismatch = ties = skipped = uninvertible = bombed = 0
    reports = []

    for fr in frames:
        act = fr.act
        if act is None:
            skipped += 1
            continue
        _frame, buttons, flags = act
        # DLL 没接管的帧（回放 / 不可操作 / MANUAL）：它同时把 prev_action 清了，这里照做。
        if (flags & (ACT_PASSTHROUGH | ACT_HUMAN)) or (fr.obs.phase & PHASE_REPLAY_PLAYBACK) \
                or not (fr.obs.phase & PHASE_PLAYER_CONTROLLABLE):
            prev_action, held = 0, HELD_NEVER
            run_len, run_want = 0, -1
            track.reset()          # DLL 的 ap_policy_model_reset 连敌人速度的跨帧状态一起清
            skipped += 1
            continue

        target = anchor(fr.obs, a.anchor, a.anchor_x, a.anchor_y)
        feed = fill_inputs(fr.obs, target, prev_action, track, dir_held=held)
        logits = sess.run(["logits"], {k: feed[k] for k in names})[0].astype(np.float64)
        action_id = pick(logits, prev_action, a.hysteresis)
        want = action_buttons(action_id)

        compared += 1
        # 比较时屏蔽 BOMB：模型永远不出这一位（动作表 v1 屏蔽了它），日志里有就是 safety_net
        # 补的。不屏蔽的话 safety_net=1 录的日志会报出一堆没有解释的分歧。
        if buttons & BTN_BOMB:
            bombed += 1
        buttons &= ~BTN_BOMB
        logged = buttons_to_action(buttons)
        by_motor = False
        if motor is not None and want != buttons and logged is not None and logged % 2 == action_id % 2:
            # 低速位一致、只有方向不同：运动层可能把变向挡住了。解释得了的条件（保守）：
            # 这一段还没执行满最长的最短保持，或者同一个意图被延迟的帧数还在延迟上界之内。
            run_len = run_len + 1 if run_want == action_id // 2 else 1
            run_want = action_id // 2
            by_motor = logged // 2 == prev_action // 2 and (held < motor[0] or run_len <= motor[1])
        else:
            run_len, run_want = 0, -1
        if by_motor:
            explained += 1
        elif want != buttons:
            order = np.argsort(-logits)
            gap = float(logits[order[0]] - logits[order[1]])
            if gap < a.eps:
                ties += 1          # 浮点近平局：C 与 Python 的 argmax 可以合理地分岔
            else:
                mismatch += 1
                if len(reports) < a.max_report:
                    reports.append(
                        f"  frame={fr.obs.frame} 日志 buttons=0x{buttons:02x} 重算=0x{want:02x}"
                        f"（id={action_id}，top-2 差={gap:.4g}，prev={prev_action}，"
                        f"锚点=({target[0]:.0f},{target[1]:.0f})）")
        # ★ 下一帧的 prev 用**日志里这一帧**的动作反推，不用重算结果 —— 见 buttons_to_action。
        logged_id = buttons_to_action(buttons)
        if logged_id is None:
            uninvertible += 1
            logged_id = action_id        # 反推不出来只能退回重算值
        held = next_held(prev_action, logged_id, held)
        prev_action = logged_id

    print(f"比对 {compared} 帧 · 跳过 {skipped} 帧（未接管/无 ACT）")
    if bombed:
        print(f"（{bombed} 帧带 BOMB 位 —— safety_net 补的，比较时已屏蔽；模型自己从不出这一位）")
    if uninvertible:
        print(f"⚠ {uninvertible} 帧的按钮位不是动作表 v1 能产出的组合 —— 录制时是不是没关 safety_net，"
              f"或者那几帧其实是 builtin 在跑？")
    if motor is not None:
        print(f"（{explained} 帧 = {explained / max(compared, 1):.1%} 的方向分歧由运动层解释：模型想换、手还锁着 / 还在延迟里）")
    print(f"一致 {compared - mismatch - ties - explained} · 近平局 {ties} · **分歧 {mismatch}**")
    for line in reports:
        print(line)
    if mismatch > len(reports):
        print(f"  …… 另有 {mismatch - len(reports)} 帧未列出")

    if compared == 0:
        print("❌ 一帧都没比到 —— 日志里没有模型接管的帧？确认 policy=onnx 且录的是 BYPASS 段")
        return 1
    if mismatch:
        print("❌ 有真正的分歧：C 侧与 Python 侧算出了不同的动作，且不是浮点近平局")
        return 1
    print("✓ 逐帧一致（不一致帧全在浮点近平局内）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
