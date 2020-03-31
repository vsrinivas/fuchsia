// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FITX_RESULT_H_
#define LIB_FITX_RESULT_H_

#include <lib/fitx/internal/result.h>
#include <lib/fitx/internal/type_traits.h>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

//
// General purpose fitx::result type for Zircon kernel, system, and above.
//
// fitx::result is an efficient C++ implementation of the result pattern found
// in many languages and vocabulary libraries. This implementation supports
// returning either an error value or zero/one non-error values from a
// function or method.
//

namespace fitx {

// Convenience type to indicate failure without elaboration.
//
// Example:
//
//   fitx::result<fitx::failed> Contains(const char* string, const char* find) {
//     if (string == nullptr || find == nullptr ||
//         strstr(string, find) == nullptr) {
//       return fitx::failed();
//     }
//     return fitx::ok();
//   }
//
struct failed {};

// Type representing an error value of type E to return as a result. Returning
// an error through fitx::result always requires using fitx::error to
// disambiguate errors from values.
//
// fitx::result<E, Ts...> is implicitly constructible from any fitx::error<F>,
// where E is constructible from F. This simplifies returning errors when the E
// has converting constructors.
//
// Example usage:
//
//   fitx::result<std::string, size_t> StringLength(const char* string) {
//     if (string == nullptr) {
//       return fitx::error("Argument to StringLength is nullptr!");
//     }
//     return fitx::success(strlen(string));
//   }
//
template <typename E>
class error {
 public:
  // Constructs an error with the given arguments.
  template <typename... Args,
            ::fitx::internal::requires_conditions<std::is_constructible<E, Args...>> = true>
  explicit constexpr error(Args&&... args) : value_(std::forward<Args>(args)...) {}

  ~error() = default;

  // Error has the same copyability and moveability as the underlying type E.
  constexpr error(const error&) = default;
  constexpr error& operator=(const error&) = default;
  constexpr error(error&&) = default;
  constexpr error& operator=(error&&) = default;

 private:
  template <typename F, typename... Ts>
  friend class result;

  E value_;
};

#if __cplusplus >= 201703L

// Deduction guide to simplify single argument error expressions in C++17.
template <typename T>
error(T) -> error<T>;

#endif

// Returns fitx::error<E> for the given value, where E is deduced from the
// argument type. This utility is a C++14 compatible alternative to the
// C++17 deduction guide above.
//
// Example:
//
//   fitx::result<std::string, std::string> MakeString(const char* string) {
//     if (string == nullptr) {
//       return fitx::as_error("String is nullptr!");
//     } else if (strlen(string) == 0) {
//       return fitx::as_error("String is empty!");
//     }
//     return fitx::ok(string);
//   }
//
template <typename E>
constexpr error<E> as_error(E&& error_value) {
  return error<E>(std::forward<E>(error_value));
}

// Type representing success with zero or one value.
//
// Base type.
template <typename... Ts>
class success;

// Type representing a success value of type T to return as a result. Returning
// a value through fitx::result always requires using fitx::success to
// disambiguate errors from values.
//
// fitx::result<E, T> is implicitly constructible from any fitx::success<U>, where
// T is constructible from U. This simplifies returning values when T has
// converting constructors.
//
// Example usage:
//
template <typename T>
class success<T> {
 public:
  // Constructs a success value with the given arguments.
  template <typename... Args,
            ::fitx::internal::requires_conditions<std::is_constructible<T, Args...>> = true>
  explicit constexpr success(Args&&... args) : value_(std::forward<Args>(args)...) {}

  ~success() = default;

  // Error has the same copyability and moveability as the underlying type E.
  constexpr success(const success&) = default;
  constexpr success& operator=(const success&) = default;
  constexpr success(success&&) = default;
  constexpr success& operator=(success&&) = default;

 private:
  template <typename E, typename... Ts>
  friend class result;

  T value_;
};

// Specialization of success for empty values.
template <>
class success<> {
 public:
  constexpr success() = default;
  ~success() = default;

