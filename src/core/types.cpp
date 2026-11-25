#include "luma/core/types.hpp"

#include <chrono>

namespace luma::core {

SteadyMs steadyNowMs() noexcept {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

const char* errorCodeName(ErrorCode c) noexcept {
    switch (c) {
        case ErrorCode::kOk:
            return "kOk";
        case ErrorCode::kInvalidArg:
            return "kInvalidArg";
        case ErrorCode::kSafetyViolation:
            return "kSafetyViolation";
        case ErrorCode::kIoError:
            return "kIoError";
        case ErrorCode::kTimeout:
            return "kTimeout";
        case ErrorCode::kChecksumError:
            return "kChecksumError";
        case ErrorCode::kProtocolError:
            return "kProtocolError";
        case ErrorCode::kNotConnected:
            return "kNotConnected";
        case ErrorCode::kQueueFull:
            return "kQueueFull";
        case ErrorCode::kStaleFrame:
            return "kStaleFrame";
        case ErrorCode::kAlreadyExists:
            return "kAlreadyExists";
        case ErrorCode::kNotFound:
            return "kNotFound";
        case ErrorCode::kBusy:
            return "kBusy";
        case ErrorCode::kNotSupported:
            return "kNotSupported";
        case ErrorCode::kHardwareFault:
            return "kHardwareFault";
        case ErrorCode::kCancelled:
            return "kCancelled";
    }
    return "kUnknown";
}

}  // namespace luma::core
