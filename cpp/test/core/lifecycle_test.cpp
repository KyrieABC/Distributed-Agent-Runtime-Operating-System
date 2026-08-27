#include "dar/core/lifecycle.h"

#include <gtest/gtest.h>

namespace dar
{
    namespace
    {
        // It should automatically start with kPending (default defined by constructor)
        TEST(LifecycleTest, StartsPending)
        {
            const Lifecycle lifecycle;

            EXPECT_EQ(lifecycle.state(), ExecutionState::kPending);

            EXPECT_FALSE(lifecycle.terminal());
        }

        TEST(LifecycleTest, AcceptsHappyPath)
        {
            // default start with kPending
            Lifecycle lifecycle;

            // change the class object's private member variable state_ to kQueued if accepted
            // (lowercase)ok() compare if the status == ok
            EXPECT_TRUE(lifecycle.TransitionTo(ExecutionState::kQueued).ok());

            EXPECT_TRUE(lifecycle.TransitionTo(ExecutionState::kScheduled).ok());

            EXPECT_TRUE(lifecycle.TransitionTo(ExecutionState::kRunning).ok());

            EXPECT_TRUE(lifecycle.TransitionTo(ExecutionState::kSSucceeded).ok());

            // now the final state(getter) should be the same as success
            EXPECT_EQ(lifecycle.state(),ExecutionState::kSSucceeded);
    
            // And it should be terminal state(success in this case)
            EXPECT_TRUE(lifecycle.terminal());
        }

        TEST(LifecycleTest, RejectsSkippedAndTerminalTransition)
        {
            Lifecycle lifecycle;

            // Go straight from default kPending to running (skip scheduled)
            const Status skipped = lifecycle.TransitionTo(ExecutionState::kRunning);
        
            // The current status should be faileprecondition(from TransitionTo() function)
            EXPECT_EQ(skipped.code(), StatusCode::KFailedPrecondition);
        
            // A failed transition must not mutate the current state
            EXPECT_EQ(lifecycle.state(), ExecutionState::kPending);

            // Cancellation is allowed for pending state
            EXPECT_TRUE(lifecycle.TransitionTo(ExecutionState::kCancelled).ok());

            // After the TransitionTo(cancelled)
            EXPECT_EQ(lifecycle.state(),ExecutionState::kCancelled);

            // Cancel is a terminal
            EXPECT_TRUE(lifecycle.terminal());

            const Status after_cancel = lifecycle.TransitionTo(ExecutionState::kFailed);

            // cannot go from terminal (cancelled) to failed
            EXPECT_EQ(after_cancel.code(), StatusCode::KFailedPrecondition);

            // Failed transition must not alter the state
            EXPECT_EQ(lifecycle.state(),ExecutionState::kCancelled);
        }

        TEST(LifecycleTest, RunningCanFail)
        {
            // default state: kPending
            Lifecycle lifecycle;

            ASSERT_TRUE(lifecycle.TransitionTo(ExecutionState::kQueued).ok());
        
            ASSERT_TRUE(lifecycle.TransitionTo(ExecutionState::kScheduled).ok());

            ASSERT_TRUE(lifecycle.TransitionTo(ExecutionState::kRunning).ok());

            ASSERT_TRUE(lifecycle.TransitionTo(ExecutionState::kFailed).ok());

            EXPECT_TRUE(lifecycle.terminal());
        }

        TEST(LifecycleTest, CanCancelWhileQueued)
        {
            Lifecycle Lifecycle;

            ASSERT_TRUE(Lifecycle.TransitionTo(ExecutionState::kQueued).ok());

            EXPECT_TRUE(Lifecycle.TransitionTo(ExecutionState::kCancelled).ok());

            EXPECT_EQ(Lifecycle.state(), ExecutionState::kCancelled);

            EXPECT_TRUE(Lifecycle.terminal());
        }

        TEST(LifecycleTeset, CanCancelWhileRunning)
        {
            Lifecycle Lifecycle;

            ASSERT_TRUE(Lifecycle.TransitionTo(ExecutionState::kQueued).ok());

            ASSERT_TRUE(Lifecycle.TransitionTo(ExecutionState::kScheduled).ok());

            ASSERT_TRUE(Lifecycle.TransitionTo(ExecutionState::kRunning).ok());

            ASSERT_TRUE(Lifecycle.TransitionTo(ExecutionState::kCancelled).ok());

            EXPECT_EQ(Lifecycle.state(), ExecutionState::kCancelled);

            EXPECT_EQ(Lifecycle.state(), ExecutionState::kCancelled);

            EXPECT_TRUE(Lifecycle.terminal());
        }

        // Unfinished

    }
}