// Status Code represent:
// 1. StatusCode(enum class) code_
// 2. std::string message_
// Ex: Status status = Status::NotFound("Agent does not exist");

#include "dar/common/status.h"
#include <utility>


namespace dar
{
    Status::Status(StatusCode code, std::string message): code_(code),message_(message){}
    //From .h file, default value for code_ is kOk,
    // So this, we can just call default Status object
    Status Status::OK()
    {
        return Status{};
    }

    //
    Status Status::InvalidArgument(std::string message)
    {
        // std::move -> type cast that convert a named variable into an rvalue refernce (the resource could be stolen)
        return Status(StatusCode::kInvalidArgument,std::move(message));
    }

    /**
     * Usage of move, same resource flow:
     * Caller string -> NotFound(message)-move->Status constructor message-move-> message_
     */
    Status Status::NotFound(std::string message)
    {
        // std::move -> tpe cast hat convert a named variable into an rvalue refernce (the resource could be stolen)
        return Status(StatusCode::KNotFound,std::move(message));
    }

    Status Status::AlreadyExists(std::string message)
    {
        // std::move -> tpe cast hat convert a named variable into an rvalue refernce (the resource could be stolen)
        return Status(StatusCode::KAlreadyExists,std::move(message));
    }

    Status Status::FailedPrecondition(std::string message)
    {
        // std::move -> tpe cast hat convert a named variable into an rvalue refernce (the resource could be stolen)
        return Status(StatusCode::KFailedPrecondition,std::move(message));
    }

    Status Status::Unavailable(std::string message)
    {
        // std::move -> tpe cast hat convert a named variable into an rvalue refernce (the resource could be stolen)
        return Status(StatusCode::KUnavailable,std::move(message));
    }

    Status Status::ResourceExhausted(std::string message)
    {
        // std::move -> tpe cast hat convert a named variable into an rvalue refernce (the resource could be stolen)
        return Status(StatusCode::KResourceExhausted,std::move(message));
    }

    Status Status::Cancelled(std::string message)
    {
        // std::move -> tpe cast hat convert a named variable into an rvalue refernce (the resource could be stolen)
        return Status(StatusCode::Kcancelled,std::move(message));
    }

        Status Status::DeadlineExceeded(std::string message)
    {
        // std::move -> tpe cast hat convert a named variable into an rvalue refernce (the resource could be stolen)
        return Status(StatusCode::KDeadlineExceeded,std::move(message));
    }

    // Program error (previous are human errors)
    Status Status::Internal(std::string message)
    {
        // std::move -> tpe cast hat convert a named variable into an rvalue refernce (the resource could be stolen)
        return Status(StatusCode::KInternal,std::move(message));
    }

    std::string Status::ToString() const
    {
        if(ok())
        {
            return "OK";
        }
        // Assign result the value (why not {}) of the codeName
        std::string result(StatusCodeName(code_));
        if(!message_.empty())
        {
            result += ": ";
            result += message_;
        }
        return result;
    }

    std::string_view StatusCodeName(StatusCode code) noexcept
    {
        switch(code)
        {
            case StatusCode::kOk:
                return "OK";
            case StatusCode::kInvalidArgument:
                return "Invalid_Argument";
            case StatusCode::KNotFound:
                return "Not_Found";
            case StatusCode::KAlreadyExists:
                return "Already_Exists";
            case StatusCode::KFailedPrecondition:
                return "Failed_Precondition";
            case StatusCode::KUnavailable:
                return "Unavailable";
            case StatusCode::Kcancelled:
                return "Cancelled";
            case StatusCode::KDeadlineExceeded:
                return "Deadline_Exceeded";
            case StatusCode::KInternal:
                return "Internal";
        }
        return "Unknown";
    }

}