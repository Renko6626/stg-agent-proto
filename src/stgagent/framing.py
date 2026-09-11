"""记录封帧：u32 length（不含自身）+ u8 type + payload。文件与 socket 共用。"""
import struct

_HDR = struct.Struct("<IB")


def pack_record(mtype: int, payload: bytes) -> bytes:
    return _HDR.pack(len(payload) + 1, mtype) + payload


def unpack_from(buf):
    if len(buf) < 4:
        return 0, b"", 0
    length = struct.unpack_from("<I", buf, 0)[0]
    if length < 1 or len(buf) < 4 + length:
        return 0, b"", 0
    return buf[4], bytes(buf[5:4 + length]), 4 + length


def iter_records(f):
    """逐条读记录；遇到不完整的尾巴（进程异常退出）就停，不抛。"""
    while True:
        hdr = f.read(4)
        if len(hdr) < 4:
            return
        length = struct.unpack("<I", hdr)[0]
        if length < 1:
            return
        body = f.read(length)
        if len(body) < length:
            return
        yield body[0], body[1:]
