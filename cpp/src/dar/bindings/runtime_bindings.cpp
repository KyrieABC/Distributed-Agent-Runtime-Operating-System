#include "dar/bindings/binding_types.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <pybind11/pybind11.h>

#include "dar/common/id.h"
#include "dar/common/status.h"
#include "dar/core/resource.h"
#include "dar/core/task_spec.h"
#include "dar/runtime/runtime.h"

namespace dar::bindings
{
    namespace py = pybind11;

    // PythonCallback::Invoke
    Status PythonCallback::Invoke(const TaskSpec& spec, RuntimeContext& context, std::string* out)
    {
        if(out==nullptr)
        {
            return Status::InvalidArgument("Python callback output must not be null");
        }

        // Cooperative cancellation before entering Python
        // If cancellation was requested after dispatch but before the Python callback begins, avoid executing user code
        if(context.IsCancellationRequested())
        {
            return Status::Cancelled("task cancelled before Python callback execution");
        }

        // Enter Python
        /** 
         * Worker::Run() executes on a native C++ worker thread
         * 
         * That thread does not enter Python through pybind11, so it does not already own the Python GIL
         */
        py::gil_scoped_acquire acquire;

        try
        {
            // Serialized payload is opaque binary data
            // Do NOT expose it as py::str because pickle data is not necessarily valid UTF-8
            py::bytes python_payload(payload_.data(), payload_.size());

            // Binding contract: callable(serialized_payload) -> bytes
            /**
             * public Python layer owns:
             *   pickle.loads(input)
             *   user function invocation
             *   pickle.dumps(result)
             * 
             * c++ remains serialization-format agnostic
             */
            py::object result = callable_(python_payload);

            // Result contract: Native boundary requires serialized bytes
            if(!py::isinstance<py::bytes>(result))
            {
                return Status::Internal("Python task callback must return bytes");
            }

            // py::bytes -> std::string preserves the raw byte sequence, including embedded NUL bytes
            *out = result.cast<std::string>();

            // Cooperative cancellation after python execution
            /**
             * Python code may have completed while cancellation was being requested
             * 
             * In phase 2, cancellation wins if the token was observed before publishing successful completion
             */
            if(context.IsCancellationRequested())
            {
                out->clear();
                return Status::Cancelled("task cancelled during Python callback execution");
            }

            return Status::OK();
        }
        catch(const py::error_already_set& error)
        {
            // Do not allow a Python exception to unwind through the C++ worker/runtime
            // Executor's contract remains Status-based
            // error.what() is good because GIL is currently held by gil_scoped_acquire
            return Status::Internal(std::string("Python task raised an exception: ") + error.what());
        }
        catch(const py::cast_error& error)
        {
            return Status::Internal(std::string("failed to convert Python task result: ") + error.what());
        }
        catch(...)
        {
            return Status::Internal("unknown Python callback binding failure");
        }
    }

    // NativeRuntime
    /**
     * Python-facing owner of DAR's native Runtime
     * 
     * This class does NOT implement scheduling
     * 
     * It only:
     *   Python configuration -> RuntimeOptions
     *   Python submission -> TaskSpec + TaskHandler -> Runtime::Submit()
     * 
     * C++ runtime remain authoritative
     */
    class NativeRuntime final
    {
    public:
        // Construction
        NativeRuntime(std::size_t worker_count, const ResourceRequest& resources_per_worker)
        {
            if(worker_count==0)
            {
                throw py::value_error("worker_count must be greater than 0");
            }

            RuntimeOptions options;
            options.worker_count = worker_count;

            // ResourceRequest is only a thin semantic wrapper around ResourceSet, RuntimeOptions requires ResourceSet
            options.resources_per_worker = resources_per_worker.resources();

            runtime_ = std::make_shared<Runtime>(std::move(options));
        }

        void Start()
        {
            EnsureRuntime();

            const Status status = runtime_->Start();

            ThrowStatus(status);
        }

        void Shutdown()
        {
            EnsureRuntime();
            Status status;
            {
                // Runtime::Shutdown() drains accepted work
                // A drain worker may need to acquire the GIL in PythonCallback::Invoke()
                // -> Holding the GIL while Shutdown() waits would create same deadlock as ref.get()
                py::gil_scoped_release release;

                status = runtime_->Shutdown();
            }

            // Python exception machinery requires the GIL
            ThrowStatus(status);
        }

        NativeExecutionRef Submit(py::function callable, py::bytes payload, const std::string& name, const ResourceRequest& resources)
        {
            EnsureRuntime();

            // Copy serialized payload into native memory
            // Once copied, the worker does not depend on the lifetime of the original Python bytes object
            const std::string serialized_payload = payload.cast<std::string>();

            // Generate native identities
            // runtime/binding-created logical identities, not Python object identities
            const TenantID tenant_id = TenantID::Random();
            const TaskID task_id = TaskID::Random();
            const AgentID agent_id = AgentID::Random();

            // Construct pure DAR TaskSpec
            // NOT here: python callable, pickle bytes, py::object
            // TaskSpec remains Python-independent
            TaskSpec spec;

            spec.tenant_id = tenant_id;
            spec.id = task_id;
            spec.agent_id = agent_id;
            spec.name = name;
            spec.resources = resources;

            // Phase 2's local serialized payload is carried by the binding callback rather than dar::ObjectRef inputs
            spec.inputs.clear();

            // Binding-owned executable state
            // PythonCallback owns both: Python callable, serialized input
            // TaskHandler owns PythonCallback through shared_ptr
            auto callback = std::make_shared<PythonCallback>(std::move(callable), serialized_payload);

            TaskHandler handler = MakePythonTaskHandler(std::move(callback));

            // Enter Phase 1 Runtime
            /**
             * Runtime performs:
             *   TaskSpec::Validate()
             *   feasibility admission
             *   TaskManager::register()
             *   Scheduler::Enqueue()
             */
            const Status status = runtime_->Submit(std::move(spec),std::move(handler));

            ThrowStatus(status);

            // Runtime accepted the task
            // Return only its handle; task state continues to belong to Runtime/TaskManager
            return NativeExecutionRef(runtime_, task_id);
        }
    private:
        void EnsureRuntime() const
        {
            if(!runtime_)
            {
                throw std::runtime_error("NativeRuntime has no runtime");
            }
        }

        // Shared ownershpi is intentional
        // NativeExecutionRef receives another shared_ptr so a task handle cannot refer to a destroyed Runtime
        std::shared_ptr<Runtime> runtime_;
    };

    // Python Registration
    void BindRuntime(py::module_& module)
    {
        py::class_<NativeRuntime>(module,"NativeRuntime")
        // Constructor
        .def(
            py::init<std::size_t,
            const ResourceRequest&>(),
            py::arg("worker_count"),
            py::arg("resource_per_worker")
        )
        // Start()
        .def(
            "start",
            &NativeRuntime::Start,
            R"doc(
            Start the native DAR runtime
            Worker and the scheduler are initialized before task admission is opened
            )doc"
        )
        // Shutdown()
        .def(
            "shutdown",
            &NativeRuntime::Shutdown,
            R"doc(
            Drain accepted work and shut down the native DAR runtime
            Python GIL is released while waiting for native workers
            )doc"
        )
        // Submit
        .def(
            "submit",
            &NativeRuntime::Submit,
            py::arg("callable"),
            py::arg("payload"),
            py::arg("name"),
            py::arg("resources"),
            R"doc(
            Submit a serialized Python task to the native DAR runtime
            The callable receives the serialized payload as bytes and must return serialized bytes
            Returns: NativeExecutionRef(handle to the acceped logical task)
            )doc"
        );
    }
}