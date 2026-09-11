import math
import numpy as np
from stgagent import fixed


def test_fx_roundtrip_scalar():
    assert fixed.fx_to_float(65536) == 1.0
    assert fixed.float_to_fx(-184.0) == -184 * 65536
    assert fixed.fx_to_float(fixed.float_to_fx(0.0078125)) == 0.0078125


def test_fx_vectorized():
    a = np.array([0, 32768, -65536], dtype="<i4")
    np.testing.assert_allclose(fixed.fx_to_float(a), [0.0, 0.5, -1.0])


def test_float_to_fx_clamps_to_i32():
    assert fixed.float_to_fx(1e9) == 2**31 - 1
    assert fixed.float_to_fx(-1e9) == -2**31


def test_bam_roundtrip():
    assert fixed.rad_to_bam(math.pi) == 32768
    assert fixed.rad_to_bam(-math.pi / 2) == 49152
    assert abs(fixed.bam_to_rad(16384) - math.pi / 2) < 1e-9
