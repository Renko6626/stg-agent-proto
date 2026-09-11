import json
import struct
import pytest
from stgagent import schema, obs


def _obs_bytes():
    body = struct.pack("<IIB", 42, 0b100001, 2)
    body += struct.pack("<BH", 1, 1) + struct.pack("<iiBBH", -184 << 16, 400 << 16, 0, 3, 128)
    body += struct.pack("<BH", 2, 2)
    body += struct.pack("<iiH", 10 << 16, 20 << 16, 16384)
    body += struct.pack("<iiH", -10 << 16, 30 << 16, 49152)
    return body


def test_decode_obs_tables(hello_dict):
    h = schema.parse_hello(json.dumps(hello_dict).encode())
    o = obs.decode_obs(_obs_bytes(), h)
    assert o.frame == 42 and o.phase == 0b100001
    p = o.tables["player"]
    assert len(p) == 1 and p["lives"][0] == 3 and p["x"][0] == -184 << 16
    b = o.tables["bullets"]
    assert len(b) == 2 and list(b["angle"]) == [16384, 49152] and b["y"][1] == 30 << 16
    assert "spell" not in o.tables


def test_count_over_cap_rejected(hello_dict):
    h = schema.parse_hello(json.dumps(hello_dict).encode())
    body = struct.pack("<IIB", 1, 0, 1) + struct.pack("<BH", 2, 5) + b"\0" * 50
    with pytest.raises(ValueError):
        obs.decode_obs(body, h)
