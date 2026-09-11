import io
from stgagent import framing


def test_pack_unpack_roundtrip():
    b = framing.pack_record(2, b"\x01\x02\x03")
    assert b == b"\x04\x00\x00\x00" + b"\x02" + b"\x01\x02\x03"
    assert framing.unpack_from(b + b"tail") == (2, b"\x01\x02\x03", 8)


def test_unpack_incomplete_returns_zero():
    assert framing.unpack_from(b"\x04\x00\x00\x00\x02\x01") == (0, b"", 0)


def test_iter_records_stops_at_truncated_tail():
    data = framing.pack_record(1, b"hello") + framing.pack_record(2, b"obs") + b"\x09\x00\x00\x00\x03\x01"
    assert list(framing.iter_records(io.BytesIO(data))) == [(1, b"hello"), (2, b"obs")]
