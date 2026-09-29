from __future__ import annotations

import pytest

import dar


def test_init_returns_started_runtime():
    runtime = dar.init(worker_count=1)
    try:
        agent = dar.Agent("identity", lambda value: value)
        assert runtime.submit(agent, 7).get() == 7
    finally:
        runtime.shutdown()


def test_runtime_requires_start_before_submit():
    runtime = dar.Runtime(worker_count=1)
    agent = dar.Agent("identity", lambda value: value)

    with pytest.raises(RuntimeError, match="must be started"):
        runtime.submit(agent, 1)

    runtime.shutdown()


def test_start_is_idempotent():
    runtime = dar.Runtime(worker_count=1)
    try:
        runtime.start()
        runtime.start()

        ref = runtime.submit(dar.Agent("identity", lambda value: value), 5)
        assert ref.get() == 5
    finally:
        runtime.shutdown()


def test_shutdown_is_idempotent():
    runtime = dar.init(worker_count=1)
    runtime.shutdown()
    runtime.shutdown()


def test_submit_after_shutdown_is_rejected():
    runtime = dar.init(worker_count=1)
    runtime.shutdown()

    with pytest.raises(RuntimeError, match="shut down|shutdown"):
        runtime.submit(dar.Agent("identity", lambda value: value), 1)


def test_context_manager_starts_and_shuts_down_runtime():
    with dar.Runtime(worker_count=1) as runtime:
        ref = runtime.submit(dar.Agent("identity", lambda value: value), 11)
        assert ref.get() == 11

    with pytest.raises(RuntimeError, match="shut down|shutdown"):
        runtime.submit(dar.Agent("identity", lambda value: value), 12)


def test_submit_requires_agent():
    runtime = dar.init(worker_count=1)
    try:
        with pytest.raises(TypeError, match="agent"):
            runtime.submit(lambda x: x, 1)  # type: ignore[arg-type]
    finally:
        runtime.shutdown()