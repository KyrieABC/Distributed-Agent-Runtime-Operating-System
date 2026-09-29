#include <chrono>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>

#include "dar/common/id.h"
#include "dar/rpc/connection_pool.h"
#include "dar/rpc/rpc_client.h"
#include "dar/rpc/rpc_server.h"
#include "dar/serialization/serialization.h"

#include "worker.grpc.pb.h"

namespace
{

class SmokeWorkerService final
    : public dar::proto::v1::WorkerService::Service
{
public:
    explicit SmokeWorkerService(dar::NodeID node_id)
        : node_id_(node_id)
    {
    }

    grpc::Status GetWorkerInfo(
        grpc::ServerContext* context,
        const dar::proto::v1::GetWorkerInfoRequest* request,
        dar::proto::v1::GetWorkerInfoResponse* response) override
    {
        (void)request;

        if (context->IsCancelled())
        {
            return grpc::Status(
                grpc::StatusCode::CANCELLED,
                "client cancelled request"
            );
        }

        // Application/runtime result.
        dar::serialization::ToProto(
            dar::Status::OK(),
            response->mutable_status()
        );

        dar::serialization::ToProto(
            node_id_,
            response->mutable_node_id()
        );

        response->set_incarnation(1);
        response->set_running_tasks(0);

        return grpc::Status::OK;
    }

private:
    dar::NodeID node_id_;
};

}  // namespace

TEST(Phase3TransportTest, UnaryRpcCrossesTransportBoundary)
{
    const dar::NodeID server_node =
        dar::NodeID::Random();

    SmokeWorkerService service(server_node);

    // Port 0 asks the OS/gRPC to select an available port.
    dar::RpcServer server("127.0.0.1:0");

    ASSERT_TRUE(
        server.RegisterService(&service).ok()
    );

    const dar::Status start_status =
        server.Start();

    ASSERT_TRUE(start_status.ok())
        << start_status.ToString();

    const int port = server.SelectedPort();

    ASSERT_GT(port, 0);

    const std::string endpoint =
        "127.0.0.1:" + std::to_string(port);

    // ---------------- Client side ----------------

    dar::ConnectionPool connections;
    dar::RpcClient client(connections);

    auto channel =
        client.Channel(endpoint);

    auto stub =
        dar::proto::v1::WorkerService::NewStub(
            channel
        );

    dar::proto::v1::GetWorkerInfoRequest request;
    dar::proto::v1::GetWorkerInfoResponse response;

    // Deadline is mandatory through RpcClient.
    auto context =
        client.CreateContext(
            std::chrono::seconds(2)
        );

    // This is TRANSPORT status.
    const grpc::Status transport_status =
        stub->GetWorkerInfo(
            context.get(),
            request,
            &response
        );

    ASSERT_TRUE(transport_status.ok())
        << transport_status.error_message();

    // This is DAR APPLICATION/RUNTIME status.
    const dar::Status runtime_status =
        dar::serialization::FromProtoStatus(
            response.status()
        );

    ASSERT_TRUE(runtime_status.ok())
        << runtime_status.ToString();

    dar::NodeID returned_node;

    const dar::Status decode_status =
        dar::serialization::FromProto(
            response.node_id(),
            &returned_node
        );

    ASSERT_TRUE(decode_status.ok())
        << decode_status.ToString();

    EXPECT_EQ(server_node, returned_node);

    EXPECT_TRUE(server.Shutdown().ok());
}

TEST(Phase3TransportTest, ConnectionPoolReusesChannel)
{
    dar::ConnectionPool pool;

    auto first =
        pool.GetChannel("127.0.0.1:50051");

    auto second =
        pool.GetChannel("127.0.0.1:50051");

    EXPECT_EQ(first.get(), second.get());
    EXPECT_EQ(pool.Size(), 1U);
}

TEST(Phase3TransportTest, DifferentEndpointsUseDifferentChannels)
{
    dar::ConnectionPool pool;

    auto first =
        pool.GetChannel("127.0.0.1:50051");

    auto second =
        pool.GetChannel("127.0.0.1:50052");

    EXPECT_NE(first.get(), second.get());
    EXPECT_EQ(pool.Size(), 2U);
}

TEST(Phase3TransportTest, RpcClientRejectsNonPositiveDeadline)
{
    dar::ConnectionPool pool;
    dar::RpcClient client(pool);

    EXPECT_THROW(
        client.CreateContext(
            std::chrono::milliseconds(0)
        ),
        std::invalid_argument
    );

    EXPECT_THROW(
        client.CreateContext(
            std::chrono::milliseconds(-1)
        ),
        std::invalid_argument
    );
}