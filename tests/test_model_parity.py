"""C ↔ Python 的填数组对拍，外加对拍工具里几个纯函数的判别式测试。

`tools/check_model_parity.py` 的 `fill_inputs` 是 `c/sa_model.c` 的 `sa_model_fill` 的独立重写。
两边各写一套单测测不出口径分叉 —— 列序写反、包络边界差一、`speed` 取了 focus 档，都能各自
自洽地绿。所以这里让 C 侧的夹具（`c/tests/dump_model_in.c`）把**同一批世界**一边录成 `.stglog`、
一边倒出填好的数组，Python 侧读日志重算，再逐元素比。
"""
import importlib.util
import struct
import subprocess
from pathlib import Path

import numpy as np
import pytest

from stgagent import read_log
from stgagent.consts import BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_SHOT, BTN_SLOW, BTN_UP

ROOT = Path(__file__).resolve().parents[1]
C_DIR = ROOT / "c"

BULLET_ROWS, BULLET_COLS = 640, 5
ENEMY_ROWS, ENEMY_COLS = 256, 6
PLAYER_COLS = 5


def _load_tool():
    """tools/ 不是包，按路径加载。"""
    path = ROOT / "tools" / "check_model_parity.py"
    spec = importlib.util.spec_from_file_location("check_model_parity", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


TOOL = _load_tool()


@pytest.fixture(scope="module")
def dump(tmp_path_factory):
    subprocess.run(["make", "-C", str(C_DIR), "build/dump_model_in"], check=True)
    d = tmp_path_factory.mktemp("dump")
    log, bin_ = d / "dump.stglog", d / "dump.bin"
    subprocess.run([str(C_DIR / "build" / "dump_model_in"), str(log), str(bin_)], check=True)
    return log, bin_


def _read_dump(path):
    """按 dump_model_in.c 头部注释的顺序逐块读回来。"""
    raw = path.read_bytes()
    head = struct.Struct("<2fqqii")
    per = (head.size + BULLET_ROWS * BULLET_COLS * 4 + BULLET_ROWS
           + ENEMY_ROWS * ENEMY_COLS * 4 + ENEMY_ROWS + PLAYER_COLS * 4)
    assert len(raw) % per == 0, f"文件大小 {len(raw)} 不是每帧 {per} 字节的整数倍"
    out = []
    for k in range(len(raw) // per):
        o = k * per
        tx, ty, prev, held, nb, ne = head.unpack_from(raw, o)
        o += head.size

        def take(count, dtype):
            nonlocal o
            a = np.frombuffer(raw, dtype=dtype, count=count, offset=o)
            o += count * np.dtype(dtype).itemsize
            return a

        bullets = take(BULLET_ROWS * BULLET_COLS, "<f4").reshape(BULLET_ROWS, BULLET_COLS)
        bmask = take(BULLET_ROWS, "u1").astype(bool)
        enemies = take(ENEMY_ROWS * ENEMY_COLS, "<f4").reshape(ENEMY_ROWS, ENEMY_COLS)
        emask = take(ENEMY_ROWS, "u1").astype(bool)
        player = take(PLAYER_COLS, "<f4")
        out.append({"target": (tx, ty), "prev_action": prev, "dir_held": held, "nb": nb, "ne": ne,
                    "bullets": bullets, "bullets_mask": bmask,
                    "enemies": enemies, "enemies_mask": emask, "player": player})
    return out


def test_python_fill_matches_c_fill(dump):
    log, bin_ = dump
    _hello, frames = read_log(log)
    want = _read_dump(bin_)
    assert len(frames) == len(want) == 9, "夹具应有 9 个场景"

    track = TOOL.EnemyTrack()      # 整个序列共用一份：敌人速度是跨帧（= 跨场景）差分的
    for k, (fr, w) in enumerate(zip(frames, want)):
        got = TOOL.fill_inputs(fr.obs, w["target"], int(w["prev_action"]), track, dir_held=int(w["dir_held"]))
        assert got["dir_held"][0] == w["dir_held"] == ((1 << 20) if k == 5 else 1 + 3 * k)
        assert int(got["bullets_mask"].sum()) == w["nb"], f"场景 {k}：弹行数不符"
        assert int(got["enemies_mask"].sum()) == w["ne"], f"场景 {k}：敌行数不符"
        for key in ("bullets", "bullets_mask", "enemies", "enemies_mask", "player"):
            np.testing.assert_array_equal(got[key], w[key], err_msg=f"场景 {k}：{key} 不符")
        np.testing.assert_array_equal(got["target"], np.asarray(w["target"], dtype=np.float32))
        assert got["prev_action"][0] == w["prev_action"]


def test_fixture_actually_exercises_every_rule(dump):
    """夹具本身得有判别力：如果每个场景都是空场，上面那条测试等于什么都没测。"""
    _log, bin_ = dump
    w = _read_dump(bin_)
    assert w[0]["nb"] == 0 and w[0]["ne"] == 0, "场景 0 应是空场"
    assert w[1]["nb"] == 4, "场景 1：8 颗里 4 颗 collidable"
    assert w[2]["nb"] == 4, "场景 2：8 个边界样本里 4 个在界内"
    assert w[3]["ne"] == 2, "场景 3：3 只敌里 2 只参与碰撞"
    assert w[4]["player"][4] == 1.0, "场景 4：focus 应为 1"
    assert w[5]["nb"] == BULLET_ROWS, "场景 5：满池"
    # speed 列必须是高速档 4.0 而不是低速档 2.0 —— 取错档时上面的对拍会红，这里点明期望值
    assert all(f["player"][3] == 4.0 for f in w)
    # 敌人第三列是 hit_w（16）而不是 hit_h（24）
    assert w[3]["enemies"][0][2] == 16.0
    # 敌人速度：场景 4 的 id 1 从场景 3 跳了 140 px → 瞬移守卫记 0
    assert tuple(w[4]["enemies"][0][4:6]) == (0.0, 0.0)
    assert not w[6]["enemies"][:, 4:6].any(), "场景 6：上一帧没有这些 id，速度全 0"
    assert [tuple(r[4:6]) for r in w[7]["enemies"][:4]] == [(2.5, 0.0), (-1.5, 3.0), (0.0, 2.0), (0.0, 0.0)]
    assert [tuple(r[4:6]) for r in w[8]["enemies"][:2]] == [(16.0, 0.0), (0.0, 0.0)], "瞬移守卫两侧"


def test_no_track_means_zero_velocity(dump):
    log, _bin = dump
    _hello, frames = read_log(log)
    got = TOOL.fill_inputs(frames[7].obs, (0.0, 384.0), 0)
    assert not got["enemies"][:, 4:6].any()


def test_action_table_matches_c_and_training_repo():
    dirs = (0, BTN_UP, BTN_UP | BTN_RIGHT, BTN_RIGHT, BTN_DOWN | BTN_RIGHT,
            BTN_DOWN, BTN_DOWN | BTN_LEFT, BTN_LEFT, BTN_UP | BTN_LEFT)
    for a in range(TOOL.NUM_ACTIONS):
        want = dirs[a // 2] | BTN_SHOT | (BTN_SLOW if a % 2 else 0)
        assert TOOL.action_buttons(a) == want
        assert not (TOOL.action_buttons(a) & (1 << 5)), "动作表 v1 从不置 BOMB"


def test_buttons_to_action_round_trips_and_rejects_garbage():
    """动作表可逆是「每帧独立比」的前提（否则一帧分歧会级联）。"""
    for a in range(TOOL.NUM_ACTIONS):
        assert TOOL.buttons_to_action(TOOL.action_buttons(a)) == a
        # safety_net 补的 BOMB 位不影响反推
        assert TOOL.buttons_to_action(TOOL.action_buttons(a) | (1 << 5)) == a
    assert TOOL.buttons_to_action(0) is None, "没按 SHOT 就不是动作表产出的"
    assert TOOL.buttons_to_action(BTN_SHOT | BTN_UP | BTN_DOWN) is None, "上下同按不在表里"
    assert TOOL.buttons_to_action(BTN_SHOT | BTN_LEFT | BTN_RIGHT) is None, "左右同按不在表里"


def test_pick_hysteresis_boundary():
    logits = np.zeros(TOOL.NUM_ACTIONS)
    logits[4] = 2.0
    logits[9] = 1.5          # 差值恰好 0.5，float 可精确表示（见 c/tests/test_model.c 同款注释）
    assert TOOL.pick(logits, 9, 0.0) == 4
    assert TOOL.pick(logits, 9, 0.5) == 9, "等号边界按 <= 应保持上一步"
    assert TOOL.pick(logits, 9, 0.25) == 4
    assert TOOL.pick(logits, 4, 9.0) == 4
    assert TOOL.pick(logits, -1, 9.0) == 4, "上一步非法时退成 argmax"


def test_anchor_modes(dump):
    log, _bin = dump
    _hello, frames = read_log(log)
    boss_frame = frames[3].obs          # 场景 3：boss 在 x = 20
    empty = frames[0].obs

    assert TOOL.anchor(boss_frame, "under_boss", 0.0, 384.0) == (20.0, 384.0)
    assert TOOL.anchor(empty, "under_boss", 0.0, 384.0) == (0.0, 384.0)
    assert TOOL.anchor(boss_frame, "field_bottom", 0.0, 384.0) == (0.0, 384.0)
    assert TOOL.anchor(empty, "fixed", -80.0, 400.0) == (-80.0, 400.0)
    lim = TOOL.FIELD_HALF_W - TOOL.ANCHOR_MARGIN
    assert TOOL.anchor(empty, "fixed", 999.0, 384.0) == (lim, 384.0)
