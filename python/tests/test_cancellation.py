from __future__ import annotations

import threading

import pytest

import dar


def test_queued_task_can_be_cancelled():
    entered = threading.Event()
    release = threading.Event()

    def blocker(value):
        entered.set()
        if not release.wait(timeout=5):
            raise RuntimeError("test blocker timed out")
        return value

    with dar.init(
        worker_count=1,
        resources_per_worker={"CPU": 1.0},
    ) as runtime:
        resources = dar.AgentOptions(resources={"CPU": 1.0})

        first = runtime.submit(dar.Agent("blocker", blocker, resources), 1)
        assert entered.wait(timeout=5), "first task never entered Python callback"

        second = runtime.submit(
            dar.Agent("queued", lambda value: value, resources),
            2,
        )

        second.cancel()
        release.set()

        assert first.get() == 1

        with pytest.raises(dar.CancelledError):
            second.get()

        assert second.status().upper() == "CANCELLED"


def test_cancel_is_forwarded_to_native_runtime():
    entered = threading.Event()
    release = threading.Event()

    def blocker(value):
        entered.set()
        release.wait(timeout=5)
        return value

    with dar.init(worker_count=1) as runtime:
        ref = runtime.submit(dar.Agent("blocker", blocker), 1)
        assert entered.wait(timeout=5)

        ref.cancel()
        release.set()

        with pytest.raises(dar.CancelledError):
            ref.get()

        assert ref.status().upper() == "CANCELLED"