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
