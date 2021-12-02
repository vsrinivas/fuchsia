// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_CPP_ZXTEST_H_
#define ZXTEST_CPP_ZXTEST_H_

#ifndef ZXTEST_INCLUDE_INTERNAL_HEADERS
#error This header is not intended for direct inclusion. Include zxtest/zxtest.h instead.
#endif

#include <cstdlib>
#include <type_traits>

#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <zxtest/base/assertion.h>
#include <zxtest/base/parameterized-value-impl.h>
#include <zxtest/base/runner.h>
#include <zxtest/base/test.h>
#include <zxtest/base/values.h>

#ifdef __Fuchsia__
#include <zircon/status.h>

#include <zxtest/base/death-statement.h>
#endif

#include "scoped_trace.h"

// Pre-processor magic to allow EXPECT_ macros not enforce a return type on helper functions.
#define LIB_ZXTEST_RETURN_IF_FATAL_true \
  do {                                  \
    unittest_fails();                   \
    if (LIB_ZXTEST_ABORT_IF_ERROR) {    \
      return;                           \
    }                                   \
  } while (0)

#define LIB_ZXTEST_RETURN_IF_FATAL_false \
  do {                                   \
    unittest_fails();                    \
  } while (0)

#define LIB_ZXTEST_RETURN_IF_FATAL(fatal) LIB_ZXTEST_RETURN_IF_FATAL_##fatal

#ifdef ZXTEST_USE_STREAMABLE_MACROS
#include <zxtest/cpp/assert_streams.h>
#else
#include <zxtest/cpp/assert.h>
#endif

// Macro definitions for usage within CPP.
#define LIB_ZXTEST_EXPAND_(arg) arg
#define LIB_ZXTEST_GET_FIRST_(first, ...) first
#define LIB_ZXTEST_GET_SECOND_(first, second, ...) second

#define RUN_ALL_TESTS(argc, argv) zxtest::RunAllTests(argc, argv)

#define LIB_ZXTEST_TEST_REF_N(TestCase, Test, Tag) TestCase##_##Test##_##Tag##_Ref
#define LIB_ZXTEST_TEST_REF(TestCase, Test) LIB_ZXTEST_TEST_REF_N(TestCase, Test, 0)

#define LIB_ZXTEST_CONCAT_TOKEN(foo, bar) LIB_ZXTEST_CONCAT_TOKEN_IMPL(foo, bar)
#define LIB_ZXTEST_CONCAT_TOKEN_IMPL(foo, bar) foo##bar

#define LIB_ZXTEST_DEFAULT_FIXTURE ::zxtest::Test
#define LIB_ZXTEST_PARAM_FIXTURE ::zxtest::TestWithParam

#define LIB_ZXTEST_TEST_CLASS(TestCase, Test) TestCase##_##Test##_Class

#define LIB_ZXTEST_TEST_CLASS_DECL(Fixture, TestClass) \
  class TestClass final : public Fixture {             \
   public:                                             \
    TestClass() = default;                             \
    ~TestClass() final = default;                      \
                                                       \
   private:                                            \
    void TestBody() final;                             \
  }

#define LIB_ZXTEST_BEGIN_TEST_BODY(TestClass) void TestClass::TestBody()

#define LIB_ZXTEST_REGISTER_FN(TestCase, Test) TestCase##_##Test##_register_fn

// Note: We intentionally wrap the assignment in a constructor function, to workaround the issue
// where in certain builds (both debug and production), the compiler would generated a global
// initiatialization function for the runtime, which would push a huge amount of memory into the
// stack. For 2048 tests, it pushed ~270 KB, which caused an overflow.
#define LIB_ZXTEST_REGISTER(TestCase, Test, Fixture)                                            \
  LIB_ZXTEST_TEST_CLASS_DECL(Fixture, LIB_ZXTEST_TEST_CLASS(TestCase, Test));                   \
  static zxtest::TestRef LIB_ZXTEST_TEST_REF(TestCase, Test) = {};                              \
  static void LIB_ZXTEST_REGISTER_FN(TestCase, Test)(void) __attribute__((constructor));        \
  void LIB_ZXTEST_REGISTER_FN(TestCase, Test)(void) {                                           \
    LIB_ZXTEST_TEST_REF(TestCase, Test) =                                                       \
        zxtest::Runner::GetInstance()                                                           \
            ->RegisterTest<Fixture, LIB_ZXTEST_TEST_CLASS(TestCase, Test)>(#TestCase, #Test,    \
                                                                           __FILE__, __LINE__); \
  }                                                                                             \
  LIB_ZXTEST_BEGIN_TEST_BODY(LIB_ZXTEST_TEST_CLASS(TestCase, Test))

