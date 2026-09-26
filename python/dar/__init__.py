from __future__ import annotations
"""
Distributed Agent Runtime (DAR) Python SDK

The top-level package exposes the primary user-facing API for defining
agents, creating runtimes, submitting work, retrieveing results, and handling DAR errors
"""

"""
Produces a clean public SDK:
import dar
def add(x: int, y: int) -> int:
  return x+y

agent = dar.Agent(name="add", handler= add, options = dar.AgentOptions(resources={"CPU":1.0}))

runtime = dar.init(worker_count=4, resoruces_per_worker={"CPU":3.0})

ref = runtime.submit(agent, 20, 22)

print(ref.task_id)
print(ref.get())

runtime.shutdown()
"""

 
from .agent import Agent as Agent
from .agent import AgentOptions as AgentOptions

from .runtime import ObjectRef as ObjectRef
from .runtime import Runtime as Runtime

from .exceptions import (
    DarError as DarError,
    InvalidArgumentError as InvalidArgumentError,
    NotFoundError as NotFoundError,
    AlreadyExistsError as AlreadyExistsError,
    FailedPreconditionError as FailedPreconditionError,
    UnavailableError as UnavailableError,
    ResourceExhaustedError as ResourceExhaustedError,
    CancelledError as CancelledError,
    DeadlineExceededError as DeadlineExceededError,
    InternalError as InternalError,
    TaskFailedError as TaskFailedError
)

def init(
    worker_count: int = 1,
    resources_per_worker: dict[str, float] | None = None
) -> Runtime:
    """
    Create and start a DAR Runtime
    
    This is a convenience API equivalent to: 
        runtime = Runtime(
            worker_count = worker_count,
            resources_per_worker = resources_per_worker
        )
        
    Parameter:
      - worker_count: Number of native DAR workers
      - resources_per_worker: Logical resources available to each worker
      Ex: {"CPU": 1.0}
      
    returns: Runtime (a started DAR runtime)
    """
    runtime = Runtime(
        worker_count=worker_count,
        resources_per_worker=resources_per_worker
    )
    
    runtime.start()
    
    return runtime

__all__ =  [
    # Agent API
    "Agent",
    "AgentOptions",
    
    # Runtime API
    "Runtime",
    "ObjectRef",
    "init",
    
    # Exception API
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
    "TaskFailedError"
]
    
"""
Hierarchy:
dar 
-> Agent
-> AgentOptions
-> Runtime
-> ObjectRef
-> init()

-> DarError
  -> InvalidArgumentError
  -> ...
  
-> agent
  -> Agent
  -> AgentOptions
  
-> runtime
  -> Runtime
  -> ObjectRef
"""
