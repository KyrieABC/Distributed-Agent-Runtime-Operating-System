#pragma once

#include <grpcpp/grpcpp.h>

#include "dar/runtime/runtime.h"
#include "dar/worker/handler_registry.h"

#include "worker.grpc.pb.h"

namespace dar
{
// in worker.proto: `service WorkerService{rpc...}`
// worker.grpc.pb.h(declaration of the generated WorkerService::service class)
// worker.grpc.pb.cpp(generated implementation/support code)
/**
 * Inherit from Service:
 * - the generated Service class defines the interface between gRPC and your application
 *  -> gRPC knows someone called RPC /dar.proto.v1.WorkerService/LaunchTask but does NOT know what DAR should actually do
 * (the generated class establishes `virtual grpc::Status LaunchTask(...))
 * (then provide actual behavior):
 * namespace dar::proto::v1 {
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
    WorkerServiceImpl(
        Runtime& runtime,
        HandlerRegistry& registry);

    grpc::Status LaunchTask(
        grpc::ServerContext* context,
        const proto::v1::LaunchTaskRequest* request,
        proto::v1::LaunchTaskResponse* response) override;

private:
    Runtime& runtime_;
    HandlerRegistry& registry_;
};

}  // namespace dar