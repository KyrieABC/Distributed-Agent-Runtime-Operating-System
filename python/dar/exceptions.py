"""
To run:
  source .venv/bin/activate
  should see: (.venv) -> Virtual environment
"""

"""
Public exceptions hierarchy for the Distributed Agent Runtime

The actual exception classes are created by the 
native `_dar` extension. 
The module re-exports those same class objects as part of DAR's stable public Python API

Users should import these exceptions from `dar.exceptions` rather than depending direclty on the private native extension
"""

"""
do not do (Do not define Python class again):
class DarError(Exception): pass
class CancelledError(DarError): pass

-> (Different Python types)
dar._dar.CancelledError != dar.exceptions.CancelledError
 
we want user to: 
from dar.exceptions import CancelledError
Not:
from dar._dar import CancelledError
 - _dar is intended to be an implementation details
"""

"""
c++ module.cpp --PyErr_NewException(...)-> dar._dar.CancelledError (source of truth of class) --Python import/re-export -> dar.exceptions.CancelledError (public name) -> User

When C++ raises an error:
Status::cancelled(...)
-> ThrowStatus()
-> RaisePythonException(cancelled_error,...)
-> (Actual object type) dar._dar.CancelledError
-> (User catches) dar.exceptions.CancelledError
"""


from ._dar import(
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
    TaskFailedError as TaskFailedError,
)

__all__ = [
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
    "TaskFailedError",
]
