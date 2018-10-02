// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_RESULT_H_
#define LIB_FIT_RESULT_H_

#include <assert.h>

#include <new>
#include <type_traits>
#include <utility>

#include "traits.h"
#include "variant.h"

namespace fit {

// Represents the intermediate state of a result that has not yet completed.
struct pending_result final {};

// Returns an value that represents a pending result.
constexpr inline pending_result pending() {
    return pending_result{};
}

// Represents the result of a successful task.
template <typename V = void>
struct ok_result final {
    using value_type = V;

    explicit constexpr ok_result(V value)
        : value(std::move(value)) {}

    V value;
};
template <>
struct ok_result<void> {
    using value_type = void;
};

// Wraps the result of a successful task as an |ok_result<T>|.
template <typename V>
constexpr inline ok_result<V> ok(V value) {
    return ok_result<V>(std::move(value));
}
constexpr inline ok_result<> ok() {
    return ok_result<>{};
}

// Represents the result of a failed task.
template <typename E = void>
struct error_result final {
    using error_type = E;

    explicit constexpr error_result(E error)
        : error(std::move(error)) {}

    E error;
};
template <>
struct error_result<void> {
    using error_type = void;
};

// Wraps the result of a failed task as an |error_result<T>|.
template <typename E>
constexpr inline error_result<E> error(E error) {
    return error_result<E>(std::move(error));
}
constexpr inline error_result<> error() {
    return error_result<>{};
}

// Describes the status of a task's result.
enum class result_state {
    // The task is still in progress.
    pending,
    // The task completed successfully.
    ok,
    // The task failed.
    error
};

// Represents the result of a task which may have succeeded, failed,
// or still be in progress.
//
// Use |fit::pending()|, |fit::ok<T>()|, or |fit::error<T>| to initialize
// the result.
//
// |V| is the type of value produced when the completes successfully.
// Defaults to |void|.
//
// |E| is the type of error produced when the completes with an error.
// Defaults to |void|.
//
// EXAMPLE:
//
// fit::result<int, std::string> divide(int dividend, int divisor) {
//     if (divisor == 0)
//         return fit::error<std::string>("divide by zero");
//     return fit::ok(dividend / divisor);
// }
//
// int try_divide(int dividend, int divisor) {
//     auto result = divide(dividend, divisor);
//     if (result.is_ok()) {
//         printf("%d / %d = %d\n", dividend, divisor, result.value());
//         return result.value();
//     }
//     printf("%d / %d: ERROR %s\n", dividend, divisor, result.error().c_str());
//     return -999;
// }
//
// EXAMPLE WITH VOID RESULT VALUE AND ERROR:
//
// fit::result<> open(std::string secret) {
//     printf("guessing \"%s\"\n", secret.c_str());
//     if (secret == "sesame") {
//         return fit::ok();
//         puts("yes!");
//     }
//     puts("no.");
//     return fit::error();
// }
//
// bool guess_combination() {
//     return open("friend") || open("sesame") || open("I give up");
// }
template <typename V = void, typename E = void>
class result final {
public:
    using value_type = V;
    using error_type = E;

    // Creates a pending result.
    constexpr result() = default;
    constexpr result(pending_result) {}

    // Creates an ok result.
    constexpr result(ok_result<V> result)
        : state_(::fit::internal::in_place_index<1>, std::move(result)) {}
    template <typename OtherV,
              typename = std::enable_if_t<std::is_constructible<V, OtherV>::value>>
    constexpr result(ok_result<OtherV> other)
        : state_(::fit::internal::in_place_index<1>,
                 fit::ok<V>(std::move(other.value))) {}

    // Creates an error result.
    constexpr result(error_result<E> result)
        : state_(::fit::internal::in_place_index<2>, std::move(result)) {}
    template <typename OtherE,
              typename = std::enable_if_t<std::is_constructible<E, OtherE>::value>>
    constexpr result(error_result<OtherE> other)
        : state_(::fit::internal::in_place_index<2>,
                 fit::error<E>(std::move(other.error))) {}

    // Copies another result (if copyable).
    result(const result& other) = default;

    // Moves from another result, leaving the other one in a pending state.
    result(result&& other)
        : state_(std::move(other.state_)) {
        other.reset();
    }

    ~result() = default;

    // Returns the state of the task's result: pending, ok, or error.
    constexpr result_state state() const {
        return static_cast<result_state>(state_.index());
    }

    // Returns true if the result is not pending.
    constexpr explicit operator bool() const {
        return !is_pending();
    }

    // Returns true if the task is still in progress.
    constexpr bool is_pending() const {
        return state() == result_state::pending;
    }

    // Returns true if the task succeeded.
    constexpr bool is_ok() const {
        return state() == result_state::ok;
    }

    // Returns true if the task failed.
    constexpr bool is_error() const {
        return state() == result_state::error;
    }

    // Gets the result's value.
    // Asserts that the result's state is |fit::result_state::ok|.
    template <typename R = V,
              typename = std::enable_if_t<!std::is_void<R>::value>>
    constexpr R& value() {
        return state_.template get<1>().value;
    }
    template <typename R = V,
              typename = std::enable_if_t<!std::is_void<R>::value>>
    constexpr const R& value() const {
        return state_.template get<1>().value;
    }

    // Takes the result's value, leaving it in a pending state.
    // Asserts that the result's state is |fit::result_state::ok|.
    template <typename R = V,
              typename = std::enable_if_t<!std::is_void<R>::value>>
    R take_value() {
        auto value = std::move(state_.template get<1>().value);
        reset();
        return value;
    }
    ok_result<V> take_ok_result() {
        auto result = std::move(state_.template get<1>());
        reset();
        return result;
    }

    // Gets a reference to the result's error.
    // Asserts that the result's state is |fit::result_state::error|.
    template <typename R = E,
              typename = std::enable_if_t<!std::is_void<R>::value>>
    constexpr R& error() {
        return state_.template get<2>().error;
    }
    template <typename R = E,
              typename = std::enable_if_t<!std::is_void<R>::value>>
    constexpr const R& error() const {
        return state_.template get<2>().error;
    }

    // Takes the result's error, leaving it in a pending state.
    // Asserts that the result's state is |fit::result_state::error|.
    template <typename R = E,
              typename = std::enable_if_t<!std::is_void<R>::value>>
    R take_error() {
        auto error = std::move(state_.template get<2>().error);
        reset();
        return error;
    }
    error_result<E> take_error_result() {
        auto result = std::move(state_.template get<2>());
        reset();
        return result;
    }

    // Assigns from another result (if copyable).
    result& operator=(const result& other) = default;

    // Moves from another result, leaving the other one in a pending state.
    result& operator=(result&& other) {
        state_ = std::move(other.state_);
        other.reset();
        return *this;
    }

private:
    void reset() { state_.template emplace<0>(); }

    ::fit::internal::variant<
        ::fit::internal::monostate, ok_result<V>, error_result<E>>
        state_;
};

} // namespace fit

#endif // LIB_FIT_RESULT_H_
