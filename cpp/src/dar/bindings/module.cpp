/**
 * C++ runtime operation
 * -> Status
 * -> status.ok()
 *   -> Yes: return
 *   -> No: 
 *     -> status.code()
 *     -> ThrowStatus()
 *     -> Python exception type
 */
/**
 * dar_core
 * -> Status/StatusCode
 * --Language Boundary--
 * _dar 
 * -> Python exceptions
 */

#include "dar/bindings/binding_types.h"

#include <string>

#include <pybind11/pybind11.h>

namespace dar::bindings
{
    namespace py = pybind11;

    //void BindResources(py::module_& module);

    // Python exception objects
    // initialized when Python imports `_dar`
    /**
     * Exception Hierarchy: (by .ptr() each level)
     * Exception
     * -> DarError
     *   -> InvalidArgumentError
     *   -> NotFoundError
     *   -> ALreadyExistsError
     *   -> FailedPreconditionError
     *   -> UnavailableError
     *   -> ResourceExhaustedError
     *   -> CancelledError
     *   -> DeadlineExceededError
     *   -> InternalError 
     *   -> TaskFailedError
     * 
     * ThrowStatus() selects one of these exception classes based on DARs StatusCode
     * 
     * Store python exception TYPE objects rather than defining separate c++ exception classes because DAR already have its own error abstraction:
     *  Status{StatusCode code; std::string message;}
     * -> Python exceptions are an adapter at language rather than a replacement for Status
     */
    namespace
    {
        py::object dar_error;

        py::object invalid_argument_error;
        py::object not_found_error;
        py::object already_exists_error;
        py::object failed_precondition_error;
        py::object unavailable_error;
        py::object resource_exhausted_error;
        py::object cancelled_error;
        py::object deadline_exceeded_error;
        py::object internal_error;
        py::object task_failed_error;

        // Create 1 Python exception class
        /**
         * Equivalent Python:
         *   class InvalidArgumentError(DarError): pass
         * 
         * PyErr_NewException returns a new Python exception TYPE object
         * 
         * `module_name` is expected to be: dar._dar
         * 
         * complete Python type name becomes:
         * dar._dar.InvalidArgumentError
         */
        py::object CreateException(
            py::module_& module,
            const char* name, 
            PyObject* base)
        {
            /**
             * __name__ (entry point guard)
             * If run directly: __name__ == __main__
             * If imported: __name__ == file's actual name
             */
            const std::string module_name = py::str(module.attr("__name__"));
            const std::string qualified_name = module_name + "." + name;

            PyObject* exception_type = PyErr_NewException(qualified_name.c_str(), base, nullptr);

            if(exception_type == nullptr)
            {
                throw py::error_already_set();
            }

            // PyErr_NewException returns a new reference
            // reinterpret_steal transfers ownership of that reference into a py::object so RAII manages it correctly
            py::object exception = py::reinterpret_steal<py::object>(exception_type);

            // Publish it into the native module:
            // __dar.InvalidArgumentError
            module.attr(name) = exception;
            return exception;
        }

        // Raise 1 registered Python exception
        /**
         * Setting Python's error indicator alone is not enough for a normal pybind11-bound function
         * After setting it, throw error_already_set so pybind11 propagates the Python exception back through the Python call boundary
         */
        [[noreturn]] void RaisePythonException(const py::object& exception_type, const std::string& message)
        {
            PyErr_SetString(exception_type.ptr(), message.c_str());

            throw py::error_already_set();
        }
    }

    // Status -> Python exception translation
    void ThrowStatus(const Status& status)
    {
        // Success is not exceptional
        if(status.ok())
        {
            return;
        }

        /**
         * Python will only receives human-readable message
         * 
         * Python exception TYPE already communicates the category:
         *   InvalidArgumentError: "message itself"
         */
        const std::string message(status.message());

        switch(status.code())
        {
            case StatusCode::kInvalidArgument:
                RaisePythonException(invalid_argument_error, message);
            case StatusCode::KNotFound:
                RaisePythonException(not_found_error, message);
            case StatusCode::KAlreadyExists:
                RaisePythonException(already_exists_error, message);
            case StatusCode::KFailedPrecondition:
                RaisePythonException(failed_precondition_error, message);
            case StatusCode::KUnavailable:
                RaisePythonException(unavailable_error, message);
            case StatusCode::KResourceExhausted:
                RaisePythonException(resource_exhausted_error, message);
            case StatusCode::Kcancelled:
                RaisePythonException(cancelled_error, message);
            case StatusCode::KDeadlineExceeded:
                RaisePythonException(deadline_exceeded_error, message);
            case StatusCode::KInternal:
                RaisePythonException(internal_error, message);
            case StatusCode::kOk:
                return;
        }

        /**
         * Defensive fallback
         * 
         *  if a future StatusCode is added but this swtich is not updated, Python still receives a DAR exception
         * rather tha siliently losing the error or exposing an unrelated native exception
         */
        RaisePythonException(dar_error, message.empty()?"Unknown DAR runtime error":message);
    }

    // Exception Hierarchy registration
    void RegisterExceptions(py::module_& module)
    {
        // Root DAR exception: class DarError(Exception): pass
        dar_error = CreateException(module, "DarError", PyExc_Exception);

        // Every runtime-specific error derives from DarError
        /**
         * Python users can either catch a precise error:
         *     except ResourceExhaustedError:
         * or every DAR runtime error:
         *     except DarError:
         */
        invalid_argument_error = CreateException(module, "InvalidArgumentError", dar_error.ptr());

        not_found_error = CreateException(module, "NotFoundError", dar_error.ptr());

        already_exists_error = CreateException(module, "AlreadyExistsError", dar_error.ptr());

        failed_precondition_error = CreateException(module, "FailedPreconditionError", dar_error.ptr());

        unavailable_error = CreateException(module, "UnavailableError", dar_error.ptr());

        resource_exhausted_error = CreateException(module, "ResourceExhaustedError", dar_error.ptr());

        cancelled_error = CreateException(module, "CancelledError", dar_error.ptr());

        deadline_exceeded_error = CreateException(module, "DeadlineExceededError", dar_error.ptr());

        internal_error = CreateException(module, "InternalError", dar_error.ptr());

        // TaskFailedError does not currently respond to a StatusCode
        // It is reserved for higher-level Python execution boundary where
        // a task reaches Failed because its Python handler itself failed
        task_failed_error = CreateException(module, "TaskFailedError", dar_error.ptr());

    }
}

// _dar Python module
/**
 * This macro defines the CPython initialization entry point for:
 *   import dar._dar
 * The first argument MUST match the native extension target:
 *   pybind11_add_module(_dar ...)
 * from cpp/MakeLists.txt
 */
/**
 * import dar._dar
 * -> PYBIND11_MODULE
 * -> RegisterExceptions()
 * -> BindResources()
 * -> ResourceRequest available
 * 
 * Order matters because ResourceRequestFromDict() call ThrowStatus() (expect exception objects established in RegisterExpcetion())
 */
PYBIND11_MODULE(_dar, module)
{
    module.doc() = "Native C++ execution boundary for the 'Distributed Agent Runtime'";

    // Exception Hierarchy
    dar::bindings::RegisterExceptions(module);

    // This is just declaration
    dar::bindings::BindResources(module);
    dar::bindings::BindExecution(module);
    dar::bindings::BindRuntime(module);
    /**
     * Future phase 2 binding registration
     * resource_bindings.cpp -> ResourceRequest
     * execution_bindings.cpp -> NativeExecutionRef
     * runtime_bindings.cpp -> NativeRuntime
     * 
     * Do NOT register them here until those binding functions have been implemented
     */
}