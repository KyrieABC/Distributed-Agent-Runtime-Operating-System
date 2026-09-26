"""
Task configuration for the Distribtued Agent Runtime

This module defines the public Python representation of options
associated with a submitted DAR task

Resource validation remains owned by the native C++ runtime
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Mapping

from .. import _dar

@dataclass(frozen=True, slots=True)
class AgentOptions:
    """
    Options controlling execution of a DAR task
    
    parameters:
    resouces: Logical resource required by 1 execution of the task
      - Ex: {"CPU":1.0}, {"CPU":2.0,"GPU": 1.0}, {"GPU":0.5}
      - Resource names adn quantities are validated by DAR's native ResourceSet implementation when task is submitted
    name: 
      - Optional human-readable task name
      - If omitted, the agent layer may derive the name from the Python callable being submitted
    """
    resources: Mapping[str,float] = field(default_factory=dict)
    name: str | None = None
    
    def _native_resources(self) -> _dar.ResourceRequest:
        """
        convert this public Python resource descrption into DAR's native ResourceRequest
        
        This method performs representation conversion only validation remains authoritative in the C++ resource layer
        """
        return _dar.ResourceRequest(dict(self.resources))
    
# __all__ is a special list of strings defiend at module level that controls what gets imported
# Hide helper functions / Define Public API
__all__ = ["AgentOptions"]
        
    
    
