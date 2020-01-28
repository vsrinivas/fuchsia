// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef FFL_SATURATING_ARITHMETIC_H_
#define FFL_SATURATING_ARITHMETIC_H_

#include <limits>
#include <type_traits>

#include <ffl/utility.h>

namespace ffl {

// Tag type to specify the result type of saturating arithmetic functions.
template <typename Result>
struct ResultAsType {};

// Provides an instance of the tag type above to streamline function call
// expressions.
template <typename Result>
inline constexpr auto ResultAs = ResultAsType<Result>{};

namespace internal {

template <typename T, typename U, typename Result, typename Alternate>
using ResultType = std::conditional_t<(sizeof(T) > sizeof(Result) || sizeof(U) > sizeof(Result)),
                                      Alternate, Result>;

}  // namespace internal

// Returns the saturated result of addition on the given integer values. By
// default, the type of the result is deduced from the argument types using the
// normal integer promotion rules. The default can be overridden by passing
// ResultAs<type> as the final argument.
template <typename T, typename U, typename Result = decltype(std::declval<T>() + std::declval<U>())>
constexpr Result SaturateAdd(T a, U b, ResultAsType<Result> = ResultAs<Result>) {
  using Intermediate =
      internal::ResultType<T, U, Result, decltype(std::declval<T>() + std::declval<U>())>;
  Intermediate result = 0;
  if (__builtin_add_overflow(a, b, &result)) {
    return a < 0 || b < 0 ? std::numeric_limits<Result>::min() : std::numeric_limits<Result>::max();
  }
  return ClampCast<Result>(result);
}

// Returns the saturated result of addition on the given integer values, using
// the given integer result type.
template <typename Result, typename T, typename U>
constexpr Result SaturateAddAs(T a, U b) {
  return SaturateAdd(a, b, ResultAs<Result>);
}

// Returns the saturated result of subtraction on the given integer values. By
// default, the type of the result is deduced from the argument types using the
// normal integer promotion rules. The default can be overridden by passing
// ResultAs<type> as the final argument.
template <typename T, typename U, typename Result = decltype(std::declval<T>() - std::declval<U>())>
constexpr Result SaturateSubtract(T a, U b, ResultAsType<Result> = ResultAs<Result>) {
  using Intermediate =
      internal::ResultType<T, U, Result, decltype(std::declval<T>() - std::declval<U>())>;
  Intermediate result = 0;
  if (__builtin_sub_overflow(a, b, &result)) {
    return a >= 0 && b < 0 ? std::numeric_limits<Result>::max()
                           : std::numeric_limits<Result>::min();
  }
  return ClampCast<Result>(result);
}

// Returns the saturated result of subtraction on the given integer values,
// using the given integer result type.
template <typename Result, typename T, typename U>
constexpr Result SaturateSubtractAs(T a, U b) {
  return SaturateSubtract(a, b, ResultAs<Result>);
}

// Returns the saturated result of multiplication on the given integer values.
// By default, the type of the result is deduced from the argument types using
// the normal integer promotion rules. The default can be overridden by passing
// ResultAs<type> as the final argument.
template <typename T, typename U, typename Result = decltype(std::declval<T>() * std::declval<U>())>
constexpr Result SaturateMultiply(T a, U b, ResultAsType<Result> = ResultAs<Result>) {
  Result result = 0;
  if (__builtin_mul_overflow(a, b, &result)) {
    return (a < 0) ^ (b < 0) ? std::numeric_limits<Result>::min()
                             : std::numeric_limits<Result>::max();
  }
  return result;
}

// Returns the saturated result of multiplication on the given integer values,
// using the given integer result type.
template <typename Result, typename T, typename U>
constexpr Result SaturateMultiplyAs(T a, U b) {
  return SaturateMultiply(a, b, ResultAs<Result>);
}

}  // namespace ffl

#endif  // FFL_SATURATING_ARITHMETIC_H_