  constexpr success(const success&) = default;
  constexpr success& operator=(const success&) = default;
  constexpr success(success&&) = default;
  constexpr success& operator=(success&&) = default;
};

#if __cplusplus >= 201703L

// Deduction guides to simplify zero and single argument success expressions in
// C++17.
success()->success<>;

template <typename T>
success(T) -> success<T>;

#endif

// Returns fitx::success<T> for the given value, where T is deduced from the
// argument type. This utility is a C++14 compatible alternative to the
// C++17 deduction guide above.
//
// Example:
//
//   fitx::result<std::string, std::string> MakeString(const char* string) {
//     if (string == nullptr) {
//       return fitx::as_error("String is nullptr!");
//     } else if (strlen(string) == 0) {
//       return fitx::as_error("String is empty!");
//     }
//     return fitx::ok(string);
//
template <typename T>
constexpr success<T> ok(T&& value) {
  return success<T>(std::forward<T>(value));
}

// Overload for empty value success.
constexpr success<> ok() { return success<>{}; }

// Result type representing either an error or zero/one return values.
//
// Base type.
template <typename E, typename... Ts>
class result;

// Specialization of result for one value type.
template <typename E, typename T>
class result<E, T> {
  static_assert(!::fitx::internal::is_success<E>,
                "fitx::success may not be used as the error type of fitx::result!");
  static_assert(!std::is_same<failed, std::decay_t<T>>::value,
                "fitx::failed may not be used as a value type of fitx::result!");

  template <typename U>
  using not_same = ::fitx::internal::negation<std::is_same<result, U>>;

  struct none {};
  using failed_or_none = std::conditional_t<std::is_same<failed, E>::value, failed, none>;

 public:
  // Result has the same trivial copyability and moveablity as E and T.
  constexpr result(const result&) = default;
  constexpr result& operator=(const result&) = default;
  constexpr result(result&&) = default;
  constexpr result& operator=(result&&) = default;

  // Implicit conversion from fitx::failed. This overload is only enabled when
  // the error type E is fitx::failed.
  constexpr result(failed_or_none) : storage_{::fitx::internal::error_v, failed{}} {}

  // Implicit conversion from success<U>, where T is constructible from U.
  template <typename U, ::fitx::internal::requires_conditions<std::is_constructible<T, U>> = true>
  constexpr result(success<U> success)
      : storage_{::fitx::internal::value_v, std::move(success.value_)} {}

  // Implicit conversion from error<F>, where E is constructible from F.
  template <typename F, ::fitx::internal::requires_conditions<std::is_constructible<E, F>> = true>
  constexpr result(error<F> error) : storage_{::fitx::internal::error_v, std::move(error.value_)} {}

  // Implicitly constructs a result from another result with compatible types.
  template <
      typename F, typename U,
      ::fitx::internal::requires_conditions<not_same<result<F, U>>, std::is_constructible<E, F>,
                                            std::is_constructible<T, U>> = true>
  constexpr result(result<F, U> other) : storage_{std::move(other.storage_)} {}

  // Predicates indicating whether the result contains a value or an error.
  // The positive values are mutually exclusive, however, both predicates are
  // negative when the result is default constructed to the empty state.
  constexpr bool has_value() const {
    return storage_.state == ::fitx::internal::state_e::has_value;
  }
  constexpr bool has_error() const {
    return storage_.state == ::fitx::internal::state_e::has_error;
  }

  // Accessors for the underlying error.
  //
  // May only be called when the result contains an error.
  constexpr E& error_value() {
    if (has_error()) {
      return storage_.error_or_value.error;
    }
    __builtin_abort();
  }
  constexpr const E& error_value() const {
    if (has_error()) {
      return storage_.error_or_value.error;
    }
    __builtin_abort();
  }

  // Moves the underlying error and returns it as an instance of fitx::error,
  // simplifying propagating the error to another fitx::result.
  //
  // May only be called when the result contains an error.
  constexpr error<E> take_error() {
    if (has_error()) {
      return error<E>(std::move(storage_.error_or_value.error));
    }
    __builtin_abort();
  }

