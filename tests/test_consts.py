from stgagent import consts


def test_proto_version_is_1():
    assert consts.PROTO_VERSION == 1


def test_msg_types_match_spec():
    assert (consts.MsgType.HELLO, consts.MsgType.OBS, consts.MsgType.ACT,
            consts.MsgType.CTRL, consts.MsgType.BYE) == (1, 2, 3, 4, 5)


def test_action_bits_match_spec():
    assert consts.BTN_UP == 1 << 0 and consts.BTN_SLOW == 1 << 6
    assert consts.BTN_TIMESTOP == 1 << 7 and consts.BTN_CARD_SWITCH == 1 << 9
    assert consts.ACTION_NAMES[0] == "UP" and consts.ACTION_NAMES[9] == "CARD_SWITCH"


def test_flags_and_phase():
    assert consts.ACT_PASSTHROUGH == 1 and consts.ACT_HUMAN == 2
    assert consts.PHASE_IN_GAME == 1 and consts.PHASE_REPLAY_PLAYBACK == 1 << 7
    assert consts.FIELD_TYPES["fx"] == ("<i", "<i4", 4) and consts.ACT_SIZE == 12


def test_item_piece_kinds():
    from stgagent import consts
    assert consts.ITEM_LIFE_PIECE == 8
    assert consts.ITEM_BOMB_PIECE == 9


def test_c_header_and_python_constants_agree():
    """c/world.h 的 AP_ITEM_* / AP_PHASE_* / AP_BTN_* 与 consts.py 逐项相等（防单边漂移）。"""
    import re
    from pathlib import Path
    header = (Path(__file__).resolve().parents[1] / "c" / "world.h").read_text(encoding="utf-8")
    items = {m[1]: int(m[2]) for m in re.finditer(r"#define AP_ITEM_(\w+)\s+(\d+)", header)}
    assert items, "world.h 里应解析到 AP_ITEM_*"
    for name, value in items.items():
        assert getattr(consts, f"ITEM_{name}") == value, name
    shifts = {(m[1], m[2]): int(m[3]) for m in re.finditer(r"#define AP_(PHASE|BTN)_(\w+)\s+\(1u << (\d+)\)", header)}
    assert shifts, "world.h 里应解析到 AP_PHASE_* / AP_BTN_*"
    for (kind, name), bit in shifts.items():
        assert getattr(consts, f"{kind}_{name}") == 1 << bit, f"{kind}_{name}"


def test_item_constants_exported_at_package_level():
    import stgagent
    assert stgagent.ITEM_LIFE_PIECE == 8 and stgagent.ITEM_CANCEL == 7
