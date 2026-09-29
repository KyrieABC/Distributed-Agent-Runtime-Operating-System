from __future__ import annotations

import dar


def test_get_releases_gil_so_worker_can_enter_python():
    with dar.init(worker_count=1) as runtime:
        ref = runtime.submit(
            dar.Agent("echo", lambda value: value),
            {"ok": True},
        )

        # If NativeExecutionRef.get() retained the GIL while waiting,
        # the native worker could not acquire it in PythonCallback::Invoke().
        assert ref.get() == {"ok": True}


def test_shutdown_releases_gil_while_draining_python_callbacks():
    runtime = dar.init(worker_count=2)

    refs = [
        runtime.submit(
            dar.Agent("increment", lambda value: value + 1),
            i,
        )
        for i in range(200)
    ]

    # Shutdown drains accepted work. If the binding retained the GIL here,
    # workers waiting to enter Python could deadlock the shutdown.
    runtime.shutdown()

    assert [ref.get() for ref in refs] == [i + 1 for i in range(200)]