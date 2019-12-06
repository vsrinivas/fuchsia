// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_CPP_ZXTEST_H_
#define ZXTEST_CPP_ZXTEST_H_

#include <cstdlib>
#include <type_traits>

#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <zxtest/base/assertion.h>
#include <zxtest/base/runner.h>
#include <zxtest/base/test.h>
#include <zxtest/cpp/internal.h>

#ifdef __Fuchsia__
#include <zircon/status.h>
#include <zxtest/base/death-statement.h>
#endif

// Macro definitions for usage within CPP.
#define RUN_ALL_TESTS(argc, argv) zxtest::RunAllTests(argc, argv)

#define _ZXTEST_TEST_REF(TestCase, Test) TestCase##_##Test##_Ref

#define _ZXTEST_DEFAULT_FIXTURE ::zxtest::Test

#define _ZXTEST_TEST_CLASS(TestCase, Test) TestCase##_##Test##_Class

#define _ZXTEST_TEST_CLASS_DECL(Fixture, TestClass) \
  class TestClass final : public Fixture {                \
   public:                                          \
    TestClass() = default;                          \
    ~TestClass() final = default;                   \
                                                    \
   private:                                         \
    void TestBody() final;                          \
  }

#define _ZXTEST_BEGIN_TEST_BODY(TestClass) void TestClass::TestBody()

