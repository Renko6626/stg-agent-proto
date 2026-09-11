"""Q16.16 定点与 BAM 角的换算。线上是整数；float 只在训练侧出现。"""
import math
import numpy as np

_ONE = 65536.0
_I32_MIN, _I32_MAX = -(2**31), 2**31 - 1


def fx_to_float(x):
    if isinstance(x, np.ndarray):
        return np.asarray(x, dtype=np.int64) / _ONE
    return x / _ONE


def float_to_fx(f):
    if isinstance(f, np.ndarray):
        return np.clip(np.rint(f * _ONE), _I32_MIN, _I32_MAX).astype("<i4")
    return max(_I32_MIN, min(_I32_MAX, int(round(f * _ONE))))


def bam_to_rad(b):
    if isinstance(b, np.ndarray):
        return np.asarray(b, dtype=np.float64) * (2 * math.pi / _ONE)
    return b * (2 * math.pi / _ONE)


def rad_to_bam(r):
    if isinstance(r, np.ndarray):
        return (np.rint(r * (_ONE / (2 * math.pi))).astype(np.int64) & 0xFFFF).astype("<u2")
    return int(round(r * (_ONE / (2 * math.pi)))) & 0xFFFF
