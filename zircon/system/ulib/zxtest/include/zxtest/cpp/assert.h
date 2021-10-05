// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_CPP_ASSERT_H_
#define ZXTEST_CPP_ASSERT_H_

#include <zxtest/cpp/internal.h>

// Basic assert macro implementation.
#define _CHECK_VAR(op, expected, actual, fatal, file, line, desc, ...)                           \
  do {                                                                                           \
    _ZXTEST_CHECK_RUNNING();                                                                     \
    if (!zxtest::internal::EvaluateCondition(actual, expected, #actual, #expected,               \
                                             {.filename = file, .line_number = line}, fatal,     \
                                             _DESC_PROVIDER(desc, __VA_ARGS__), _COMPARE_FN(op), \
                                             _DEFAULT_PRINTER, _DEFAULT_PRINTER)) {              \
      _RETURN_IF_FATAL(fatal);                                                                   \
    }                                                                                            \
  } while (0)

#define _CHECK_VAR_STATUS(op, expected, actual, fatal, file, line, desc, ...)                     \
  do {                                                                                            \
    _ZXTEST_CHECK_RUNNING();                                                                      \
    if (!zxtest::internal::EvaluateStatusCondition(                                               \
            actual, expected, #actual, #expected, {.filename = file, .line_number = line}, fatal, \
            _DESC_PROVIDER(desc, __VA_ARGS__), _COMPARE_FN(op), _STATUS_PRINTER,                  \
            _STATUS_PRINTER)) {                                                                   \
      _RETURN_IF_FATAL(fatal);                                                                    \
    }                                                                                             \
  } while (0)

#define _CHECK_VAR_COERCE(op, expected, actual, coerce_type, fatal, file, line, desc, ...)         \
  do {                                                                                             \
    _ZXTEST_CHECK_RUNNING();                                                                       \
    auto buffer_compare = [&](const auto& expected_, const auto& actual_) {                        \
      using DecayType = typename std::decay<coerce_type>::type;                                    \
      return op(static_cast<const DecayType&>(actual_), static_cast<const DecayType&>(expected_)); \
    };                                                                                             \
    if (!zxtest::internal::EvaluateCondition(actual, expected, #actual, #expected,                 \
                                             {.filename = file, .line_number = line}, fatal,       \
                                             _DESC_PROVIDER(desc, __VA_ARGS__), buffer_compare,    \
                                             _DEFAULT_PRINTER, _DEFAULT_PRINTER)) {                \
      _RETURN_IF_FATAL(fatal);                                                                     \
    }                                                                                              \
  } while (0)

#define _CHECK_VAR_BYTES(op, expected, actual, size, fatal, file, line, desc, ...)               \
  do {                                                                                           \
    _ZXTEST_CHECK_RUNNING();                                                                     \
    size_t byte_count = size;                                                                    \
    if (!zxtest::internal::EvaluateCondition(                                                    \
            zxtest::internal::ToPointer(actual), zxtest::internal::ToPointer(expected), #actual, \
            #expected, {.filename = file, .line_number = line}, fatal,                           \
            _DESC_PROVIDER(desc, __VA_ARGS__), _COMPARE_3_FN(op, byte_count),                    \
            _HEXDUMP_PRINTER(byte_count), _HEXDUMP_PRINTER(byte_count))) {                       \
      _RETURN_IF_FATAL(fatal);                                                                   \
    }                                                                                            \
  } while (0)

#define _ZXTEST_FAIL_NO_RETURN(fatal, desc, ...)                                    \
  do {                                                                              \
    _ZXTEST_CHECK_RUNNING();                                                        \
    zxtest::Runner::GetInstance()->NotifyAssertion(                                 \
        zxtest::Assertion(_DESC_PROVIDER(desc, __VA_ARGS__)(),                      \
                          {.filename = __FILE__, .line_number = __LINE__}, fatal)); \
  } while (0)

#define _ZXTEST_ASSERT_ERROR(has_errors, fatal, desc, ...) \
  do {                                                     \
    _ZXTEST_CHECK_RUNNING();                               \
    if (has_errors) {                                      \
      _ZXTEST_FAIL_NO_RETURN(fatal, desc, ##__VA_ARGS__);  \
      _RETURN_IF_FATAL(fatal);                             \
    }                                                      \
  } while (0)

#define FAIL(...)                                    \
  do {                                               \
    _ZXTEST_FAIL_NO_RETURN(true, "", ##__VA_ARGS__); \
    return;                                          \
  } while (0)

#define ZXTEST_SKIP(desc, ...)                                                                  \
  do {                                                                                          \
    _ZXTEST_CHECK_RUNNING();                                                                    \
    zxtest::Runner::GetInstance()->SkipCurrent(zxtest::Message(                                 \
        _DESC_PROVIDER(desc, __VA_ARGS__)(), {.filename = __FILE__, .line_number = __LINE__})); \
    return;                                                                                     \
  } while (0)

#endif  // ZXTEST_CPP_ASSERT_H_