#define _ZXTEST_REGISTER(TestCase, Test, Fixture)                                               \
  _ZXTEST_TEST_CLASS_DECL(Fixture, _ZXTEST_TEST_CLASS(TestCase, Test));                         \
  [[maybe_unused]] static zxtest::TestRef _ZXTEST_TEST_REF(TestCase, Test) =                    \
      zxtest::Runner::GetInstance()->RegisterTest<Fixture, _ZXTEST_TEST_CLASS(TestCase, Test)>( \
          #TestCase, #Test, __FILE__, __LINE__);                                                \
  _ZXTEST_BEGIN_TEST_BODY(_ZXTEST_TEST_CLASS(TestCase, Test))

#define TEST(TestCase, Test) _ZXTEST_REGISTER(TestCase, Test, _ZXTEST_DEFAULT_FIXTURE)

#define TEST_F(TestCase, Test) _ZXTEST_REGISTER(TestCase, Test, TestCase)

#define _ZXTEST_NULLPTR nullptr

#define _ZXTEST_ABORT_IF_ERROR zxtest::Runner::GetInstance()->CurrentTestHasFatalFailures()

#define _ZXTEST_STRCMP(actual, expected) zxtest::StrCmp(actual, expected)

#define _ZXTEST_AUTO_VAR_TYPE(var) decltype(var)

#define _ZXTEST_TEST_HAS_ERRORS zxtest::Runner::GetInstance()->CurrentTestHasFailures()

// Pre-processor magic to allow EXPECT_ macros not enforce a return type on helper functions.
#define _RETURN_IF_FATAL_true     \
  do {                            \
    if (_ZXTEST_ABORT_IF_ERROR) { \
      unittest_returns_early();   \
      return;                     \
    }                             \
  } while (0)

#define _RETURN_IF_FATAL_false \
  do {                         \
  } while (0)

#define _RETURN_IF_FATAL(fatal) _RETURN_IF_FATAL_##fatal

// Definition of operations used to evaluate assertion conditions.
#define _EQ(actual, expected) \
  zxtest::internal::Compare(actual, expected, [](const auto& a, const auto& b) { return a == b; })
#define _NE(actual, expected) !_EQ(actual, expected)
#define _BOOL(actual, expected) (static_cast<bool>(actual) == static_cast<bool>(expected))
#define _LT(actual, expected) \
  zxtest::internal::Compare(actual, expected, [](const auto& a, const auto& b) { return a < b; })
#define _LE(actual, expected) \
  zxtest::internal::Compare(actual, expected, [](const auto& a, const auto& b) { return a <= b; })
#define _GT(actual, expected) \
  zxtest::internal::Compare(actual, expected, [](const auto& a, const auto& b) { return a > b; })
#define _GE(actual, expected) \
  zxtest::internal::Compare(actual, expected, [](const auto& a, const auto& b) { return a >= b; })
#define _STREQ(actual, expected) zxtest::StrCmp(actual, expected)
#define _STRNE(actual, expected) !_STREQ(actual, expected)
#define _BYTEEQ(actual, expected, size) \
  (memcmp(static_cast<const void*>(actual), static_cast<const void*>(expected), size) == 0)
#define _BYTENE(actual, expected, size) !(_BYTEEQ(actual, expected, size))

// Functions used as arguments for EvaluateCondition.
#define _DESC_PROVIDER(desc, ...)                                    \
  [&]() -> fbl::String {                                             \
    fbl::String out_desc;                                            \
    auto format_msg = fbl::StringPrintf(" " __VA_ARGS__);            \
    out_desc = fbl::String::Concat({fbl::String(desc), format_msg}); \
    return out_desc;                                                 \
  }

#define _COMPARE_FN(op) \
  [](const auto& expected_, const auto& actual_) { return op(expected_, actual_); }

#define _COMPARE_3_FN(op, third_param)                        \
  [third_param](const auto& expected_, const auto& actual_) { \
    return op(expected_, actual_, third_param);               \
  }

// Printers for converting values into readable strings.
#define _DEFAULT_PRINTER [](const auto& val) { return zxtest::PrintValue(val); }

#ifdef __Fuchsia__
#define _STATUS_PRINTER [](zx_status_t status) { return zxtest::PrintStatus(status); }
#else
#define _STATUS_PRINTER _DEFAULT_PRINTER
#endif

#define _HEXDUMP_PRINTER(size)                                                 \
  [size](const auto& val) {                                                    \
    return zxtest::internal::ToHex(static_cast<const void*>(val), byte_count); \
  }

// Basic assert macro implementation.
#define _ASSERT_VAR(op, expected, actual, fatal, file, line, desc, ...)                          \
  do {                                                                                           \
    if (!zxtest::internal::EvaluateCondition(actual, expected, #actual, #expected,               \
                                             {.filename = file, .line_number = line}, fatal,     \
                                             _DESC_PROVIDER(desc, __VA_ARGS__), _COMPARE_FN(op), \
                                             _DEFAULT_PRINTER, _DEFAULT_PRINTER)) {              \
      _RETURN_IF_FATAL(fatal);                                                                   \
    }                                                                                            \
  } while (0)

#define _ASSERT_VAR_STATUS(op, expected, actual, fatal, file, line, desc, ...)                   \
  do {                                                                                           \
    if (!zxtest::internal::EvaluateCondition(actual, expected, #actual, #expected,               \
                                             {.filename = file, .line_number = line}, fatal,     \
                                             _DESC_PROVIDER(desc, __VA_ARGS__), _COMPARE_FN(op), \
                                             _STATUS_PRINTER, _STATUS_PRINTER)) {                \
      _RETURN_IF_FATAL(fatal);                                                                   \
    }                                                                                            \
  } while (0)

#define _ASSERT_VAR_COERCE(op, expected, actual, coerce_type, fatal, file, line, desc, ...)     \
  do {                                                                                          \
    auto buffer_compare = [&](const auto& expected_, const auto& actual_) {                     \
      using DecayType = typename std::decay<coerce_type>::type;                                 \
      return op(static_cast<const DecayType&>(actual_),                                         \
                static_cast<const DecayType&>(expected_));                                      \
    };                                                                                          \
    if (!zxtest::internal::EvaluateCondition(actual, expected, #actual, #expected,              \
                                             {.filename = file, .line_number = line}, fatal,    \
                                             _DESC_PROVIDER(desc, __VA_ARGS__), buffer_compare, \
                                             _DEFAULT_PRINTER, _DEFAULT_PRINTER)) {             \
      _RETURN_IF_FATAL(fatal);                                                                  \
    }                                                                                           \
  } while (0)

#define _ASSERT_VAR_BYTES(op, expected, actual, size, fatal, file, line, desc, ...)              \
  do {                                                                                           \
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
    zxtest::Runner::GetInstance()->NotifyAssertion(                                 \
        zxtest::Assertion(_DESC_PROVIDER(desc, __VA_ARGS__)(),                      \
                          {.filename = __FILE__, .line_number = __LINE__}, fatal)); \
  } while (0)

#define _ZXTEST_ASSERT_ERROR(has_errors, fatal, desc, ...) \
  do {                                                     \
    if (has_errors) {                                      \
      _ZXTEST_FAIL_NO_RETURN(fatal, desc, ##__VA_ARGS__);  \
      _RETURN_IF_FATAL(fatal);                             \
    }                                                      \
  } while (0)

#ifdef __Fuchsia__
#define _ZXTEST_DEATH_STATUS_COMPLETE zxtest::internal::DeathStatement::State::kSuccess
#define _ZXTEST_DEATH_STATUS_EXCEPTION zxtest::internal::DeathStatement::State::kException
#define _ZXTEST_DEATH_STATEMENT(statement, expected_result, desc, ...)                     \
  do {                                                                                     \
    zxtest::internal::DeathStatement death_statement(statement);                           \
    death_statement.Execute();                                                             \
    if (death_statement.state() != expected_result) {                                      \
      if (death_statement.state() == zxtest::internal::DeathStatement::State::kBadState) { \
        zxtest::Runner::GetInstance()->NotifyFatalError();                                 \
      }                                                                                    \
      if (!death_statement.error_message().empty()) {                                      \
        _ZXTEST_ASSERT_ERROR(true, true, death_statement.error_message().data());          \
      } else {                                                                             \
        _ZXTEST_ASSERT_ERROR(true, true, desc, ##__VA_ARGS__);                             \
      }                                                                                    \
    }                                                                                      \
  } while (0)
#endif

#endif  // ZXTEST_CPP_ZXTEST_H_
