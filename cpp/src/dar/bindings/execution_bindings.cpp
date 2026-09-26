#include "dar/bindings/binding_types.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include <pybind11/pytypes.h>

#include "dar/common/id.h"
#include "dar/common/status.h"
#include "dar/core/lifecycle.h"
#include "dar/runtime/runtime.h"
#include "dar/runtime/task_manager.h"

namespace dar::bindings
{
    namespace py = pybind11;

    // NativeExecutionRef
    /**
     * Python-visible handle to one logical DAR task
     * 
     * NativeExectionRef does NOT own task state
     * 
     * TaskManager remains the authoritative owner of:
     *   - lifecycle state
     *   - result 
     *   - terminal status
     *   - cancellation token
     *   - worker assignment
     *   - execution attempt
     */
    // THis class stores only enough information to address that state through Runtime: Runtime + TaskID
    // NativeExecutionRef is intentionally NOT named ObjectRef
    // DAR already has dar::ObjectRef representing logical runtime value identity
    // -> THis binding represents an asynchronous execution/result handle
    
    NativeExecutionRef::NativeExecutionRef(std::shared_ptr<Runtime> runtime, TaskID task_id):task_id_(task_id), runtime_(runtime){}


    py::bytes NativeExecutionRef::Get()
    {
        EnsureRuntime();

        std::string result;
        Status status;

        {
            // Runtime::GetResult() may block indefinitely
            // No Python objects may be touched while this scoped release is active
            py::gil_scoped_release release;

            status = runtime_->GetResult(task_id_, &result);
        }

            // GIL has been reacquired here
            // ThrowStatus() uses Python's exception machinery, so it must execute while the GIL is held
            ThrowStatus(status);

            // Phase 2 transports serialized Python values as bytes
            // Python's higher-level ObjectRed.get() will eventually deserialize this payload
            return py::bytes(result.data(), result.size());
    }


    void NativeExecutionRef::Cancel()
    {
        EnsureRuntime();

        const Status status = runtime_->Cancel(task_id_);

        ThrowStatus(status);
    }

    std::string NativeExecutionRef::StatusName() const
    {
        EnsureRuntime();

        TaskSnapshot snapshot;

        const Status status = runtime_->GetStatus(task_id_,&snapshot);

        ThrowStatus(status);
        
        return std::string(ExecutionStateName(snapshot.state));
    }


    std::string NativeExecutionRef::TaskId() const
    {
        return task_id_.Hex();
    }


    void NativeExecutionRef::EnsureRuntime() const
    {
        if(!runtime_)
        {
            throw std::runtime_error("NativeExecutionRef has no runtime");
        }   
    }

    // Python binding registration
    void BindExecution(py::module_& module)
    {
        py::class_<NativeExecutionRef>(module, "NativeExecutionRef")
        // NativeExecutionRef should NOT be directly constructible from Python
        // Use obtain 1 from: NativeRuntime.submit(...)
        // get()
        .def(
            "get",
            &NativeExecutionRef::Get,
            R"doc(
            Wait for task completion and return serialized result
            )doc"
        )
        // cancel
        .def(
            "cancel",
            &NativeExecutionRef::Cancel,
            R"doc(
            Request cancellation of this task
            Cancellation of running work is cooperative
            )doc"
        )
        //  status()
        .def(
            "status",
            &NativeExecutionRef::StatusName,
            R"doc(
            Return the current DAR execution sate
            )doc"
        )
        // task_id
        .def_property_readonly(
            "task_id",
            &NativeExecutionRef::TaskId,
            R"doc(
            Return the logical DAR task ID as a hexadecimal string
            )doc"
        )
        // repr
        .def(
            "__repr__",
            [](const NativeExecutionRef& ref)
            {
                return "nativeExecutionRef(task_id = " + ref.TaskId() + ")";
            }
        );
    }
}