  // Accessors for the underlying value.
  //
  // May only be called when the result contains a value.
  constexpr T& value() & {
    if (has_value()) {
      return storage_.error_or_value.value;
    }
    __builtin_abort();
  }
  constexpr const T& value() const& {
    if (has_value()) {
      return storage_.error_or_value.value;
    }
    __builtin_abort();
  }
  constexpr T&& value() && {
    if (has_value()) {
      return std::move(storage_.error_or_value.value);
    }
    __builtin_abort();
  }
  constexpr const T&& value() const&& {
    if (has_value()) {
      return std::move(storage_.error_or_value.value);
    }
    __builtin_abort();
  }

  // Moves the underlying value and returns it as an instance of fitx::success,
  // simplifying propagating the value to another fitx::result.
  //
  // May only be called when the result contains a value.
  constexpr success<T> take_value() {
    if (has_value()) {
      return success<T>(std::move(storage_.error_or_value.value));
    }
    __builtin_abort();
  }

  // Contingent accessors for the underlying value.
  //
  // Returns the value when the result has a value, otherwise returns the given
  // default value.
  template <typename U, ::fitx::internal::requires_conditions<std::is_constructible<T, U>> = true>
  constexpr T value_or(U&& default_value) const& {
    if (has_value()) {
      return storage_.error_or_value.value;
    }
    return static_cast<T>(std::forward<U>(default_value));
  }
  template <typename U, ::fitx::internal::requires_conditions<std::is_constructible<T, U>> = true>
  constexpr T value_or(U&& default_value) && {
    if (has_value()) {
      return std::move(storage_.error_or_value.value);
    }
    return static_cast<T>(std::forward<U>(default_value));
  }

  // Accessors for the members of the underyling value. These operators forward
  // to T::operator->() when defined, otherwise they provide direct access to T*.
  //
  // May only be called when the result contains a value.
  constexpr decltype(auto) operator->() {
    if (has_value()) {
      return ::fitx::internal::arrow_operator<T>::forward(storage_.error_or_value.value);
    }
    __builtin_abort();
  }
  constexpr decltype(auto) operator->() const {
    if (has_value()) {
      return ::fitx::internal::arrow_operator<T>::forward(storage_.error_or_value.value);
    }
    __builtin_abort();
  }

 protected:
  // Default constructs a result in empty state.
  constexpr result() = default;

  // Reset is not a recommended operation for the general result pattern. This
  // method is provided for derived types that need it for specific use cases.
  constexpr void reset() { storage_.reset(); }

 private:
  template <typename, typename...>
  friend class result;

  ::fitx::internal::storage<E, T> storage_;
};

// Specialization of the result type for zero values.
template <typename E>
class result<E> {
  static_assert(!::fitx::internal::is_success<E>,
                "fitx::success may not be used as the error type of fitx::result!");

  template <typename U>
  using not_same = ::fitx::internal::negation<std::is_same<result, U>>;

  template <size_t>
  struct none {};
  using failure_or_none = std::conditional_t<std::is_same<failed, E>::value, failed, none<1>>;

 public:
  // Result has the same trivial copyability and moveablity as E and the
  // elements of Ts.
  constexpr result(const result&) = default;
  constexpr result& operator=(const result&) = default;
  constexpr result(result&&) = default;
  constexpr result& operator=(result&&) = default;

  // Implicit conversion from fitx::failure. This overload is only enabled when
  // the error type E is fitx::failed.
  constexpr result(failure_or_none) : storage_{::fitx::internal::error_v, failed{}} {}

  // Implicit conversion from fitx::success<>.
  constexpr result(success<>) : storage_{::fitx::internal::value_v} {}

  // Implicit conversion from error<F>, where E is constructible from F.
  template <typename F, ::fitx::internal::requires_conditions<std::is_constructible<E, F>> = true>
  constexpr result(error<F> error) : storage_{::fitx::internal::error_v, std::move(error.value_)} {}

  // Implicitly constructs a result from another result with compatible types.
  template <typename F, ::fitx::internal::requires_conditions<not_same<result<F>>,
                                                              std::is_constructible<E, F>> = true>
  constexpr result(result<F> other) : storage_{std::move(other.storage_)} {}

