from __future__ import annotations

import math

import pytest

import dar
import dar._dar as native


def test_empty_resource_request():
    request = native.ResourceRequest()
    assert isinstance(request, native.ResourceRequest)


def test_valid_resource_request():
    request = native.ResourceRequest({"CPU": 2.0, "GPU": 0.5})
    assert isinstance(request, native.ResourceRequest)


def test_agent_options_convert_to_native_resource_request():
    options = dar.AgentOptions(resources={"CPU": 1.0})
    request = options._native_resources()
    assert isinstance(request, native.ResourceRequest)


def test_resource_name_must_be_string():
    with pytest.raises(TypeError, match="resource names must be strings"):
        native.ResourceRequest({123: 1.0})


def test_resource_quantity_must_be_numeric():
    with pytest.raises(TypeError, match="resource quant"):
        native.ResourceRequest({"CPU": object()})


@pytest.mark.parametrize("quantity", [-1.0, math.inf, -math.inf, math.nan])
def test_native_resource_validation_rejects_invalid_quantities(quantity):
    with pytest.raises(dar.InvalidArgumentError):
        native.ResourceRequest({"CPU": quantity})