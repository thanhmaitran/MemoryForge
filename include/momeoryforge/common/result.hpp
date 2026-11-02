#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include "memoryforge/common/types.hpp"

namespace memoryforge {

inline Error make_error(ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

/// Either a value of type T or an Error. Accessing the wrong alternative is a
/// programming error and throws std::logic_error.
template <typename T>
class Result {
public:
    Result(T value) : storage_(std::move(value)) {}      // NOLINT: implicit by design
    Result(Error error) : storage_(std::move(error)) {}  // NOLINT: implicit by design

    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(storage_); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T& value() & {
        require_value();
        return std::get<T>(storage_);
    }
    [[nodiscard]] const T& value() const& {
        require_value();
        return std::get<T>(storage_);
    }
    [[nodiscard]] T&& value() && {
        require_value();
        return std::get<T>(std::move(storage_));
    }
    [[nodiscard]] const Error& error() const {
        if (ok()) throw std::logic_error("Result::error() called on a success value");
        return std::get<Error>(storage_);
    }

private:
    void require_value() const {
        if (!ok()) {
            throw std::logic_error("Result::value() called on an error: " +
                                   std::get<Error>(storage_).message);
        }
    }
    std::variant<T, Error> storage_;
};

/// Success-or-Error for operations that return no value.
class Status {
public:
    Status() = default;
    Status(Error error) : error_(std::move(error)) {}  // NOLINT: implicit by design

    [[nodiscard]] static Status success() { return Status{}; }
    [[nodiscard]] bool ok() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }
    [[nodiscard]] const Error& error() const {
        if (ok()) throw std::logic_error("Status::error() called on success");
        return *error_;
    }

private:
    std::optional<Error> error_;
};

}  // namespace memoryforge