  // Predicates indicating whether the result contains a value or an error.
  constexpr bool has_value() const {
    return storage_.state == ::fitx::internal::state_e::has_value;
  }
  constexpr bool has_error() const {
    return storage_.state == ::fitx::internal::state_e::has_error;
  }

  // Accessors for the underlying error.
  //
  // May only be called when the result contains an error.
  constexpr E& error_value() {
    if (has_error()) {
      return storage_.error_or_value.error;
    }
    __builtin_abort();
  }
  constexpr const E& error_value() const {
    if (has_error()) {
      return storage_.error_or_value.error;
    }
    __builtin_abort();
  }

  // Moves the underlying error and returns it as an instance of fitx::error,
  // simplifying propagating the error to another fitx::result.
  //
  // May only be called when the result contains an error.
  constexpr error<E> take_error() {
    if (has_error()) {
      return error<E>(std::move(storage_.error_or_value.error));
    }
    __builtin_abort();
  }

 protected:
  // Default constructs a result in empty state.
  constexpr result() = default;

  // Reset is not a recommended operation for the general result pattern. This
  // method is provided for derived types that need it for specific use cases.
  constexpr void reset() { storage_.reset(); }

 private:
  template <typename, typename...>
  friend class result;

  ::fitx::internal::storage<E> storage_;
};

// Relational Operators.
//
// Results are comparable to the follownig types:
//  * Other results with the same arity when the value types are comparable.
//  * Any type that is comparable to the value type when the arity is 1.
//  * Any instance of fitx::success<> (i.e. fitx::ok()).
//  * Any instance of fitx::failed.
//
// Result comparisons behave similarly to std::optional<T>, having the same
// empty and non-empty lexicographic ordering. A non-value result behaves like
// an empty std::optional, regardless of the value of the actual error. Error
// values are never compared, only the has_value() predicate and result values
// are considered in comparisons.
//

// Equal/not equal to fitx::success.
template <typename E, typename... Ts>
constexpr bool operator==(const result<E, Ts...>& lhs, const success<>&) {
  return lhs.has_value();
}
template <typename E, typename... Ts>
constexpr bool operator!=(const result<E, Ts...>& lhs, const success<>&) {
  return !lhs.has_value();
}

template <typename E, typename... Ts>
constexpr bool operator==(const success<>&, const result<E, Ts...>& rhs) {
  return rhs.has_value();
}
template <typename E, typename... Ts>
constexpr bool operator!=(const success<>&, const result<E, Ts...>& rhs) {
  return !rhs.has_value();
}

// Equal/not equal to fitx::failed.
template <typename E, typename... Ts>
constexpr bool operator==(const result<E, Ts...>& lhs, failed) {
  return lhs.has_error();
}
template <typename E, typename... Ts>
constexpr bool operator!=(const result<E, Ts...>& lhs, failed) {
  return !lhs.has_error();
}

template <typename E, typename... Ts>
constexpr bool operator==(failed, const result<E, Ts...>& rhs) {
  return rhs.has_error();
}
template <typename E, typename... Ts>
constexpr bool operator!=(failed, const result<E, Ts...>& rhs) {
  return !rhs.has_error();
}

// Equal/not equal.
template <typename E, typename F, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() == std::declval<U>())> = true>
constexpr bool operator==(const result<E, T>& lhs, const result<F, U>& rhs) {
  return (lhs.has_value() == rhs.has_value()) && (!lhs.has_value() || lhs.value() == rhs.value());
}
template <typename E, typename F, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() != std::declval<U>())> = true>
constexpr bool operator!=(const result<E, T>& lhs, const result<F, U>& rhs) {
  return (lhs.has_value() != rhs.has_value()) || (lhs.has_value() && lhs.value() != rhs.value());
}

template <typename E, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() == std::declval<U>()),
                                          ::fitx::internal::not_result_type<U>> = true>
constexpr bool operator==(const result<E, T>& lhs, const U& rhs) {
  return lhs.has_value() && lhs.value() == rhs;
}
template <typename E, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() != std::declval<U>()),
                                          ::fitx::internal::not_result_type<U>> = true>
