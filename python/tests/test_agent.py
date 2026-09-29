from __future__ import annotations

import pytest

import dar


def identity(value):
    return value


def test_agent_stores_definition():
    options = dar.AgentOptions(resources={"CPU": 1.0})
    agent = dar.Agent("identity", identity, options)

    assert agent.name == "identity"
    assert agent.handler is identity
    assert agent.options is options


def test_agent_creates_default_options():
    agent = dar.Agent("identity", identity)

    assert isinstance(agent.options, dar.AgentOptions)
    assert dict(agent.options.resources) == {}


def test_agent_rejects_non_string_name():
    with pytest.raises(TypeError, match="name must be a string"):
        dar.Agent(123, identity)  # type: ignore[arg-type]


def test_agent_rejects_empty_name():
    with pytest.raises(ValueError, match="must not be empty"):
        dar.Agent("", identity)


def test_agent_rejects_non_callable_handler():
    with pytest.raises(TypeError, match="handler must be callable"):
        dar.Agent("bad", None)  # type: ignore[arg-type]