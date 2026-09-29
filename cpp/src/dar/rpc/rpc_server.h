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

    std::vector<grpc::Service*> services_;

    std::unique_ptr<grpc::Server> server_;

    bool started_{false};

    int selected_port_{0};
};

}  // namespace dar