constexpr bool operator!=(const result<E, T>& lhs, const U& rhs) {
  return !lhs.has_value() || lhs.value() != rhs;
}

template <typename F, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() == std::declval<U>()),
                                          ::fitx::internal::not_result_type<T>> = true>
constexpr bool operator==(const T& lhs, const result<F, U>& rhs) {
  return rhs.has_value() && lhs == rhs.value();
}
template <typename F, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() != std::declval<U>()),
                                          ::fitx::internal::not_result_type<T>> = true>
constexpr bool operator!=(const T& lhs, const result<F, U>& rhs) {
  return !rhs.has_value() || lhs != rhs.value();
}

// Less than/greater than.
template <typename E, typename F, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() < std::declval<U>())> = true>
constexpr bool operator<(const result<E, T>& lhs, const result<F, U>& rhs) {
  return rhs.has_value() && (!lhs.has_value() || lhs.value() < rhs.value());
}
template <typename E, typename F, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() > std::declval<U>())> = true>
constexpr bool operator>(const result<E, T>& lhs, const result<F, U>& rhs) {
  return lhs.has_value() && (!rhs.has_value() || lhs.value() > rhs.value());
}

template <typename E, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() < std::declval<U>()),
                                          ::fitx::internal::not_result_type<U>> = true>
constexpr bool operator<(const result<E, T>& lhs, const U& rhs) {
  return !lhs.has_value() || lhs.value() < rhs;
}
template <typename E, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() > std::declval<U>()),
                                          ::fitx::internal::not_result_type<U>> = true>
constexpr bool operator>(const result<E, T>& lhs, const U& rhs) {
  return lhs.has_value() && lhs.value() > rhs;
}

template <typename F, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() < std::declval<U>()),
                                          ::fitx::internal::not_result_type<T>> = true>
constexpr bool operator<(const T& lhs, const result<F, U>& rhs) {
  return rhs.has_value() && lhs < rhs.value();
}
template <typename F, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() > std::declval<U>()),
                                          ::fitx::internal::not_result_type<T>> = true>
constexpr bool operator>(const T& lhs, const result<F, U>& rhs) {
  return !rhs.has_value() || lhs > rhs.value();
}

// Less than or equal/greater than or equal.
template <typename E, typename F, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() <= std::declval<U>())> = true>
constexpr bool operator<=(const result<E, T>& lhs, const result<F, U>& rhs) {
  return !lhs.has_value() || (rhs.has_value() && lhs.value() <= rhs.value());
}
template <typename E, typename F, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() >= std::declval<U>())> = true>
constexpr bool operator>=(const result<E, T>& lhs, const result<F, U>& rhs) {
  return !rhs.has_value() || (lhs.has_value() && lhs.value() >= rhs.value());
}

template <typename E, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() <= std::declval<U>()),
                                          ::fitx::internal::not_result_type<U>> = true>
constexpr bool operator<=(const result<E, T>& lhs, const U& rhs) {
  return !lhs.has_value() || lhs.value() <= rhs;
}
template <typename E, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() >= std::declval<U>()),
                                          ::fitx::internal::not_result_type<U>> = true>
constexpr bool operator>=(const result<E, T>& lhs, const U& rhs) {
  return lhs.has_value() && lhs.value() >= rhs;
}

template <typename F, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() <= std::declval<U>()),
                                          ::fitx::internal::not_result_type<T>> = true>
constexpr bool operator<=(const T& lhs, const result<F, U>& rhs) {
  return rhs.has_value() && lhs <= rhs.value();
}
template <typename F, typename T, typename U,
          ::fitx::internal::enable_rel_op<decltype(std::declval<T>() >= std::declval<U>()),
                                          ::fitx::internal::not_result_type<T>> = true>
constexpr bool operator>=(const T& lhs, const result<F, U>& rhs) {
  return !rhs.has_value() || lhs >= rhs.value();
}

}  // namespace fitx

#endif  // LIB_FITX_RESULT_H_
