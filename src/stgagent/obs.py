"""OBS 解码：u32 frame, u32 phase, u8 table_count, [u8 id, u16 count, count×stride]。"""
import struct
from dataclasses import dataclass
import numpy as np
from .schema import Hello

_HDR = struct.Struct("<IIB")
_TBL = struct.Struct("<BH")


@dataclass
class Observation:
    frame: int
    phase: int
    tables: dict


def decode_obs(payload: bytes, hello: Hello) -> Observation:
    """解码一条 OBS。任何长度不一致都抛 ValueError —— 训练侧会成批读崩溃留下的半截日志，
    错误类型必须统一可捞（struct.error 不是 ValueError 的子类，会漏网）。"""
    if len(payload) < _HDR.size:
        raise ValueError(f"OBS header truncated: {len(payload)} < {_HDR.size} bytes")
    frame, phase, n = _HDR.unpack_from(payload, 0)
    pos = _HDR.size
    tables = {}
    for _ in range(n):
        if pos + _TBL.size > len(payload):
            raise ValueError(f"OBS table header truncated at offset {pos}")
        tid, count = _TBL.unpack_from(payload, pos)
        pos += _TBL.size
        t = hello.tables.get(tid)
        if t is None:
            raise ValueError(f"unknown table id {tid}")
        if count > t.cap:
            raise ValueError(f"table {t.name}: count {count} > cap {t.cap}")
        nbytes = count * t.stride
        if pos + nbytes > len(payload):
            raise ValueError(f"table {t.name}: truncated")
        tables[t.name] = np.frombuffer(payload, dtype=t.dtype, count=count, offset=pos).copy()
        pos += nbytes
    return Observation(frame, phase, tables)
