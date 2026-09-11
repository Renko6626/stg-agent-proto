"""HELLO 的解析与校验；把 tables 变成 numpy structured dtype。"""
import json
from dataclasses import dataclass, field
import numpy as np
from .consts import PROTO_VERSION, FIELD_TYPES


class SchemaError(ValueError):
    pass


@dataclass(frozen=True)
class Field:
    name: str
    type: str
    off: int


@dataclass(frozen=True)
class Table:
    id: int
    name: str
    cap: int
    stride: int
    fields: tuple
    dtype: np.dtype


@dataclass
class Hello:
    proto: int
    backend: str
    policy: str
    obs_timing: str
    tick_hz: int
    half_w: int
    height: int
    move_area: dict
    actions: dict
    tables: dict = field(default_factory=dict)

    def table_by_name(self, name):
        for t in self.tables.values():
            if t.name == name:
                return t
        return None


def _build_table(d) -> Table:
    stride = int(d["stride"])
    fields, spans, names, formats, offsets = [], [], [], [], []
    for f in d["fields"]:
        if f["type"] not in FIELD_TYPES:
            raise SchemaError(f"table {d['name']}: unknown field type {f['type']!r}")
        size = FIELD_TYPES[f["type"]][2]
        off = int(f["off"])
        if off + size > stride:
            raise SchemaError(f"table {d['name']}: field {f['name']} exceeds stride")
        for a, b in spans:
            if off < b and a < off + size:
                raise SchemaError(f"table {d['name']}: field {f['name']} overlaps another field")
        spans.append((off, off + size))
        fields.append(Field(f["name"], f["type"], off))
        names.append(f["name"]); formats.append(FIELD_TYPES[f["type"]][1]); offsets.append(off)
    dtype = np.dtype({"names": names, "formats": formats, "offsets": offsets, "itemsize": stride})
    return Table(int(d["id"]), d["name"], int(d["cap"]), stride, tuple(fields), dtype)


def parse_hello(payload: bytes) -> Hello:
    d = json.loads(payload.decode("utf-8"))
    if d.get("proto") != PROTO_VERSION:
        raise SchemaError(f"proto {d.get('proto')} != {PROTO_VERSION}")
    tables = {}
    for td in d["tables"]:
        t = _build_table(td)
        if t.id in tables:
            raise SchemaError(f"duplicate table id {t.id}")
        tables[t.id] = t
    fld = d["field"]
    return Hello(
        proto=d["proto"], backend=d["backend"], policy=d.get("policy", ""),
        obs_timing=d.get("obs_timing", ""), tick_hz=int(d["tick_hz"]),
        half_w=int(fld["half_w"]), height=int(fld["height"]),
        move_area=dict(fld.get("move_area", {})),
        actions={int(a["bit"]): a["name"] for a in d["actions"]},
        tables=tables,
    )
