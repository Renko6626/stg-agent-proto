"""stg-agent-proto v1 常量。唯一权威是仓库根的 SPEC.md；本文件是它的机器可读形式。"""
from enum import IntEnum

PROTO_VERSION = 1


class MsgType(IntEnum):
    HELLO = 1
    OBS = 2
    ACT = 3
    CTRL = 4
    BYE = 5


_ACTION_BITS = {
    "UP": 0, "DOWN": 1, "LEFT": 2, "RIGHT": 3,
    "SHOT": 4, "BOMB": 5, "SLOW": 6, "TIMESTOP": 7,
    "CARD_USE": 8, "CARD_SWITCH": 9,
}
ACTION_NAMES = {bit: name for name, bit in _ACTION_BITS.items()}

BTN_UP = 1 << 0
BTN_DOWN = 1 << 1
BTN_LEFT = 1 << 2
BTN_RIGHT = 1 << 3
BTN_SHOT = 1 << 4
BTN_BOMB = 1 << 5
BTN_SLOW = 1 << 6
BTN_TIMESTOP = 1 << 7
BTN_CARD_USE = 1 << 8
BTN_CARD_SWITCH = 1 << 9

# 掉落物种类（契约统一枚举，镜像 c/world.h 的 AP_ITEM_*；tests/test_consts.py 与 C 头逐项对拍）
ITEM_UNKNOWN = 0
ITEM_POWER = 1
ITEM_POINT = 2
ITEM_BIG_POWER = 3
ITEM_BOMB = 4
ITEM_FULL_POWER = 5
ITEM_LIFE = 6
ITEM_CANCEL = 7
ITEM_LIFE_PIECE = 8
ITEM_BOMB_PIECE = 9

ACT_PASSTHROUGH = 1
ACT_HUMAN = 2

PHASE_IN_GAME = 1 << 0
PHASE_PAUSED = 1 << 1
PHASE_DIALOGUE = 1 << 2
PHASE_SHOP = 1 << 3
PHASE_BOMB_ACTIVE = 1 << 4
PHASE_SPELL_ACTIVE = 1 << 5
PHASE_PLAYER_CONTROLLABLE = 1 << 6
PHASE_REPLAY_PLAYBACK = 1 << 7

# 字段类型 → (struct 格式, numpy dtype, 字节数)
FIELD_TYPES = {
    "fx": ("<i", "<i4", 4),
    "angle": ("<H", "<u2", 2),
    "i32": ("<i", "<i4", 4),
    "u32": ("<I", "<u4", 4),
    "u16": ("<H", "<u2", 2),
    "u8": ("<B", "u1", 1),
}

ACT_SIZE = 12
