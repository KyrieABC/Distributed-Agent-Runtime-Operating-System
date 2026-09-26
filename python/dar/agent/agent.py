from __future__ import annotations

from collections.abc import Callable
from typing import Any

from .options import AgentOptions

class Agent:
    """
    Declarative description of a Python function executable by DAR
    
      
    Agent does not:
      - submit work
      - serialize arguments
      - schedule tasks
      - interact directly with the native runtime
      -> Those belongs to the runtime layer
    """
    # Callable: identify and type-hint objects that can be called with a __call__ method
    # Callable[..., Any] (...)accept argument of any type and quantity
    # Callable[..., Any] (Any)return a value of any type
    # TaskOptions | None: accept TaskOptions object or None
    # -> (TaskOptions is an optional parameter by `= None`)
    # `-> None`: This method returns no value
    def __init__(self, name:str, handler: Callable[...,Any], options: AgentOptions | None = None,)->None:
        """
        Create an Agent
        An agent contains only Python-level task metadata
        
        Parameters:
      - name: stable human-readable name
      - handler: Python callable containing the user's task logic
      - options: Default execution options for this agent. If omitted, an empty TaskOptions instance is created  
        """
        if not isinstance(name, str):
            raise TypeError("Agent name must be a string")
        if not name:
            raise ValueError("agent name must not be empty")
        if not callable(handler):
            raise TypeError("agent handler must be callable")
        
        self._name = name
        self._handler = handler
        self._options = options if options is not None else AgentOptions()
        
    @property
    def name(self) -> str:
        return self._name
        
    @property
    def handler(self) -> Callable[..., Any]:
        return self._handler
        
    @property
    def options(self) -> AgentOptions:
        return self._options
        
    def __repr__(self) -> str:
        return (
            f"Agent("
            f"name={self._name!r}, "
            f"handler = {self._handler!r}, "
            f"options= { self._options!r}"
            f")")
            
__all__ = ["Agent"]