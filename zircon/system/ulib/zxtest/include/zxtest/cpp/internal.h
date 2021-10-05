// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_CPP_INTERNAL_H_
#define ZXTEST_CPP_INTERNAL_H_

#include <type_traits>

#include <fbl/macros.h>
#include <fbl/string.h>
#include <zxtest/base/assertion.h>
#include <zxtest/base/runner.h>
#include <zxtest/base/types.h>

#define _ZXTEST_CHECK_RUNNING()                                                                \
  do {                                                                                         \
    ZX_ASSERT_MSG(zxtest::Runner::GetInstance()->IsRunning(), "See Context Check in README."); \
  } while (0)

namespace zxtest {
namespace internal {

// Used for delegating an assertion evaluation. to a lambda.
template <typename Actual, typename Expected, typename Comparer>
bool Compare(const Actual& actual, const Expected& expected, const Comparer& compare) {
  if constexpr (std::is_integral<Actual>::value && std::is_integral<Expected>::value) {
    return compare(static_cast<typename std::common_type<Actual, Expected>::type>(actual),
                   static_cast<typename std::common_type<Actual, Expected>::type>(expected));
  } else {
    return compare(actual, expected);
  }
}

// Alternative when there exists an implicit conversion to pointer, which allows
// VLAs to be compared.
template <typename Actual, typename Expected, typename Comparer>
bool Compare(const Actual* a, const Expected* e, const Comparer& compare) {
  return compare(a, e);
}

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_status_value, status_value, zx_status_t (C::*)() const);
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_status, status, zx_status_t (C::*)() const);

// Evaluates a condition and returns true if it is satisfied. If it is not, will create an assertion
// and notify the global runner instance.
template <typename Actual, typename Expected, typename CompareOp, typename PrintActual,
          typename PrintExpected, typename DescGenerator>
bool EvaluateStatusCondition(const Actual& actual, const Expected& expected,
                             const char* actual_symbol, const char* expected_symbol,
                             const zxtest::SourceLocation& location, bool is_fatal,
                             const DescGenerator& description, const CompareOp& compare,
                             const PrintActual& print_actual, const PrintExpected& print_expected) {
  zx_status_t actual_status;
  if constexpr (has_status_value_v<Actual>) {
    actual_status = actual.status_value();
  } else if constexpr (has_status_v<Actual>) {
    actual_status = actual.status();
  } else {
    actual_status = actual;
  }
  if (compare(actual_status, expected)) {
    return true;
  }

  // Report the assertion error.
  fbl::String actual_value = print_actual(actual_status);
  fbl::String expected_value = print_expected(expected);
  Assertion assertion(description(), expected_symbol, expected_value, actual_symbol, actual_value,
                      location, is_fatal);
  zxtest::Runner::GetInstance()->NotifyAssertion(assertion);
  return false;
}

// Evaluates a condition and returns true if it is satisfied. If it is not, will create an assertion
// and notify the global runner instance.
template <typename Actual, typename Expected, typename CompareOp, typename PrintActual,
          typename PrintExpected, typename DescGenerator>
bool EvaluateCondition(const Actual& actual, const Expected& expected, const char* actual_symbol,
                       const char* expected_symbol, const zxtest::SourceLocation& location,
                       bool is_fatal, const DescGenerator& description, const CompareOp& compare,
                       const PrintActual& print_actual, const PrintExpected& print_expected) {
  if (compare(actual, expected)) {
    return true;
  }

  // Report the assertion error.
  fbl::String actual_value = print_actual(actual);
  fbl::String expected_value = print_expected(expected);
  Assertion assertion(description(), expected_symbol, expected_value, actual_symbol, actual_value,
                      location, is_fatal);
  zxtest::Runner::GetInstance()->NotifyAssertion(assertion);
  return false;
}

// Force Array to become a pointer, has no effect in pointers.
template <typename T>
const T* ToPointer(const T* value) {
  return value;
}

}  // namespace internal
}  // namespace zxtest

#endif  // ZXTEST_CPP_INTERNAL_H_
