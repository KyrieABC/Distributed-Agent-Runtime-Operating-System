#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "dar/common/status.h"

namespace dar
{

class RpcServer final
{
public:
    explicit RpcServer(std::string listen_address);

    ~RpcServer();

    RpcServer(const RpcServer&) = delete;
    RpcServer& operator=(const RpcServer&) = delete;

    // Services must outlive the running RpcServer.
    Status RegisterService(grpc::Service* service);

    Status Start();

    Status Shutdown();

    void Wait();

    [[nodiscard]]
    bool IsRunning() const;

    [[nodiscard]]
    int SelectedPort() const;

private:
    std::string listen_address_;

    mutable std::mutex mu_;

    // grpc::Service: base class for all generated server-side service implementation in gRPC C++.
    // -> Act as routing and registration contract between the gRPC server runtime
    // -> When an incoming network request arrives at the gRPC server, the server uses the registered grpc::Service
    std::vector<grpc::Service*> services_;

    // core server-side object, represent a running server instance that listens for incoming network connections, manager worker threads, dispatches incoming RPC requests to their corresponding grpc::Service handlers, and manages the server's lifecycle
    std::unique_ptr<grpc::Server> server_;

    bool started_{false};

    int selected_port_{0};
};

}  // namespace dar