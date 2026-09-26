#pragma once

#include <memory>
#include <string>
#include <utility>

#include <pybind11/pybind11.h>

#include "dar/common/status.h"
#include "dar/core/task_spec.h"
#include "dar/runtime/runtime_context.h"
#include "dar/worker/executor.h"
#include "dar/runtime/runtime.h"

namespace dar::bindings
{
    namespace py = pybind11;

    // Status -> Python exception translation

    /**
     * Convert a failed DAR Status into the corresponding Python exception
     * 
     * Contract:
     *   status.ok() == true -> return normally 
     *   status.ok() == false -> throws the Python exception associated with status.code()
     * 
     * This is the single translation boundary between DAR's runtime-level Status abstraction and the public Python SDK
     * 
     * The implementation belongs in module.cpp because that file owns creation/registration of the Python exception classes 
     */
    void ThrowStatus(const Status& status);

    // PythonCallBack
    /**
     * Object/mechanism that manages lifecycle  or binding context of a callable(function or method) wihtout executing it directly
     * 
     * PythonCallBack deliberately lives in dar::bindings not DAR core runtime
     * The runtime continues to understand only: TaskSpec, TaskHandler, RuntimeContext, Status
     * it does NOT understand pybind11 or Python objects
     *  
     * Ownership model:
     * Python Agent -> py::function -> PythonCallBack-> shared_ptr<PythonCallBack> -> TaskHandler -> C++ Runtime
     * 
     * PythonCallback is intended to be owned through shared_ptr 
     * -> allows the TaskHandler installed into the runtime to retain the Python callable for as long as the accepted task may need it
     * 
     */
    /**
     * TaskSpec -> pure DAR metadata
     * TaskHandler -> shared_ptr<PythonCallback> 
     *                     -> Python callable
     *                     -> serialized Python payload
     */
    class PythonCallback final
    {
    public:
        /**
         * Construct a callback from a Python callable
         * 
         * The caller must hold the Python GIL(Global Interpreter Lock: mutex used by default Python interpret ensure 1 thread executes python bytecode at a time) while constructing this object 
         * because ownershp of a Python object is being transferred into py::function
         */
        explicit PythonCallback(py::function callable,std::string payload)
        :callable_(std::move(callable)), payload_(payload) {}

        PythonCallback(const PythonCallback&) = delete;
        PythonCallback& operator=(const PythonCallback&) = delete;

        PythonCallback(PythonCallback&&) = delete;
        PythonCallback& operator=(PythonCallback&&) = delete;

        /**
         * Execute the Python callable using the exact TaskHandler contract in phase 1:
         * Status(const TaskSpec&, RuntimeContext&, std::string*)
         *
         * Invoke() will acquire the Python GIL,
         * construct Python bytes from the task payload,
         * invoke callable_(payload),
         * require the callback result to be bytes,
         * copy those bytes into 'out',
         * translate Python failures into DAR Status values
         * (implemented by runtime_bindings.cpp)
         */
        Status Invoke(const TaskSpec& spec, RuntimeContext& context, std::string* out);
    private:
        /** Strong reference to the developer's Python callable
         * 
         * This is intentionally stored here instead of inside TaskSpec 
         * -> Python dependncy remains confined to the binding layer
         */
        // destruction might be an issue here
        py::function callable_;
        std::string payload_;
    };

    // TaskHandler adapter
    /**
     * Convert a binding-owned PythonCallback into the TaskHandler abstraction understood by the C++ runtime
     * 
     * Keeping this conversion in 1 helper prevents runtime_bindings.cpp from repeated constructing slightly different callback lambdas
     * 
     * The returned Taskhandler captures shared ownership of PythonCallback, ensuring Python callable remains alive while the runtime may still execute the task
     */
    // C++ inline function: acts as compiler optimization hint to reduce function call overhead
    /**
     * shared_ptr: runtime accepts (Status Submit()), TaskHandler is a std::function
     * 
     * we need callback = std::make_shared
     */
    inline TaskHandler MakePythonTaskHandler(std::shared_ptr<PythonCallback> callback)
    {
        // Phase 1 interface doesn't contain the input-payload argument
        // need to solve when runtime_bindings.cpp
        /**
         * Where does serialized Python input paylaod live?
         * 
         * TaskSpec now: std::vector<ObjectRef> inputs
         * without: std::stirng payload
         */
        /**
         * [](){} function
         *   [] -> Tells compiler what local variables from the outside scope the function can "see" and use inside
         *   [=] -> Captures everything in the surrounding scope by copy (or list out specific variables wanted to copy)
         */
        return [callback = std::move(callback)](
            const TaskSpec& spec, 
            RuntimeContext& context,
            std::string* out
        ) -> Status
        {
            if(!callback)
            {
                return Status::Internal("Python task callback is null");
            }

            return callback->Invoke(spec,context, out);
        };
    }

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
    class NativeExecutionRef final
    {
    public:
        NativeExecutionRef(std::shared_ptr<Runtime> runtime, TaskID task_id);

