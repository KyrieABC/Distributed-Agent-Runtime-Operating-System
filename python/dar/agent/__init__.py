"""
Public agent API for the Distributed Agent Runtime
"""

from .agent import Agent as Agent
from .options import AgentOptions as AgentOptions

__all__ = ["Agent","AgentOptions"]

"""
Now use:
  from dar.agent import Agent, AgentOptions
Instead of:
  from dar.agent.agent import Agent         --->           (private implementation)
  from dar.agent.options import AgentOptions    -->
"""

"""
AgentOptions:  Python SDK configuration 
C++ TaskSpec:  Runtime task representation

resources(AO) = ResourceRequest(C++)
Agent.name(AO) = TaskSpec.name
Agent.handler(AO) = TashHandler/PythonCallback
"""

