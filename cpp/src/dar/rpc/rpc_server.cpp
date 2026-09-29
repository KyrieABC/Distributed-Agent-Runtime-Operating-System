#include "dar/rpc/rpc_server.h"

#include <utility>

namespace dar
{

RpcServer::RpcServer(std::string listen_address)
    : listen_address_(std::move(listen_address))
{
}

RpcServer::~RpcServer()
{
    Shutdown();
}

Status RpcServer::RegisterService(grpc::Service* service)
{
    if (service == nullptr)
    {
        return Status::InvalidArgument(
            "RPC service must not be null"
        );
    }

    std::lock_guard<std::mutex> lock(mu_);

    if (started_)
    {
        return Status::FailedPrecondition(
            "cannot register service after RPC server has started"
        );
    }

    services_.push_back(service);

    return Status::OK();
}

Status RpcServer::Start()
{
    std::lock_guard<std::mutex> lock(mu_);

    if (started_)
    {
        return Status::AlreadyExists(
            "RPC server is already running"
        );
    }

    if (listen_address_.empty())
    {
        return Status::InvalidArgument(
            "RPC listen address must not be empty"
        );
    }

    if (services_.empty())
    {
        return Status::FailedPrecondition(
            "RPC server requires at least one registered service"
        );
    }

    grpc::ServerBuilder builder;

    int selected_port = 0;

    builder.AddListeningPort(
        listen_address_,
        grpc::InsecureServerCredentials(),
        &selected_port
    );

    for (grpc::Service* service : services_)
    {
        builder.RegisterService(service);
    }

    auto server = builder.BuildAndStart();

    if (server == nullptr)
    {
        return Status::Unavailable(
            "failed to start gRPC server"
        );
    }

    server_ = std::move(server);
    selected_port_ = selected_port;
    started_ = true;

    return Status::OK();
}

Status RpcServer::Shutdown()
{
    std::unique_ptr<grpc::Server> server;

    {
        std::lock_guard<std::mutex> lock(mu_);

        if (!started_)
        {
            return Status::OK();
        }

        server = std::move(server_);

        started_ = false;
        selected_port_ = 0;
    }

    if (server != nullptr)
    {
        server->Shutdown();
    }

    return Status::OK();
}

void RpcServer::Wait()
{
    grpc::Server* server = nullptr;

    {
        std::lock_guard<std::mutex> lock(mu_);

        if (started_)
        {
            server = server_.get();
        }
    }

    if (server != nullptr)
    {
        server->Wait();
    }
}

bool RpcServer::IsRunning() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return started_;
}

int RpcServer::SelectedPort() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return selected_port_;
}

}  // namespace dar