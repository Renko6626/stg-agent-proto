from stgagent import act
from stgagent.consts import BTN_LEFT, BTN_SHOT, ACT_HUMAN


def test_encode_act_is_12_bytes_le():
    b = act.encode_act(7, BTN_LEFT | BTN_SHOT, ACT_HUMAN)
    assert b == (7).to_bytes(4, "little") + (0x14).to_bytes(4, "little") + (2).to_bytes(4, "little")
    assert act.decode_act(b) == (7, 0x14, 2)
