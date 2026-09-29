/**
 * std::condition_variable::notify_all() (notify_one() wakes up 1 waiting thread)
 * -> unblocks all threads currently waiting for a specific condition variable
 *   Each woken threads contend to reacquire the associated mutex lock one by one before evaluating their boolean conditions and proceeding 
 */

#include <gtest/gtest.h>

// atomic is a C++ template class that make operations on shared variables indivisible and safe from data races without using mutexes
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include "dar/runtime/runtime.h"

namespace dar
{
    namespace
    {
        TEST(Phase1RuntimeStressTest, Executes10000TasksAcrossWorkers)
        {
            constexpr std::size_t KTaskCount = 10000;
            constexpr std::size_t KWorkerCount = 4;
            // Give each worker one logical CPU
            // Every task requests 1 CPU -> 1 worker = at most one simultaneously executing task
            // Exercise both worker reservation and ResourceManager accounting
            ResourceSet worker_resources;
            ASSERT_TRUE(worker_resources.Set("CPU",1.0).ok());

            RuntimeOptions options;
            options.worker_count = KWorkerCount;
            options.resources_per_worker = worker_resources;

            Runtime runtime(std::move(options));
            ASSERT_TRUE(runtime.Start().ok());

            // Test-side exeecution accounting
            std::atomic<std::size_t> executed{0};
            // Number of handlers currently executing
            std::atomic<std::size_t> active{0};
            // Maximum number observed executing concurrently
            std::atomic<std::size_t> max_active{0};

            // Exactly-onec accounting
            // Each logical task gets one index [0,9999]
            std::vector<std::atomic<int>> execution_counts(KTaskCount);

            for(auto& count : execution_counts)
            {
                count.store(0);
            }

            // Completion synchronization

            std::mutex completion_mu;
            std::condition_variable completion_cv;

            // Keep IDs so lifecycle can be checked afterward
            std::vector<TaskID> task_ids;
            task_ids.reserve(KTaskCount);

            // Submit 10000(kTaskCount) tasks
            for(std::size_t i = 0; i< KTaskCount; ++i)
            {
                TaskSpec spec;

                spec.id = TaskID::Random();
                spec.tenant_id = TenantID::Random();
                spec.agent_id = AgentID::Random();
                spec.name = "stress-task-" + std::to_string(i);

                ResourceSet requested_resources;
                ASSERT_TRUE(requested_resources.Set("CPU",1.0).ok());

                spec.resources = ResourceRequest(std::move(requested_resources));

                const TaskID task_id = spec.id;
                task_ids.push_back(task_id);

                // Handler
                TaskHandler handler = [
                    i, 
                    &executed, 
                    &active, 
                    &max_active, 
                    &execution_counts, 
                    &completion_cv]
                    ( const TaskSpec&, RuntimeContext&, std::string* result) -> Status
                    {
                        /**
                         * std::memory_order_relaxed: weakeset memroy ordering available for C++ atomic operations
                         * 
                         * guarantees that the increment itself is atomic (indivisible)
                         * does NOT provide any synchronization or ordering guarantees for other memory reads or writes around it
                         * Fastest memory ordering options (without inserting expensive memory barriers or fences)
                         */

                        // Exactly-once observation
                        // Keep track of an event count across multiple threads, but exact order to timing of the updates does not coordinate other logic
                        // (Atomic operation -> thread safe) (atomically increments [i] by 1 using relaxed memory ordering)
                        // Even if multiple threads execute this line at the exact same time, no increments will be lost
                        execution_counts[i].fetch_add(1,std::memory_order_relaxed);

                        // Measure actual handler concurrency  ?
                        const std::size_t now_active = active.fetch_add(1, std::memory_order_acq_rel)+1;

                        // Update the maximum concurrency observed
                        std::size_t previous_max = max_active.load(std::memory_order_relaxed);

                        while(now_active>previous_max && !max_active.compare_exchange_weak(previous_max,now_active, std::memory_order_relaxed)){}

                        // Trivial task body
                        *result = "result-" + std::to_string(i);
                    
                        // Handler leaving execution
                        active.fetch_sub(1, std::memory_order_acq_rel);

                        const std::size_t finished = executed.fetch_add(1, std::memory_order_acq_rel)+1;

                        // Wake the test when the final task completes
                        if(finished == KTaskCount)
                        {
                            completion_cv.notify_all();
                        }

                        return Status::OK();
                    };

                // Submit through PUBLIC Runtime API
                // Do not bypass Scheduler/TaskManager/etc

                Status submit_status = runtime.Submit(std::move(spec), std::move(handler));
                ASSERT_TRUE(submit_status.ok()) << "Submit failed at task " << i << ": "<< submit_status.ToString();
            }

            // Wait for all handlers
            {
                std::unique_lock<std::mutex> lock(completion_mu);

                // wait either all tasks are completed or 30-seconds timeout is reached
                // wait_for() blocks teh current thread
                // -> while waiting, it automatically releases the mutex lock so others can do work, then re-acquires the lock before waking up and returning
                const bool completed = completion_cv.wait_for(
                    // protects shared state and required by condition variable to prevent race conditions during state checks
                    lock, 
                    // maximum amount of time the thread is willing to wait
                    std::chrono::seconds(30),
                    // Predicate Lambda function passed to condition variable to check whether it is safe to proceed
                    [&executed]()
                    {
                        // .load: atomically fetch the current value of executed usign acquire memory ordering (proper syncrhonization of memory)
                        // if returns true -> waiting stops immediate. if false, thread goes back to sleep until notified again or until the 30-second timeout occurs
                        return executed.load(std::memory_order_acquire) == KTaskCount;
                    }
                );

                ASSERT_TRUE(completed) << " Timed out: completed " << executed.load() << " / " << KTaskCount << " tasks";
            }

            // Invariant 1: Every submitted task executed
            EXPECT_EQ(executed.load(), KTaskCount);

            // INvariant 2: Every task executed once
            for(std::size_t i=0;i<KTaskCount;++i)
            {
                EXPECT_EQ(execution_counts[i].load(),1) << "Task "<<i<<" did not execute exactly once";
            }

            // Invariant 3: WOrker concurrency nver exceeded worker count
            EXPECT_LE(max_active.load(), KWorkerCount);

            /**
             * With multiple workers, expect at least some overlap
             *  NO EXPECT_GT(max_active,1)
             */

             // Invariant 4: FIFO waiting queue has drained
             EXPECT_EQ(runtime.QueueSize(), 0U);

             // Invariant 5: Every task reached succeeded
            for(std::size_t i =0; i<KTaskCount;++i)
            {
                TaskSnapshot snapshot;

                Status status = runtime.GetStatus(task_ids[i],&snapshot);

                ASSERT_TRUE(status.ok()) << "GetStatus failed for task "<<i;
                
                EXPECT_EQ(snapshot.state, ExecutionState::kSSucceeded) << "Task "<<i<<" did not reach succeeded";

                EXPECT_EQ(snapshot.attempt, 1U) << "task "<<i<<" should have exactly one execution attempt";

                EXPECT_TRUE(snapshot.worker.has_value())<<"Task "<<i<<" never received a worker";

                EXPECT_FALSE(snapshot.execution_id.IsNil()) << "task "<<i<< "never received an ExecutionID";
            }

            // Invariant 6: Results survived the entire executionpath
            // Check a representative subset instead of the entire 100 results
            constexpr std::size_t KResultSampes[] ={
                0,1,999,4999,9999
            };

            for(const std::size_t i: KResultSampes)
            {
                std::string result;
                Status status = runtime.GetResult(
                    task_ids[i],
                    &result,
                    std::chrono::seconds(1)
                );

                ASSERT_TRUE(status.ok()) << "GetResult failed for task "<<i<<": "<<status.ToString();
                
                EXPECT_EQ(result, "result-"+std::to_string(i));
            }

            // Invariant 7: Graceful shutdown succeeds afer all accepted work completed
            EXPECT_TRUE(runtime.Shutdown().ok());
        }
    }
}