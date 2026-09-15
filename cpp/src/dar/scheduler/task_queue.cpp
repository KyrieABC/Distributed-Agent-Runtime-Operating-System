#include "dar/scheduler/task_queue.h"

#include <algorithm>

namespace dar
{
    Status TaskQueue::Push(TaskID id)
    {
        // Acquire the mutex to protect the queue
        std::lock_guard<std::mutex> lock(mu_);

        // Append the task ID to the back of the queue
        queue_.push_back(id);

        return Status::OK();
    }

    /**
     * TaskID id;
     * Status status = queue.Pop(&id);   
     * -> Now the id stores the TaskID type variable of the front of queue(FIFO)
     */
    Status TaskQueue::Pop(TaskID* out)
    {
        // Creates a scoped lock that uses RAII for automatic locking and unlocking of mutex
        /**
         * When lock is initialized, constructor call mu_.lock(), blocking current thread until it successfully acquires ownership of mu_
         * When lock goes out of scope, its(mu_) destrucor automatically call mu_.unlock()
         * -> Because unlocking happens in destructor, guaranteed to be unlock even if unexpected exception occured inside function, Prevents deadlock
         */
        std::lock_guard<std::mutex> lock(mu_);

        // If the queue is empty, cannot help
        if(queue_.empty())
        {
            return Status::NotFound("pop from an empty queue");
        }

        *out = queue_.front();
        queue_.pop_front();
        return Status::OK();
    }

    Status TaskQueue::Peek(TaskID* out) const
    {
        std::lock_guard<std::mutex> lock(mu_);

        // if queue empty, nothing to return
        if(queue_.empty())
        {
            return Status::NotFound("peek empty queue");
        }

        // Return (without removing) the front element
        *out = queue_.front();
        return Status::OK();
    }

    Status TaskQueue::Remove(TaskID id)
    {
        std::lock_guard<std::mutex> lock(mu_);

        auto it = std::find(queue_.begin(), queue_.end(),id);
        if(it == queue_.end())
        {
            // not found
            return Status::NotFound("task not found in queue");
        }

        //Erase the found task
        queue_.erase(it);
        return Status::OK();
    }

    std::size_t TaskQueue::Size() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return queue_.size();
    }

    bool TaskQueue::Empty() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return queue_.empty();
    }
}