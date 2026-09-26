"""
High-level Python runtime for the Distributed Agent Runtime

This module owns the Python-side execution boundary:
  - pickel serialization
  - NativeRuntime
  - C++ scheduler / worker
  - Python handler
  - pickle serialization
  - ObjectRef.get()
  - Python result
  
Scheduling, resource accounting, task lifecycle, cancellation, and execution state remain authoritative in the native C++ runtime
"""

from __future__ import annotations

import pickle
from typing import Any

from .. import _dar
from ..agent.agent import Agent
from .object_ref import ObjectRef

class Runtime:
    """
    High-level Python interface to DAR's native runtime
    
    Runtime owns 1 native `_dar.NativeRuntime` and provides:
      - runtime lifecycle management
      - Agent submission
      - Python argument serialization
      - Python result serialization
      - context-manager support
      
    Native C++ runtime remains authoritative for scheduling, resources, execution state, cancellation and submission
    """
    
    def __init__(self, 
                worker_count: int = 1 ,
                resources_per_worker: dict[str,float] | None = None, 
                ):
        """
        Create a DAR runtime
        
        Parameters;
          - worker_count: Number of active DAR workers
          - resources_per_worker: Logical resources available to each worker  
        Ex: {"CPU": 4.0, "GPU": 1.0}
        
        Resource validation is delegated to DAR's native ResourceRequest/ResourceSet implementation
        """
        if resources_per_worker is None:
            resources_per_worker = {}
        native_resources = _dar.ResourceRequest(resources_per_worker)
        
        self._native_runtime = _dar.NativeRuntime(
            worker_count,
            native_resources
        )
        
        self._started = False
        self._shutdown = False
        
    def start(self) -> None:
        """
        Start the native DAR runtime
        
        Calling start() more than once through this Python Runtime object is harmless
        """
        if self._shutdown:
            raise RuntimeError("cannot start a Runtime after it has been shut down")
        
        if self._started:
            return
        
        self._native_runtime.start()
        self._started = True
        
    def shutdown(self) -> None:
        """
        Drain accepted work and shutdown the native runtime
    
        Calling shutdown() more than once through this Python Runtime object is harmless
        """
        if self._shutdown:
            return
        
        self._native_runtime.shutdown()
        
        self._started = False
        self._shutdown = True
        
    def submit(
        self,
        agent: Agent,
        *arg: Any,
        **kwargs: Any
    ) -> ObjectRef:
        """
        Submit an Agent for asynchronous execution
        
        Positional and keyword arguments are serialized in Python before crossing the native boundary
        
        Returns: ObjectRef (high-level handle to the submitted logical DAR task)
        """
        if not isinstance(agent, Agent):
            raise TypeError("agent must be a dar.agent.Agent")
        if self._shutdown:
            raise RuntimeError("cannot submit work to a runtime that has been shutdown")
        if not self._started:
            raise RuntimeError("Runtime must be started before submitting work")
        
        # Serialize 1 invocation as a single Python value
        # The native runtime treats this payload as opaque bytes
        # -> Its structure belongs entirely to the Python SDk
        payload = pickle.dumps(
            (arg, kwargs),
            protocol = pickle.HIGHEST_PROTOCOL,
        )
        
        # Native callback receives serialized bytes and must return serialized bytes
        # PythonCallback::Invoke() in C++ owns the GIL acquisition and invokes this callable from the Native worker thread
        def invoke(serialized_payload: bytes) -> bytes:
            call_args, call_kwargs = pickle.loads(serialized_payload)
            
            result = agent.handler(*call_args, **call_kwargs)
            
            return pickle.dumps(result, protocol=pickle.HIGHEST_PROTOCOL)
        
        native_ref = self._native_runtime.submit(
            invoke,
            payload,
            agent.name,
            agent.options._native_resources()
        )
        
        return ObjectRef(native_ref)
    
    def __enter__(self) -> Runtime:
        # Start this runtime and return it for use in a "with" block
        self.start()
        return self
    
    def __exit__(
        self,
        exc_type: object,
        exc_value: object,
        traceback: object
    ) -> bool:
        """
        Shut down the runtime when leaving a "With" block
        
        Returning False ensures exceptions raised inside the block continue propagating normally
        """
        self.shutdown()
        return False

__all__ = ["Runtime"]
               
