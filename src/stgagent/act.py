import struct
from .consts import ACT_SIZE

_ACT = struct.Struct("<III")


def encode_act(frame: int, buttons: int, flags: int = 0) -> bytes:
    return _ACT.pack(frame & 0xFFFFFFFF, buttons & 0xFFFFFFFF, flags & 0xFFFFFFFF)


def decode_act(b: bytes):
    if len(b) != ACT_SIZE:
        raise ValueError(f"ACT must be {ACT_SIZE} bytes, got {len(b)}")
    return _ACT.unpack(b)
