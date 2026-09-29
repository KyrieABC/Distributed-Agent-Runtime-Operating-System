from __future__ import annotations

import dar


def test_10000_local_python_tasks_cross_binding_boundary():
    count = 10_000

    with dar.init(
        worker_count=8,
        resources_per_worker={"CPU": 1.0},
    ) as runtime:
        agent = dar.Agent(
            "increment",
            lambda value: value + 1,
            dar.AgentOptions(resources={"CPU": 1.0}),
        )

        refs = [runtime.submit(agent, i) for i in range(count)]
        results = [ref.get() for ref in refs]

        assert results[0] == 1
        assert results[-1] == count
        assert results == list(range(1, count + 1))
        assert all(ref.status().upper() == "SUCCEEDED" for ref in refs)