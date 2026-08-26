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
            EXPECT_EQ(execution.created_at(),created_at);
            // Uninitialized?
            EXPECT_FALSE(execution.started_at().has_value());
            EXPECT_FALSE(execution.finished_at().has_value());


            // Just initialized, should be kPending and not finished
            EXPECT_FALSE(execution.terminal());
        }


        TEST(ExecutionTest, ValidExecutionPassValidation)
        {
            Execution execution(ExecutionID::Random(),TenantID::Random(),TaskID::Random(),AgentID::Random(),1,ResourceRequest{},WallTimeNow());


            EXPECT_TRUE(execution.Validate().ok());
        }


        TEST(ExecutionTest, NilExecutionFailsValidation)
        {
            Execution execution(ExecutionID::Nil(),TenantID::Random(),TaskID::Random(),AgentID::Random(),1,ResourceRequest{},WallTimeNow());


            // Validate() make sure ID cannot be a nil id (all 0)
            const Status status = execution.Validate();
            EXPECT_EQ(status.code(), StatusCode::kInvalidArgument);
        }


        // test another if loop for execution Validate function(attempt number cannot be 0)
        TEST(ExecutionTest, ZeroAttemptFailsValidation)
        {
            const WallTime created_at = WallTimeNow();


            Execution execution(ExecutionID::Random(), TenantID::Random(),TaskID::Random(),AgentID::Random(),0, ResourceRequest{},created_at);


            const Status status = execution.Validate();


            EXPECT_FALSE(status.ok());


            EXPECT_EQ(status.code(), StatusCode::kInvalidArgument);


            EXPECT_EQ(status.message(),"attempt number cannot be less or equal to 0");
        }


        TEST(ExecutionTest, RunningRecordsStartTime)
        {
            const WallTime created_at = WallTimeNow();
            const WallTime queued_at = created_at + std::chrono::seconds(1);
            const WallTime scheduled_at = created_at + std::chrono::seconds(2);
            const WallTime running_at = created_at + std::chrono::seconds(3);

            Execution execution(ExecutionID::Random(), TenantID::Random(), TaskID::Random(),AgentID::Random(), 1, ResourceRequest{},created_at);

            // See if it can be transition to queued at a specific time
            ASSERT_TRUE(execution.TransitionTo(ExecutionState::kQueued,queued_at).ok());

            ASSERT_TRUE(execution.TransitionTo(ExecutionState::kScheduled,scheduled_at).ok());

            ASSERT_TRUE(execution.TransitionTo(ExecutionState::kRunning, running_at).ok());

            EXPECT_EQ(execution.state(), ExecutionState::kRunning);

            // see if the started_at_ variable is set through TransitionTo
            ASSERT_TRUE(execution.started_at().has_value());
            
            // What is this? 
            EXPECT_EQ(*execution.started_at(), running_at);

            // This should be false because the state has never transitioned and updated to terminal state
            EXPECT_FALSE(execution.finished_at().has_value());
            EXPECT_FALSE(execution.terminal());
        }

        TEST(ExecutionTest, SucccessRecordsFinishTime)
        {
            const WallTime created_at = WallTimeNow();
            const WallTime queued_at = created_at + std::chrono::seconds(1);
            const WallTime scheduled_at = created_at + std::chrono::seconds(2);
            const WallTime running_at = created_at + std::chrono::seconds(3);
            const WallTime succeed_at = created_at + std::chrono::seconds(10);

            Execution execution(ExecutionID::Random(), TenantID::Random(), TaskID::Random(),AgentID::Random(), 1, ResourceRequest{},created_at);

            ASSERT_TRUE(execution.TransitionTo(ExecutionState::kQueued,queued_at).ok());

            ASSERT_TRUE(execution.TransitionTo(ExecutionState::kScheduled,scheduled_at).ok());

            ASSERT_TRUE(execution.TransitionTo(ExecutionState::kRunning, running_at).ok());

            ASSERT_TRUE(execution.TransitionTo(ExecutionState::kSSucceeded, succeed_at).ok());

            EXPECT_EQ(execution.state(), ExecutionState::kSSucceeded);

            EXPECT_TRUE(execution.terminal());

            ASSERT_TRUE(execution.started_at().has_value());

            // The time that set started_at should be when state = kRunning
            EXPECT_EQ(*execution.started_at(), running_at);

            ASSERT_TRUE(execution.finished_at().has_value());

            EXPECT_EQ(*execution.finished_at(),succeed_at);
        }

        
    }
}
