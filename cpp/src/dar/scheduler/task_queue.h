#pragma once

// Double-ended queue
#include <deque>
// Synchronization primitive used to protect shared ata from being simultaneously accessed by multiple threads (prevent race conditions)
#include <mutex>

#include "dar/common/status.h"
#include "dar/core/task_spec.h"

namespace dar
{
    // TaskQueue
    /**
     * A thread-safe FIFO queue of TaskID
     * 
     * Operations:
     *   - Push adds a task to the back (always succeeds)
     *   - Pop removes and returns the front task (fail if empty)
     *   - Peek returns the front task without removing (fail if empty)
     *   - Remove erases a specific task if present (fail if not found)
     *   - Size/Empty query the queue size
     * 
     * This does NOT implement priorities, fairness, or any resource-based ordering. 
     */
    class TaskQueue
    {
    public:
        // Add a task to the end of queue
        // Always succeeds in Phase 1
        Status Push(TaskID id);

        // Remove and return the front task
        // On Success: returns OK and sets *out to the TaskID
        // If queue is emtpy, returns a non-OK status (NotFound)
        // Output goes to TaskID* out
        Status Pop(TaskID* out);

        // Read the front task without removing it
        // Success: returns OK and sets *out to the TaskID
        // if queue is empty, returns a non-OK status (NotFound)
        Status Peek(TaskID* out) const;

        // Remove the specified task (by ID) from the queue, if present.
        // Success: returns OK. 
        // If task not in queue, returns a non-OK status
        Status Remove(TaskID id);

        // Number of tasks currently in the queue
        std::size_t Size() const;

        // True if there are no tasks in queue
        bool Empty() const;

    private:
        mutable std::mutex mu_;
        std::deque<TaskID> queue_;
    };
}