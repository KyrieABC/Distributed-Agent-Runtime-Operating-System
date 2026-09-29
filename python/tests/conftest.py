from __future__ import annotations

"""
 To test
source .venv/bin/activate
python -m pip install -U pytest
python -m pytest python/tests -q
"""

import pytest

import dar

# Decorate used to define a reusable setup functoin for test
"""
import pytest

# Define the fixture
@pytest.fixture
def sample_user():
    return {"username": "johndoe", "role": "admin"}

# Use the fixture by matching its name as an argument
def test_user_role(sample_user):
    assert sample_user["role"] == "admin"

"""

@pytest.fixture
def started_runtime():
    runtime = dar.init(worker_count=2, resources_per_worker={"CPU": 1.0})
    try:
        # save the error returned by the object initialization
        yield runtime
    # Finally block always executes
    finally:
        runtime.shutdown()