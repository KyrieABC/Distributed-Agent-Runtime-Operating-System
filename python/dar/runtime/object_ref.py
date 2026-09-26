"""
Python SDK

ObjectRef 
-> _native_ref
-> NativeExecutionRef
-> get()/cancel()/status()/task_id 
-> C++ Runtime
-> TaskManager
"""
# c++ dar::ObjectRef -> Logical runtime value identity
# Python _dar.NativeExecutionRef -> Low-level native task/result handle
# Python dar.runtime.ObjectRef -> public high-level Python result handle

from __future__ import annotations

import pickle
from typing import Any

from .. import _dar

class ObjectRef:
    """
    High-level Python handle to the result of a submitted DAR task
    
    ObjectRef does not own task state. 
    The native DAR runtime and TaskManager remain authoritative for execution state, results, failures, and cancellation
    
    Underlying NativeExecutionRef addresses the task using its native Runtime and TaskID
    """
    def __init__(self, native_ref: _dar.NativeExecutionRef) -> None:
        if not isinstance(native_ref, _dar.NativeExecutionRef):
            raise TypeError("native_ref must be a dar._dar.NativeExecutionRef")
        self._native_ref = native_ref

    def get(self) -> Any:
        """
        wait for task completion and return the deserialized result
        
        NativeExecutionRef.get() performs the blocking wait and returns the serialized result as bytes
        The Python SDK owns deserialization of those bytes back into a Python object
        """
        payload = self._native_ref.get()
        
        # loads(): Accepts bytes-like data
        return pickle.loads(payload)
    
    def cancel(self) -> None:
        """
        Request cancellation of this task
        
        Cancellation semantics remain owned by the native DAR runtime
        Running Python tasks use cooperative cancellation
        """
        self._native_ref.cancel()
        
    def status(self) -> str:
        """
        Return the current DAR execution lifecycle state
        """
        return self._native_ref.status()
    
    @property
    def task_id(self) -> str:
        """
        Return the logical DAR TaskID as its canonical hexadecimal representation
        """
        return self._native_ref.task_id
    
    def __repr__(self) -> str:
        return f"ObjectRef(task_id = {self.task_id!r})"
    
__all__ = ["ObjectRef"]
        
        
        
        
    