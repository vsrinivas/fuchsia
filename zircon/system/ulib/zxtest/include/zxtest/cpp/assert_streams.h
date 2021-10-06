// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_CPP_ASSERT_STREAMS_H_
#define ZXTEST_CPP_ASSERT_STREAMS_H_

#include <zxtest/cpp/internal.h>

#include "streams_helper.h"

// Basic assert macro implementation with stream support.
#define _CHECK_VAR(op, expected, actual, fatal, file, line, desc, ...)                          \
  _ZXTEST_CHECK_RUNNING();                                                                      \
  if (StreamableAssertion assertion = StreamableAssertion(                                      \
          actual, expected, #actual, #expected, {.filename = file, .line_number = line}, fatal, \
          _COMPARE_FN(op), _DEFAULT_PRINTER, _DEFAULT_PRINTER);                                 \
      assertion.IsTriggered())                                                                  \
  _RETURN_TAG(fatal) assertion << desc << " "

#define _CHECK_VAR_STATUS(op, expected, actual, fatal, file, line, desc, ...)                  \
  _ZXTEST_CHECK_RUNNING();                                                                     \
  if (StreamableAssertion assertion =                                                          \
          StreamableAssertion(GetStatus(actual), GetStatus(expected), #actual, #expected,      \
                              {.filename = file, .line_number = line}, fatal, _COMPARE_FN(op), \
                              _STATUS_PRINTER, _STATUS_PRINTER);                               \
      assertion.IsTriggered())                                                                 \
  _RETURN_TAG(fatal) assertion << desc << " "

#define _CHECK_VAR_COERCE(op, expected, actual, coerce_type, fatal, file, line, desc, ...)      \
  _ZXTEST_CHECK_RUNNING();                                                                      \
  if (StreamableAssertion assertion = StreamableAssertion(                                      \
          actual, expected, #actual, #expected, {.filename = file, .line_number = line}, fatal, \
          [&](const auto& expected_, const auto& actual_) {                                     \
            using DecayType = typename std::decay<coerce_type>::type;                           \
            return op(static_cast<const DecayType&>(actual_),                                   \
                      static_cast<const DecayType&>(expected_));                                \
          },                                                                                    \
          _DEFAULT_PRINTER, _DEFAULT_PRINTER);                                                  \
      assertion.IsTriggered())                                                                  \
  _RETURN_TAG(fatal) assertion << desc << " "

#define _CHECK_VAR_BYTES(op, expected, actual, size, fatal, file, line, desc, ...)               \
  _ZXTEST_CHECK_RUNNING();                                                                       \
  if (size_t byte_count = size; true)                                                            \
    if (StreamableAssertion assertion = StreamableAssertion(                                     \
            zxtest::internal::ToPointer(actual), zxtest::internal::ToPointer(expected), #actual, \
            #expected, {.filename = file, .line_number = line}, fatal,                           \
            _COMPARE_3_FN(op, byte_count), _HEXDUMP_PRINTER(byte_count),                         \
            _HEXDUMP_PRINTER(byte_count));                                                       \
        assertion.IsTriggered())                                                                 \
  _RETURN_TAG(fatal) assertion << desc << " "

#define _ZXTEST_FAIL_NO_RETURN(fatal, desc, ...) \
  StreamableFail({.filename = __FILE__, .line_number = __LINE__}, fatal) << desc << " "

#define _ZXTEST_ASSERT_ERROR(has_errors, fatal, desc, ...) \
  _ZXTEST_CHECK_RUNNING();                                 \
  if (has_errors)                                          \
  _RETURN_TAG(fatal) _ZXTEST_FAIL_NO_RETURN(fatal, desc, ##__VA_ARGS__)

#define FAIL(...)          \
  _ZXTEST_CHECK_RUNNING(); \
  return Tag() & _ZXTEST_FAIL_NO_RETURN(true, "", ##__VA_ARGS__)

#define ZXTEST_SKIP(...)   \
  _ZXTEST_CHECK_RUNNING(); \
  return Tag() & StreamableSkip({.filename = __FILE__, .line_number = __LINE__})

#endif  // ZXTEST_CPP_ASSERT_STREAMS_H_
