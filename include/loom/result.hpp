#pragma once

#include <loom/error.hpp>
#include <variant>
#include <functional>

namespace loom {

template<typename T>
class Result {
    std::variant<T, LoomError> data_;

    explicit Result(T val) : data_(std::move(val)) {}

public:
    // Implicit from LoomError so LOOM_TRY can return errors across Result<T> types
    Result(LoomError err) : data_(std::move(err)) {}
    static Result ok(T val) { return Result(std::move(val)); }
    static Result err(LoomError e) { return Result(std::move(e)); }

    bool is_ok() const { return std::holds_alternative<T>(data_); }
    bool is_err() const { return std::holds_alternative<LoomError>(data_); }

    T& value() & { return std::get<T>(data_); }
    const T& value() const& { return std::get<T>(data_); }
    T&& value() && { return std::get<T>(std::move(data_)); }

    LoomError& error() & { return std::get<LoomError>(data_); }
    const LoomError& error() const& { return std::get<LoomError>(data_); }
    LoomError&& error() && { return std::get<LoomError>(std::move(data_)); }

    explicit operator bool() const { return is_ok(); }

    template<typename F>
    auto map(F&& f) -> Result<decltype(f(std::declval<T&>()))> {
        using U = decltype(f(std::declval<T&>()));
        if (is_ok()) {
            return Result<U>::ok(f(value()));
        }
        return Result<U>::err(error());
    }

    template<typename F>
    auto and_then(F&& f) -> decltype(f(std::declval<T&>())) {
        if (is_ok()) {
            return f(value());
        }
        using RetType = decltype(f(std::declval<T&>()));
        return RetType::err(error());
    }

    template<typename F>
    Result or_else(F&& f) {
        if (is_ok()) {
            return *this;
        }
        return f(error());
    }
};

using Status = Result<std::monostate>;

inline Status ok_status() {
    return Status::ok(std::monostate{});
}

#define LOOM_TRY(expr) \
    do { \
        auto _loom_result = (expr); \
        if (_loom_result.is_err()) return std::move(_loom_result).error(); \
    } while(0)

} // namespace loom