#define LIB_ZXTEST_REGISTER_PARAMETERIZED(TestSuite, Test)                                \
  LIB_ZXTEST_TEST_CLASS_DECL(TestSuite, LIB_ZXTEST_TEST_CLASS(TestSuite, Test));          \
  static void LIB_ZXTEST_REGISTER_FN(TestSuite, Test)(void) __attribute__((constructor)); \
  void LIB_ZXTEST_REGISTER_FN(TestSuite, Test)(void) {                                    \
    zxtest::Runner::GetInstance()->AddParameterizedTest<TestSuite>(                       \
        std::make_unique<zxtest::internal::AddTestDelegateImpl<                           \
            TestSuite, TestSuite::ParamType, LIB_ZXTEST_TEST_CLASS(TestSuite, Test)>>(),  \
        #TestSuite, #Test, {.filename = __FILE__, .line_number = __LINE__});              \
  }                                                                                       \
  LIB_ZXTEST_BEGIN_TEST_BODY(LIB_ZXTEST_TEST_CLASS(TestSuite, Test))

#define TEST(TestCase, Test) LIB_ZXTEST_REGISTER(TestCase, Test, LIB_ZXTEST_DEFAULT_FIXTURE)

#define TEST_F(TestCase, Test) LIB_ZXTEST_REGISTER(TestCase, Test, TestCase)

#define TEST_P(TestSuite, Test) LIB_ZXTEST_REGISTER_PARAMETERIZED(TestSuite, Test)

#define LIB_ZXTEST_NULLPTR nullptr

#define LIB_ZXTEST_ABORT_IF_ERROR zxtest::Runner::GetInstance()->CurrentTestHasFatalFailures()

#define LIB_ZXTEST_STRCMP(actual, expected) zxtest::StrCmp(actual, expected)

#define LIB_ZXTEST_AUTO_VAR_TYPE(var) decltype(var)

#define LIB_ZXTEST_TEST_HAS_ERRORS zxtest::Runner::GetInstance()->CurrentTestHasFailures()

#define LIB_ZXTEST_IS_SKIPPED zxtest::Runner::GetInstance()->IsSkipped()

#define LIB_ZXTEST_ADD_INSTANTIATION_DEFAULT_NAME(arg1) \
  [](const auto info) -> std::string { return std::to_string(info.index); }

#define LIB_ZXTEST_ADD_INSTANTIATION_CUSTOM_NAME(arg1, generator) generator

#define LIB_ZXTEST_GET_3RD_ARG(arg1, arg2, arg3, ...) arg3

#define LIB_ZXTEST_NAME_GENERATOR_CHOOSER(...)                                  \
  LIB_ZXTEST_GET_3RD_ARG(__VA_ARGS__, LIB_ZXTEST_ADD_INSTANTIATION_CUSTOM_NAME, \
                         LIB_ZXTEST_ADD_INSTANTIATION_DEFAULT_NAME)

#define LIB_ZXTEST_INSTANTIATION_NAME_FN(...) \
  LIB_ZXTEST_NAME_GENERATOR_CHOOSER(__VA_ARGS__)(__VA_ARGS__)

#define INSTANTIATE_TEST_SUITE_P(Prefix, TestSuite, ...)                                        \
  static void LIB_ZXTEST_REGISTER_FN(Prefix, TestSuite)(void) __attribute__((constructor));     \
  void LIB_ZXTEST_REGISTER_FN(Prefix, TestSuite)(void) {                                        \
    static zxtest::internal::ValueProvider<TestSuite::ParamType> provider(                      \
        LIB_ZXTEST_EXPAND_(LIB_ZXTEST_GET_FIRST_(__VA_ARGS__)));                                \
    zxtest::Runner::GetInstance()->AddInstantiation<TestSuite, TestSuite::ParamType>(           \
        std::make_unique<                                                                       \
            zxtest::internal::AddInstantiationDelegateImpl<TestSuite, TestSuite::ParamType>>(), \
        #Prefix, {.filename = __FILE__, .line_number = __LINE__}, provider,                     \
        LIB_ZXTEST_INSTANTIATION_NAME_FN(__VA_ARGS__));                                         \
  }                                                                                             \
  static int LIB_ZXTEST_TEST_REF(Prefix, TestSuite) __attribute__((unused)) = 0

// Definition of operations used to evaluate assertion conditions.
#define LIB_ZXTEST_EQ(actual, expected) \
  zxtest::internal::Compare(actual, expected, [](const auto& a, const auto& b) { return a == b; })
#define LIB_ZXTEST_NE(actual, expected) !LIB_ZXTEST_EQ(actual, expected)
#define LIB_ZXTEST_BOOL(actual, expected) (static_cast<bool>(actual) == static_cast<bool>(expected))
#define LIB_ZXTEST_LT(actual, expected) \
  zxtest::internal::Compare(actual, expected, [](const auto& a, const auto& b) { return a < b; })
#define LIB_ZXTEST_LE(actual, expected) \
  zxtest::internal::Compare(actual, expected, [](const auto& a, const auto& b) { return a <= b; })
#define LIB_ZXTEST_GT(actual, expected) \
  zxtest::internal::Compare(actual, expected, [](const auto& a, const auto& b) { return a > b; })
#define LIB_ZXTEST_GE(actual, expected) \
  zxtest::internal::Compare(actual, expected, [](const auto& a, const auto& b) { return a >= b; })
#define LIB_ZXTEST_STREQ(actual, expected) zxtest::StrCmp(actual, expected)
#define LIB_ZXTEST_STRNE(actual, expected) !LIB_ZXTEST_STREQ(actual, expected)
#define LIB_ZXTEST_SUBSTR(str, substr) zxtest::StrContain(str, substr)
#define LIB_ZXTEST_BYTEEQ(actual, expected, size) \
  (memcmp(static_cast<const void*>(actual), static_cast<const void*>(expected), size) == 0)
#define LIB_ZXTEST_BYTENE(actual, expected, size) !(LIB_ZXTEST_BYTEEQ(actual, expected, size))

// Functions used as arguments for EvaluateCondition.
#define LIB_ZXTEST_DESC_PROVIDER(desc, ...)                          \
  [&]() -> fbl::String {                                             \
    fbl::String out_desc;                                            \
    auto format_msg = fbl::StringPrintf(" " __VA_ARGS__);            \
    out_desc = fbl::String::Concat({fbl::String(desc), format_msg}); \
    return out_desc;                                                 \
  }

#define LIB_ZXTEST_COMPARE_FN(op) \
  [](const auto& expected_, const auto& actual_) { return op(expected_, actual_); }

#define LIB_ZXTEST_COMPARE_3_FN(op, third_param)              \
  [third_param](const auto& expected_, const auto& actual_) { \
    return op(expected_, actual_, third_param);               \
  }

// Printers for converting values into readable strings.
#define LIB_ZXTEST_DEFAULT_PRINTER [](const auto& val) { return zxtest::PrintValue(val); }

#ifdef __Fuchsia__
#define LIB_ZXTEST_STATUS_PRINTER [](zx_status_t status) { return zxtest::PrintStatus(status); }
#else
#define LIB_ZXTEST_STATUS_PRINTER LIB_ZXTEST_DEFAULT_PRINTER
#endif

#define LIB_ZXTEST_HEXDUMP_PRINTER(size)                                       \
  [size](const auto& val) {                                                    \
    return zxtest::internal::ToHex(static_cast<const void*>(val), byte_count); \
  }

#ifdef __Fuchsia__
#define LIB_ZXTEST_DEATH_STATUS_COMPLETE zxtest::internal::DeathStatement::State::kSuccess
#define LIB_ZXTEST_DEATH_STATUS_EXCEPTION zxtest::internal::DeathStatement::State::kException
#define LIB_ZXTEST_DEATH_STATEMENT(statement, expected_result, desc, ...)                  \
  do {                                                                                     \
    LIB_ZXTEST_CHECK_RUNNING();                                                            \
    zxtest::internal::DeathStatement death_statement(statement);                           \
    death_statement.Execute();                                                             \
    if (death_statement.state() != expected_result) {                                      \
      if (death_statement.state() == zxtest::internal::DeathStatement::State::kBadState) { \
        zxtest::Runner::GetInstance()->NotifyFatalError();                                 \
      }                                                                                    \
      if (!death_statement.error_message().empty()) {                                      \
        LIB_ZXTEST_ASSERT_ERROR(true, true, death_statement.error_message().data());       \
      } else {                                                                             \
        LIB_ZXTEST_ASSERT_ERROR(true, true, desc, ##__VA_ARGS__);                          \
      }                                                                                    \
    }                                                                                      \
  } while (0)
#endif  // __Fuchsia__

#define SCOPED_TRACE(message)                                           \
  zxtest::ScopedTrace LIB_ZXTEST_CONCAT_TOKEN(zxtest_trace_, __LINE__)( \
      {.filename = __FILE__, .line_number = __LINE__}, message)

#endif  // ZXTEST_CPP_ZXTEST_H_
