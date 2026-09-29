from __future__ import annotations

import pytest

import dar


def test_python_task_crosses_cpp_boundary():
    with dar.init(worker_count=1) as runtime:
        agent = dar.Agent("increment", lambda value: value + 1)
        ref = runtime.submit(agent, 41)

        assert isinstance(ref, dar.ObjectRef)
        assert ref.get() == 42


def test_positional_and_keyword_arguments_round_trip():
    def calculate(x, y, *, scale=1):
        return (x + y) * scale

    with dar.init(worker_count=1) as runtime:
        ref = runtime.submit(
            dar.Agent("calculate", calculate),
            2,
            3,
            scale=10,
        )

        assert ref.get() == 50


@pytest.mark.parametrize(
    "value",
    [
        None,
        True,
        123,
        3.25,
        "DAR",
        b"\x00\x01\xff",
        [1, 2, 3],
        ("a", 1),
        {"hello": "world", "nested": [1, 2]},
    ],
)
def test_pickle_round_trip(value):
    with dar.init(worker_count=1) as runtime:
        ref = runtime.submit(dar.Agent("echo", lambda item: item), value)
        assert ref.get() == value


def test_task_id_is_nonempty_hex_string():
    with dar.init(worker_count=1) as runtime:
        ref = runtime.submit(dar.Agent("identity", lambda value: value), 1)

        assert isinstance(ref.task_id, str)
        assert ref.task_id
        int(ref.task_id, 16)
        assert ref.get() == 1


def test_successful_task_reaches_succeeded_state():
    with dar.init(worker_count=1) as runtime:
        ref = runtime.submit(dar.Agent("identity", lambda value: value), 1)
        assert ref.get() == 1
        assert ref.status().upper() == "SUCCEEDED"


def test_object_ref_repr_contains_task_id():
    with dar.init(worker_count=1) as runtime:
        ref = runtime.submit(dar.Agent("identity", lambda value: value), 1)
        assert ref.task_id in repr(ref)
        assert ref.get() == 1
