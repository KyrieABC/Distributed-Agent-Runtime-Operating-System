#pragma once

#include <mutex>
#include <unordered_map>

#include <grpcpp/grpcpp.h>

#include "dar/runtime/runtime.h"
#include "dar/worker/handler_registry.h"
#include "dar/worker/result_reporter.h"

#include "worker.grpc.pb.h"

namespace dar
{
// in worker.proto: `service WorkerService{rpc...}`
// worker.grpc.pb.h(declaration of the generated WorkerService::Service class)
// worker.grpc.pb.cpp(generated implementation/support code)
/**
 * Inherit from Service:
 * - the generated Service class defines the interface between gRPC and your application
 *  -> gRPC knows someone called RPC /dar.proto.v1.WorkerService/LaunchTask but does NOT know what DAR should actually do
 * (the generated class establishes `virtual grpc::Status LaunchTask(...))
 * (then provide actual behavior):
namespace dar::proto::v1 {
class WorkerService {
public:
    // Client side
    class Stub {
    public:
        grpc::Status LaunchTask(...);
        grpc::Status CancelTask(...);
    };
    // Server side
    class Service : public grpc::Service {
    public:
        virtual grpc::Status LaunchTask(
            grpc::ServerContext* context,
            const LaunchTaskRequest* request,
            LaunchTaskResponse* response);
        virtual grpc::Status CancelTask(
            grpc::ServerContext* context,
            const CancelWorkerTaskRequest* request,
            CancelWorkerTaskResponse* response);
    };
};

}
 */
class WorkerServiceImpl final
    : public proto::v1::WorkerService::Service
{
public:

    /**
     * (phase 3 stage 5) Incoming distributed request contains: ExecutionID = E1
     * But, phase-1 scheduler  generates its own local ExecutionID when it calls MarkScheduled()
     * 
     * ControlPlane (ExecutionID: E1) -> WorkerService -> Runtime -> Phase 1 scheduler(ExecutionID: L7)
     * (do NOT report L7 to ControlPlane)
     * 
     * ControlPlane knows: TaskID T1 -> ExecutionID E1
     * 
     * Worker needs to preserve: T1 -> remote E1 (?)
     * WorkerServiceImpl owns the mapping
     */

    WorkerServiceImpl(
        Runtime& runtime,
        HandlerRegistry& registry,
        ResultReporter* result_reporter = nullptr
    );

    grpc::Status LaunchTask(
        grpc::ServerContext* context,
        const proto::v1::LaunchTaskRequest* request,
        proto::v1::LaunchTaskResponse* response) override;

    grpc::Status CancelTask(
        grpc::ServerContext* context,
        const proto::v1::CancelWorkerTaskRequest* request,
        proto::v1::CancelWorkerTaskResponse* response) override;
private:
    // For Phase 3 stage 5B, replace remote_executions_ with an ExecutionIDkkeyed admission ledge
    struct ExecutionAdmission
    {
        TaskID task_id;

        // Result of the original Runtime::Submit admission

        // A duplicate LaunchTask carrying the same ExecutionID returns this result instead of submitting again
        Status admission_status;
    };
    
    Runtime& runtime_;
    HandlerRegistry& registry_;

    ResultReporter* result_reporter_;

    std::mutex executions_mu_;

    // E1 (execution ID) may arrive multiple times because of transport redelivery, but it may cross Runtime::Submit() at most once
    std::unordered_map<ExecutionID, ExecutionAdmission, StrongIDHash<ExecutionID>> executions_;

    // Phase 1 completes task by TaskID while the ControlPlane expects its distributed ExecutionID back
    // This mapping is retained for the lifetime of the worker in Phase 3 so that completed executions remain unknown
    std::unordered_map<TaskID, ExecutionID, StrongIDHash<TaskID>> task_executions_;
};

}  // namespace dar