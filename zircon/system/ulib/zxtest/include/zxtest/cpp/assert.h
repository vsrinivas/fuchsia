// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_CPP_ASSERT_H_
#define ZXTEST_CPP_ASSERT_H_

#include <zxtest/cpp/internal.h>

// Basic assert macro implementation.
#define LIB_ZXTEST_CHECK_VAR(op, expected, actual, fatal, file, line, desc, ...)                  \
  do {                                                                                            \
    LIB_ZXTEST_CHECK_RUNNING();                                                                   \
    if (!zxtest::internal::EvaluateCondition(                                                     \
            actual, expected, #actual, #expected, {.filename = file, .line_number = line}, fatal, \
            LIB_ZXTEST_DESC_PROVIDER(desc, __VA_ARGS__), LIB_ZXTEST_COMPARE_FN(op),               \
            LIB_ZXTEST_DEFAULT_PRINTER, LIB_ZXTEST_DEFAULT_PRINTER)) {                            \
      LIB_ZXTEST_RETURN_IF_FATAL(fatal);                                                          \
    }                                                                                             \
  } while (0)

#define LIB_ZXTEST_CHECK_VAR_STATUS(op, expected, actual, fatal, file, line, desc, ...)           \
  do {                                                                                            \
    LIB_ZXTEST_CHECK_RUNNING();                                                                   \
    if (!zxtest::internal::EvaluateStatusCondition(                                               \
            actual, expected, #actual, #expected, {.filename = file, .line_number = line}, fatal, \
            LIB_ZXTEST_DESC_PROVIDER(desc, __VA_ARGS__), LIB_ZXTEST_COMPARE_FN(op),               \
            LIB_ZXTEST_STATUS_PRINTER, LIB_ZXTEST_STATUS_PRINTER)) {                              \
      LIB_ZXTEST_RETURN_IF_FATAL(fatal);                                                          \
    }                                                                                             \
  } while (0)

#define LIB_ZXTEST_CHECK_VAR_COERCE(op, expected, actual, coerce_type, fatal, file, line, desc,    \
                                    ...)                                                           \
  do {                                                                                             \
    LIB_ZXTEST_CHECK_RUNNING();                                                                    \
    auto buffer_compare = [&](const auto& expected_, const auto& actual_) {                        \
      using DecayType = typename std::decay<coerce_type>::type;                                    \
      return op(static_cast<const DecayType&>(actual_), static_cast<const DecayType&>(expected_)); \
    };                                                                                             \
    if (!zxtest::internal::EvaluateCondition(                                                      \
            actual, expected, #actual, #expected, {.filename = file, .line_number = line}, fatal,  \
            LIB_ZXTEST_DESC_PROVIDER(desc, __VA_ARGS__), buffer_compare,                           \
            LIB_ZXTEST_DEFAULT_PRINTER, LIB_ZXTEST_DEFAULT_PRINTER)) {                             \
      LIB_ZXTEST_RETURN_IF_FATAL(fatal);                                                           \
    }                                                                                              \
  } while (0)

#define LIB_ZXTEST_CHECK_VAR_BYTES(op, expected, actual, size, fatal, file, line, desc, ...)      \
  do {                                                                                            \
    LIB_ZXTEST_CHECK_RUNNING();                                                                   \
    size_t byte_count = size;                                                                     \
    if (!zxtest::internal::EvaluateCondition(                                                     \
            zxtest::internal::ToPointer(actual), zxtest::internal::ToPointer(expected), #actual,  \
            #expected, {.filename = file, .line_number = line}, fatal,                            \
            LIB_ZXTEST_DESC_PROVIDER(desc, __VA_ARGS__), LIB_ZXTEST_COMPARE_3_FN(op, byte_count), \
            LIB_ZXTEST_HEXDUMP_PRINTER(byte_count), LIB_ZXTEST_HEXDUMP_PRINTER(byte_count))) {    \
      LIB_ZXTEST_RETURN_IF_FATAL(fatal);                                                          \
    }                                                                                             \
  } while (0)

#define LIB_ZXTEST_FAIL_NO_RETURN(fatal, desc, ...)                               \
  do {                                                                            \
    LIB_ZXTEST_CHECK_RUNNING();                                                   \
    zxtest::Runner::GetInstance()->NotifyAssertion(                               \
        zxtest::Assertion(LIB_ZXTEST_DESC_PROVIDER(desc, __VA_ARGS__)(),          \
                          {.filename = __FILE__, .line_number = __LINE__}, fatal, \
                          zxtest::Runner::GetInstance()->GetScopedTraces()));     \
  } while (0)

#define LIB_ZXTEST_ASSERT_ERROR(has_errors, fatal, desc, ...) \
  do {                                                        \
    LIB_ZXTEST_CHECK_RUNNING();                               \
    if (has_errors) {                                         \
      LIB_ZXTEST_FAIL_NO_RETURN(fatal, desc, ##__VA_ARGS__);  \
      LIB_ZXTEST_RETURN_IF_FATAL(fatal);                      \
    }                                                         \
  } while (0)

#define FAIL(...)                                       \
  do {                                                  \
    LIB_ZXTEST_FAIL_NO_RETURN(true, "", ##__VA_ARGS__); \
    return;                                             \
  } while (0)

#define ZXTEST_SKIP(desc, ...)                                             \
  do {                                                                     \
    LIB_ZXTEST_CHECK_RUNNING();                                            \
    zxtest::Runner::GetInstance()->SkipCurrent(                            \
        zxtest::Message(LIB_ZXTEST_DESC_PROVIDER(desc, __VA_ARGS__)(),     \
                        {.filename = __FILE__, .line_number = __LINE__})); \
    return;                                                                \
  } while (0)

#endif  // ZXTEST_CPP_ASSERT_H_
