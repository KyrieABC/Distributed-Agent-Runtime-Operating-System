# pragma once

#include <string>
#include <string_view>

namespace dar
{
    // Stable runtime-level categories
    // Do not put gRPC codes, Kafka errors, PostgreSQL errors, or OS errno values
    // directly into this abstraction. Future adapters translate into these codes
    enum class StatusCode
    {
        kOk = 0,
        kInvalidArgument,
        KNotFound,
        KAlreadyExists,
        KFailedPrecondition,
        KUnavailable,
        KResourceExhausted,
        Kcancelled,
        KDeadlineExceeded,
        KInternal,
    };
    
    class Status final
    {
    public:
        Status() = default;
        static Status OK();
        static Status InvalidArgument(std::string message);
        static Status NotFound(std::string message);
        static Status AlreadyExists(std::string message);
        static Status FailedPrecondition(std::string message);
        static Status Unavailable(std::string message);
        static Status ResourceExhausted(std::string message);
        static Status Cancelled(std::string message);
        static Status DeadlineExceeded(std::string message);
        static Status Internal(std::string message);

        [[nodiscard]] bool ok() const noexcept
        {
            return code_ == StatusCode::kOk;
        }
        [[nodiscard]] StatusCode code() const noexcept
        {
            return code_;
        }
        [[nodiscard]] std::string_view message() const noexcept
        {
            return message_;
        }
        [[nodiscard]] std::string ToString() const;
        friend bool operator==(const Status& lhs,const Status& rhs) noexcept
        {
            return lhs.code_==rhs.code_ && lhs.message_==rhs.message_;
        }

    private:
    // Make the overloaded constructor private:
    /**
     * Instead of: Status(StatusCode::KNotFound,"Agent does not exist")
     * -> Not found != doesn't exist (Vocabulary and constructing error)
     * -> Instead: Status::NotFound("Agent doesn't exist");
     */
        Status(StatusCode code, std::string message);
        // Default value of code_ is okay
        StatusCode code_{StatusCode::kOk};
        std::string message_;
    };
    [[nodiscard]] std::string_view StatusCodeName(StatusCode code) noexcept;
}