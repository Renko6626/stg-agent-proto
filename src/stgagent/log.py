""".stglog 读取器：HELLO 一条，然后 OBS/ACT 成对，BYE 收尾（可缺）。"""
from dataclasses import dataclass
from .consts import MsgType
from .framing import iter_records
from .schema import parse_hello, Hello
from .obs import decode_obs, Observation
from .act import decode_act


@dataclass
class Frame:
    obs: Observation
    act: tuple | None


def read_log(path):
    frames = []
    with open(path, "rb") as f:
        it = iter_records(f)
        first = next(it, None)
        if first is None or first[0] != MsgType.HELLO:
            raise ValueError("log must start with HELLO")
        hello = parse_hello(first[1])
        for mtype, payload in it:
            if mtype == MsgType.OBS:
                frames.append(Frame(decode_obs(payload, hello), None))
            elif mtype == MsgType.ACT:
                a = decode_act(payload)
                if frames and frames[-1].act is None and frames[-1].obs.frame == a[0]:
                    frames[-1].act = a
            elif mtype == MsgType.BYE:
                break
    return hello, frames
