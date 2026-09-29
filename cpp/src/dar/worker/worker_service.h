#pragma once

#include <grpcpp/grpcpp.h>

#include "dar/runtime/runtime.h"
#include "dar/worker/handler_registry.h"

#include "worker.grpc.pb.h"

namespace dar
{

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