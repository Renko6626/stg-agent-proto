import subprocess
from pathlib import Path
import pytest
from stgagent import read_log, BTN_SHOT, ACT_HUMAN
from stgagent.fixed import fx_to_float

C_DIR = Path(__file__).resolve().parents[1] / "c"


@pytest.fixture(scope="module")
def sample_log(tmp_path_factory):
    subprocess.run(["make", "-C", str(C_DIR), "build/write_sample_log"], check=True)
    out = tmp_path_factory.mktemp("log") / "sample.stglog"
    subprocess.run([str(C_DIR / "build" / "write_sample_log"), str(out)], check=True)
    return out


def test_python_reads_c_log(sample_log):
    hello, frames = read_log(sample_log)
    assert hello.backend == "c-sample" and hello.policy == "builtin" and hello.obs_timing == "prev-frame-final"
    assert hello.move_area == {"xmin": -184, "xmax": 184, "ymin": 16, "ymax": 432}
    assert [t.name for t in hello.tables.values()] == ["player", "bullets", "enemies", "lasers"]
    assert hello.table_by_name("bullets").cap == 640
    assert [f.obs.frame for f in frames] == [0, 1, 2]
    assert frames[1].act == (1, BTN_SHOT, ACT_HUMAN) and frames[0].act == (0, BTN_SHOT, 0)
    b = frames[2].obs.tables["bullets"]
    assert len(b) == 1 and fx_to_float(int(b["y"][0])) == 22.0 and b["flags"][0] == 0x03
    p = frames[0].obs.tables["player"]
    assert fx_to_float(int(p["y"][0])) == 400.0 and p["lives"][0] == 3 and p["power"][0] == 100
    assert len(frames[0].obs.tables["enemies"]) == 0 and len(frames[0].obs.tables["lasers"]) == 0
