from __future__ import annotations

import dar
import dar._dar as native


def test_native_extension_imports():
    assert native.NativeRuntime is not None
    assert native.NativeExecutionRef is not None
    assert native.ResourceRequest is not None


def test_public_api_exports_expected_symbols():
    expected = {
        "Agent",
        "AgentOptions",
        "Runtime",
        "ObjectRef",
        "init",
        "DarError",
        "InvalidArgumentError",
        "NotFoundError",
        "AlreadyExistsError",
        "FailedPreconditionError",
        "UnavailableError",
        "ResourceExhaustedError",
        "CancelledError",
        "DeadlineExceededError",
        "InternalError",
        "TaskFailedError",
    }

    assert expected.issubset(set(dar.__all__))


def test_exception_hierarchy_is_native():
    assert issubclass(dar.InvalidArgumentError, dar.DarError)
    assert issubclass(dar.ResourceExhaustedError, dar.DarError)
    assert issubclass(dar.CancelledError, dar.DarError)
    assert issubclass(dar.InternalError, dar.DarError)
    assert issubclass(dar.TaskFailedError, dar.DarError)