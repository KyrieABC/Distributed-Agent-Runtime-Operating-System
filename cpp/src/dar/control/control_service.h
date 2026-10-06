#pragma once

#include <grpcpp/grpcpp.h>

#include "dar/control/control_plane.h"

#include "control.grpc.pb.h"

namespace dar
{

class ControlServiceImpl final
    : public proto::v1::ControlService::Service
{
public:
    explicit ControlServiceImpl(
        ControlPlane& control_plane);

    grpc::Status RegisterNode(
        grpc::ServerContext* context,
        const proto::v1::RegisterNodeRequest* request,
        proto::v1::RegisterNodeResponse* response
    ) override;

    grpc::Status RemoveNode(
        grpc::ServerContext* context,
        const proto::v1::RemoveNodeRequest* request,
        proto::v1::RemoveNodeResponse* response
    ) override;

    grpc::Status ReportTaskResult(
        grpc::ServerContext* context,
        const proto::v1::ReportTaskResultRequest* request,
        proto::v1::ReportTaskResultResponse* response) override;

private:
    ControlPlane& control_plane_;
};

}  // namespace dar