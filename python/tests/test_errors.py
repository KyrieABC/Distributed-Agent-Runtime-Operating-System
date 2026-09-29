from __future__ import annotations

import pytest

import dar


def test_infeasible_resource_request_is_rejected():
    runtime = dar.init(
        worker_count=1,
        resources_per_worker={"CPU": 1.0},
    )
    try:
        agent = dar.Agent(
            "too-large",
            lambda value: value,
            dar.AgentOptions(resources={"CPU": 2.0}),
        )

        with pytest.raises(dar.ResourceExhaustedError):
            runtime.submit(agent, 1)
    finally:
        runtime.shutdown()


def test_python_exception_marks_task_failed_without_escaping_cpp_frames():
    def boom(value):
        raise RuntimeError(f"boom: {value}")

    with dar.init(worker_count=1) as runtime:
        ref = runtime.submit(dar.Agent("boom", boom), 7)

        # Current Phase-2 implementation converts py::error_already_set to
        # Status::Internal in PythonCallback::Invoke(). Therefore the current
        # public result is InternalError, not TaskFailedError.
        with pytest.raises(dar.InternalError, match="boom: 7"):
            ref.get()

        assert ref.status().upper() == "FAILED"


@pytest.mark.xfail(
    strict=True,
    reason=(
        "Phase-2 gap: Python handler failures currently become Status::Internal. "
        "Wire task execution failure to TaskFailedError before removing this xfail."
    ),
)
def test_python_exception_should_eventually_be_task_failed_error():
    def boom(value):
        raise RuntimeError(f"boom: {value}")

    with dar.init(worker_count=1) as runtime:
        ref = runtime.submit(dar.Agent("boom", boom), 7)

        with pytest.raises(dar.TaskFailedError, match="boom: 7"):
            ref.get()