import json
import numpy as np
import pytest
from stgagent import schema


def test_parse_hello_builds_dtypes(hello_dict):
    h = schema.parse_hello(json.dumps(hello_dict).encode())
    assert h.backend == "fake" and h.policy == "builtin" and h.half_w == 192
    assert h.move_area == {"xmin": -184, "xmax": 184, "ymin": 16, "ymax": 432}
    assert h.actions == {0: "UP", 4: "SHOT"}
    t = h.table_by_name("bullets")
    assert t.id == 2 and t.dtype.itemsize == 10 and t.dtype.names == ("x", "y", "angle")
    assert t.dtype["angle"] == np.dtype("<u2")


def test_rejects_wrong_proto(hello_dict):
    hello_dict["proto"] = 2
    with pytest.raises(schema.SchemaError):
        schema.parse_hello(json.dumps(hello_dict).encode())


def test_rejects_field_past_stride(hello_dict):
    hello_dict["tables"][1]["fields"][2]["off"] = 9
    with pytest.raises(schema.SchemaError):
        schema.parse_hello(json.dumps(hello_dict).encode())


def test_rejects_overlapping_fields(hello_dict):
    hello_dict["tables"][0]["fields"][1]["off"] = 2
    with pytest.raises(schema.SchemaError):
        schema.parse_hello(json.dumps(hello_dict).encode())
