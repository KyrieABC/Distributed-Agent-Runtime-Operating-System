#include "dar/rpc/rpc_server.h"

/**
 * grpc::Service -> Contract/Blueprint
 *  - Abstract base class generated from .proto fil. Defines what RPC methods your service handles, but contains no networking or lifecycle logic
 * grpc::ServerBuilder -> Assembler/Factory
 *  - COnfiguration utility used before the server starts
 * grpc::ServerInterface/ grpc::Server -> Running Engine
 *  - Abstract interface (ServerInterface), and concrete implementation(Server)
 * 
 * grpc::impl::Server -> ABI (application Binary Interface) stability and code organization
 *  - inline namespaces or internal implementation namespaces to separate public-facing API headers form the concrete implementation details 
 */
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

    // Configuration and builder class used to construct, configure, and launch a grpc::Server
    // AddListenPort(): specify IP addresses and ports to listen on, and attach credentials
    // RegisterService(): link custom service implementation so server knows how to handle incoming requests
    grpc::ServerBuilder builder;

    int selected_port = 0;

    builder.AddListeningPort(
        listen_address_,
        grpc::InsecureServerCredentials(),
        &selected_port
    );

    for (grpc::Service* service : services_)
    {
        // wire up custom service implementation so the server knows which RPC methods it is responsible for handling
        //! grpc::Server does not take ownership of your service instance (service object must outlive the running grpc::Server object)
        builder.RegisterService(service);
    }

    // builder verifies that you've added at least 1 valid listening port and registered your services
    // Then allocates the underlying networking resources, binds to the port, and spins up gRPC's internal thread pools and completion queues
    // It finishes its job as a configuration tool, starts the network listener immediately, and hands back ownership of the newly running instance wrapped inside a unique_ptr<grpc::Server>
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
        // grpc::ServerInterface::Shutdown()
        // immediately deactivates all listening ports so no new connections or incoming RPC requests can be accepted
        // Any pre-registered or queued server-side calls that haven't yet been matched to an incoming client request are returned immediately as failed
        // blocks execution to allow currently active, in-flight RPC handlers a chance to finish executing their business logic
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
        // grpc_impl::Server::Wait()
        // -> called on active grpc::Server instance
        // -> Once server started, calling server->Wait() parks the calling thread until the server is shut down.
        /**
         * When call builder.BuildAndStart(), gRPC spins up background worker threads to listen for network traffic
         * However, main() thread will naturally rush to the end of program and exit if don't block it. 
         * Waits() prevents program from terminating immediately, keeping the process alive to listen for incoming RPCs.
         */
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