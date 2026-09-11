import pytest

HELLO = {
    "proto": 1, "backend": "fake", "policy": "builtin", "obs_timing": "prev-frame-final", "tick_hz": 60,
    "field": {"half_w": 192, "height": 448, "origin": "center-top",
              "move_area": {"xmin": -184, "xmax": 184, "ymin": 16, "ymax": 432}},
    "number": {"fx": "q16.16-i32-le", "angle": "bam-u16-le"},
    "actions": [{"bit": 0, "name": "UP"}, {"bit": 4, "name": "SHOT"}],
    "tables": [
        {"id": 1, "name": "player", "cap": 1, "stride": 12,
         "fields": [{"name": "x", "type": "fx", "off": 0}, {"name": "y", "type": "fx", "off": 4},
                    {"name": "state", "type": "u8", "off": 8}, {"name": "lives", "type": "u8", "off": 9},
                    {"name": "power", "type": "u16", "off": 10}]},
        {"id": 2, "name": "bullets", "cap": 4, "stride": 10,
         "fields": [{"name": "x", "type": "fx", "off": 0}, {"name": "y", "type": "fx", "off": 4},
                    {"name": "angle", "type": "angle", "off": 8}]},
    ],
}


@pytest.fixture
def hello_dict():
    import copy
    return copy.deepcopy(HELLO)
