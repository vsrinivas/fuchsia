// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_CPP_ASSERT_STREAMS_H_
#define ZXTEST_CPP_ASSERT_STREAMS_H_

#include <zxtest/cpp/internal.h>

#include "streams_helper.h"

// Basic assert macro implementation with stream support.
#define LIB_ZXTEST_CHECK_VAR(op, expected, actual, fatal, file, line, desc, ...)                  \
  if ([]() { LIB_ZXTEST_CHECK_RUNNING(); }(); true)                                               \
    if (StreamableAssertion assertion = StreamableAssertion(                                      \
            actual, expected, #actual, #expected, {.filename = file, .line_number = line}, fatal, \
            LIB_ZXTEST_COMPARE_FN(op), LIB_ZXTEST_DEFAULT_PRINTER, LIB_ZXTEST_DEFAULT_PRINTER,    \
            zxtest::Runner::GetInstance()->GetScopedTraces());                                    \
        assertion.IsTriggered())                                                                  \
  LIB_ZXTEST_RETURN_TAG(fatal) assertion << desc << " "

#define LIB_ZXTEST_CHECK_VAR_STATUS(op, expected, actual, fatal, file, line, desc, ...) \
  if ([]() { LIB_ZXTEST_CHECK_RUNNING(); }(); true)                                     \
    if (StreamableAssertion assertion = StreamableAssertion(                            \
            GetStatus(actual), GetStatus(expected), #actual, #expected,                 \
            {.filename = file, .line_number = line}, fatal, LIB_ZXTEST_COMPARE_FN(op),  \
            LIB_ZXTEST_STATUS_PRINTER, LIB_ZXTEST_STATUS_PRINTER,                       \
            zxtest::Runner::GetInstance()->GetScopedTraces());                          \
        assertion.IsTriggered())                                                        \
  LIB_ZXTEST_RETURN_TAG(fatal) assertion << desc << " "

#define LIB_ZXTEST_CHECK_VAR_COERCE(op, expected, actual, coerce_type, fatal, file, line, desc,   \
                                    ...)                                                          \
  if ([]() { LIB_ZXTEST_CHECK_RUNNING(); }(); true)                                               \
    if (StreamableAssertion assertion = StreamableAssertion(                                      \
            actual, expected, #actual, #expected, {.filename = file, .line_number = line}, fatal, \
            [&](const auto& expected_, const auto& actual_) {                                     \
              using DecayType = typename std::decay<coerce_type>::type;                           \
              return op(static_cast<const DecayType&>(actual_),                                   \
                        static_cast<const DecayType&>(expected_));                                \
            },                                                                                    \
            LIB_ZXTEST_DEFAULT_PRINTER, LIB_ZXTEST_DEFAULT_PRINTER,                               \
            zxtest::Runner::GetInstance()->GetScopedTraces());                                    \
        assertion.IsTriggered())                                                                  \
  LIB_ZXTEST_RETURN_TAG(fatal) assertion << desc << " "

#define LIB_ZXTEST_CHECK_VAR_BYTES(op, expected, actual, size, fatal, file, line, desc, ...)       \
  if ([]() { LIB_ZXTEST_CHECK_RUNNING(); }(); true)                                                \
    if (size_t byte_count = size; true)                                                            \
      if (StreamableAssertion assertion = StreamableAssertion(                                     \
              zxtest::internal::ToPointer(actual), zxtest::internal::ToPointer(expected), #actual, \
              #expected, {.filename = file, .line_number = line}, fatal,                           \
              LIB_ZXTEST_COMPARE_3_FN(op, byte_count), LIB_ZXTEST_HEXDUMP_PRINTER(byte_count),     \
              LIB_ZXTEST_HEXDUMP_PRINTER(byte_count),                                              \
              zxtest::Runner::GetInstance()->GetScopedTraces());                                   \
          assertion.IsTriggered())                                                                 \
  LIB_ZXTEST_RETURN_TAG(fatal) assertion << desc << " "

#define LIB_ZXTEST_FAIL_NO_RETURN(fatal, desc, ...)                      \
  StreamableFail({.filename = __FILE__, .line_number = __LINE__}, fatal, \
                 zxtest::Runner::GetInstance()->GetScopedTraces())       \
      << desc << " "

#define LIB_ZXTEST_ASSERT_ERROR(has_errors, fatal, desc, ...) \
  if ([]() { LIB_ZXTEST_CHECK_RUNNING(); }(); has_errors)     \
  LIB_ZXTEST_RETURN_TAG(fatal) LIB_ZXTEST_FAIL_NO_RETURN(fatal, desc, ##__VA_ARGS__)

#define FAIL(...)                                   \
  if ([]() { LIB_ZXTEST_CHECK_RUNNING(); }(); true) \
  return Tag() & LIB_ZXTEST_FAIL_NO_RETURN(true, "", ##__VA_ARGS__)

#define ZXTEST_SKIP(...)                            \
  if ([]() { LIB_ZXTEST_CHECK_RUNNING(); }(); true) \
  return Tag() & StreamableSkip({.filename = __FILE__, .line_number = __LINE__})

#endif  // ZXTEST_CPP_ASSERT_STREAMS_H_