    // get()
    /*
    * Wait until the task reaches a terminal state and return its serialized result
    * 
    * Runtime::GetResult() is authoritative for: 
    *   - waiting
    *   - terminal-state detection
    *   - timeout semantics
    *   - execution failure
    *   - cancellation
    * 
    * GIL(global Interpreter Lock) -> mutual-exclusion lock(mutex) used by default CPython interpreter
    * to ensure only 1 thread executes Python bytecode at a time
    * - is released while waiting
    * 
    * A DAR worker executing this task may need to acquire the GIL in PythonCallback::Invoke()
    * 
    * May acquire deadlock:
    *  Python thread: ref.get() --holds GIL--> GetResult() waits
    *  Worker: PythonCallback::Invoke() --waits for GIL--> DEADLOCK
    */
        py::bytes Get();
            // cancel()
    /**
     * Request cancellation through Runtime
     * 
     * this does NOT mutate execution state itself
         * 
         * Phase 1 cancellation semantics remain authoritative:
         *   Queued -> immediately cancelled
         *   Running -> set cooperative cancellation token
         *   Terminal -> FailedPrecondition
         *   Scheduled handoff -> currently FailedPrecondition
        */
        void Cancel();
                /**
         * Return the current execution lifecycle state
         * 
         * This operation is non-blocking
         * 
         *  Intentionally return the stable name supplied by ExecutionStateName() rather than exposing the C++ enum directly into Python in Phase 2
        */
        std::string StatusName() const;
        
        /**
        * return the stable 128-bit TaskID as lowercase hexademical
        * StrongID::Hex() already owns DAR's canonical textual ID representation, so the binding must not implement another serializer
        */
        /**
         * C++ type: dar::TaskID
         * c++ Getter: NativeExecutionRef::TaskId()
         * Python property: ref.task_id
         */
        std::string TaskId() const;


    private:
        /**
        *Defensive invariant check
        * 
        * Normal construction through NativeRuntime::Submit()
        * should always provide a valid runtime
        */
        void EnsureRuntime() const;

        // Shared ownership keeps Runtime alive for as long as this execution handle may call back into it
        std::shared_ptr<Runtime> runtime_;

        // Stable identity of the logical task
        TaskID task_id_;
    };

    // Namespace funtions
    void BindResources(py::module_& module);
    void BindExecution(py::module_& module);
    void BindRuntime(py::module_& module);
}

/**
 *     class NativeExecutionRef final
    {
    public:
        NativeExecutionRef(
            std::shared_ptr<Runtime> runtime,
            TaskID task_id
        ) : runtime_(std::move(runtime)), task_id_(std::move(task_id)){}

        // get()
        /**
         * Wait until the task reaches a terminal state and return its serialized result
         * 
         * Runtime::GetResult() is authoritative for: 
         *   - waiting
         *   - terminal-state detection
         *   - timeout semantics
         *   - execution failure
         *   - cancellation
         * 
         * GIL(global Interpreter Lock) -> mutual-exclusion lock(mutex) used by default CPython interpreter
         * to ensure only 1 thread executes Python bytecode at a time
         * - is released while waiting
         * 
         * A DAR worker executing this task may need to acquire the GIL in PythonCallback::Invoke()
         * 
         * May acquire deadlock:
         *  Python thread: ref.get() --holds GIL--> GetResult() waits
         *  Worker: PythonCallback::Invoke() --waits for GIL--> DEADLOCK

        py::bytes Get()
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

        // cancel()
        /**
         * Request cancellation through Runtime
         * 
         * this does NOT mutate execution state itself
         * 
         * Phase 1 cancellation semantics remain authoritative:
         *   Queued -> immediately cancelled
         *   Running -> set cooperative cancellation token
         *   Terminal -> FailedPrecondition
         *   Scheduled handoff -> currently FailedPrecondition
        void Cancel()
        {
            EnsureRuntime();

            const Status status = runtime_->Cancel(task_id_);

            ThrowStatus(status);
        }

        /**
         * Return the current execution lifecycle state
         * 
         * This operation is non-blocking
         * 
         *  Intentionally return the stable name supplied by ExecutionStateName() rather than exposing the C++ enum directly into Python in Phase 2
        std::string StatusName() const
        {
            EnsureRuntime();

            TaskSnapshot snapshot;

            const Status status = runtime_->GetStatus(task_id_,&snapshot);

            return std::string(ExecutionStateName(snapshot.state));
        }

        /**
         * return the stable 128-bit TaskID as lowercase hexademical
         * StrongID::Hex() already owns DAR's canonical textual ID representation, so the binding must not implement another serializer
         */
        /**
         * C++ type: dar::TaskID
         * c++ Getter: NativeExecutionRef::TaskId()
         * Python property: ref.task_id
        std::string TaskId() const
        {
            return task_id_.Hex();
        }

    private:
        /**
         * Defensive invariant check
         * 
         * Normal construction through NativeRuntime::Submit()
         * should always provide a valid runtime
        void EnsureRuntime() const
        {
            if(!runtime_)
            {
                throw std::runtime_error("NativeExecutionRef has no runtime");
            }        
        }

        // Shared ownership keeps Runtime alive for as long as this execution handle may call back into it
        std::shared_ptr<Runtime> runtime_;

        // Stable identity of the logical task
        TaskID task_id_;
    };
 */