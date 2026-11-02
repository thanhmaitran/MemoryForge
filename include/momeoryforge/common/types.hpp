#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace memoryforge {

/// Error categories shared by every module. Functions that can fail for
/// reasons the caller is expected to handle return Result<T> or Status
/// instead of throwing.
enum class ErrorCode {
    InvalidArgument,
    ZeroSize,
    InvalidAlignment,
    SizeOverflow,
    OutOfMemory,
    InvalidHandle,
    DoubleFree,
    OutOfRange,
    ParseError,
    InvalidConfig,
    IoError,
    ValidationFailed,
};

constexpr std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::InvalidArgument: return "invalid_argument";
        case ErrorCode::ZeroSize: return "zero_size";
        case ErrorCode::InvalidAlignment: return "invalid_alignment";
        case ErrorCode::SizeOverflow: return "size_overflow";
        case ErrorCode::OutOfMemory: return "out_of_memory";
        case ErrorCode::InvalidHandle: return "invalid_handle";
        case ErrorCode::DoubleFree: return "double_free";
        case ErrorCode::OutOfRange: return "out_of_range";
        case ErrorCode::ParseError: return "parse_error";
        case ErrorCode::InvalidConfig: return "invalid_config";
        case ErrorCode::IoError: return "io_error";
        case ErrorCode::ValidationFailed: return "validation_failed";
    }
    return "unknown";
}

struct Error {
    ErrorCode code;
    std::string message;
};

}  // namespace memoryforge
