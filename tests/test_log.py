import json
import struct
from stgagent import framing, read_log
from stgagent.act import encode_act
from stgagent.consts import BTN_SHOT, ACT_HUMAN


def _obs(frame):
    body = struct.pack("<IIB", frame, 1, 2)
    body += struct.pack("<BH", 1, 1) + struct.pack("<iiBBH", 0, 400 << 16, 0, 3, 100)
    body += struct.pack("<BH", 2, 1) + struct.pack("<iiH", 10 << 16, (20 + frame) << 16, 16384)
    return body


def test_read_log_pairs_obs_and_act(tmp_path, hello_dict):
    p = tmp_path / "a.stglog"
    data = framing.pack_record(1, json.dumps(hello_dict).encode())
    for f in range(3):
        data += framing.pack_record(2, _obs(f))
        data += framing.pack_record(3, encode_act(f, BTN_SHOT, ACT_HUMAN if f == 1 else 0))
    data += framing.pack_record(5, b"")
    p.write_bytes(data)
    hello, frames = read_log(p)
    assert hello.backend == "fake" and [fr.obs.frame for fr in frames] == [0, 1, 2]
    assert frames[1].act == (1, BTN_SHOT, ACT_HUMAN) and frames[2].act[2] == 0
    assert frames[2].obs.tables["bullets"]["y"][0] == 22 << 16


def test_read_log_tolerates_truncation(tmp_path, hello_dict):
    p = tmp_path / "b.stglog"
    data = framing.pack_record(1, json.dumps(hello_dict).encode())
    data += framing.pack_record(2, _obs(0)) + framing.pack_record(3, encode_act(0, 0))
    data += framing.pack_record(2, _obs(1))[:-3]          # 第二帧 OBS 写了一半
    p.write_bytes(data)
    _, frames = read_log(p)
    assert len(frames) == 1 and frames[0].act == (0, 0, 0)
