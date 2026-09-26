from __future__ import annotations

from collections.abc import Callable, Mapping


# ---------------------------------------------------------------------------
# Exceptions
# ---------------------------------------------------------------------------

class DarError(RuntimeError):
    """Base exception for errors reported by the native DAR runtime."""


class InvalidArgumentError(DarError):
    """An argument does not satisfy the native DAR contract."""


class NotFoundError(DarError):
    """The requested DAR object does not exist."""


class AlreadyExistsError(DarError):
    """The requested DAR object already exists."""


class FailedPreconditionError(DarError):
    """The operation cannot run in the current runtime state."""


class UnavailableError(DarError):
    """The requested runtime operation is temporarily unavailable."""


class ResourceExhaustedError(DarError):
    """The runtime does not have sufficient resources for the operation."""


class CancelledError(DarError):
    """The operation or task was cancelled."""


class DeadlineExceededError(DarError):
    """The operation exceeded its deadline."""


class InternalError(DarError):
    """An internal DAR runtime error occurred."""


class TaskFailedError(DarError):
    """A submitted task failed during execution."""


# ---------------------------------------------------------------------------
# Resources
# ---------------------------------------------------------------------------

class ResourceRequest:
    """Native DAR resource requirements."""

    def __init__(
        self,
        resources: Mapping[str, float] = ...,
    ) -> None: ...

    def __repr__(self) -> str: ...


# ---------------------------------------------------------------------------
# Execution
# ---------------------------------------------------------------------------

class NativeExecutionRef:
    """
    Native handle to one logical DAR task.

    Instances are returned by NativeRuntime.submit() and are not directly
    constructed by the public Python SDK.
    """

    def get(self) -> bytes:
        """Wait for task completion and return its serialized result."""
        ...

    def cancel(self) -> None:
        """Request cooperative cancellation of the task."""
        ...

    def status(self) -> str:
        """Return the current native execution-state name."""
        ...

    @property
    def task_id(self) -> str:
        """Return the logical DAR task ID as a hexadecimal string."""
        ...

    def __repr__(self) -> str: ...


# ---------------------------------------------------------------------------
# Runtime
# ---------------------------------------------------------------------------

class NativeRuntime:
    """Python binding around the native C++ DAR runtime."""

    def __init__(
        self,
        worker_count: int,
        resource_per_worker: ResourceRequest,
    ) -> None: ...

    def start(self) -> None:
        """Start workers and the native scheduler."""
        ...

    def shutdown(self) -> None:
        """Drain accepted work and shut down the native runtime."""
        ...

    def submit(
        self,
        callable: Callable[[bytes], bytes],
        payload: bytes,
        name: str,
        resources: ResourceRequest,
    ) -> NativeExecutionRef:
        """Submit serialized Python work to the native DAR runtime."""
        ...