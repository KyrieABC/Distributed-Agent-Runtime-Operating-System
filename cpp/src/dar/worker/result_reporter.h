#pragma once

#include <chrono>
#include <string>

#include "dar/common/id.h"
#include "dar/common/status.h"
#include "dar/rpc/rpc_client.h"
#include "dar/runtime/task_manager.h"

namespace dar
{

class ResultReporter final
{
public:
    ResultReporter(
        RpcClient& rpc_client,
        std::string control_endpoint);

    Status Report(
        TaskID task_id,
        ExecutionID execution_id,
        ExecutionState state,
        const Status& terminal_status,
        const std::string& result,
        std::chrono::milliseconds timeout);

private:
    RpcClient& rpc_client_;

    std::string control_endpoint_;
};

}  // namespace dar