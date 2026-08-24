#include "dar/core/execution.h"

#include <chrono>

#include <gtest/gtest.h>

namespace dar
{
    namespace
    {
        TEST(ExecutionTest, StoresIdentityAndStartsPending)
        {
            const ExecutionID execution_id = ExecutionID::Random();

            const TenantID tenant_id = TenantID::Random();

            const TaskID task_id = TaskID::Random();

            const AgentID agent_id = AgentID::Random();

            const WallTime created_at = WallTimeNow();

            Execution execution(execution_id,tenant_id,task_id,agent_id,1, ResourceRequest{},created_at);

            // Test how it is set through Execution constructor and the getter(id())
            EXPECT_EQ(execution.id(), execution_id);
            EXPECT_EQ(execution.tenant_id(),tenant_id);
            EXPECT_EQ(execution.task_id(),task_id);
            EXPECT_EQ(execution.agent_id(),agent_id);
            EXPECT_EQ(execution.attempt(),1U);
            // the default state of lifecycle created as private member variable should be kPending
            EXPECT_EQ(execution.state(), ExecutionState::kPending);
            EXPECT_EQ(execution.created_at(),create_at);
            EXPECT_FALSE(execution.started_at().has_value());
        }
    }
}