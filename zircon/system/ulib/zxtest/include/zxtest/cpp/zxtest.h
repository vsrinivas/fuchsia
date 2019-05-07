// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdlib>
#include <type_traits>

#include <fbl/string.h>
#include <fbl/string_printf.h>

#include <zxtest/base/assertion.h>
#include <zxtest/base/runner.h>
#include <zxtest/base/test.h>

#ifdef __Fuchsia__
#include <zircon/status.h>
#endif

// Macro definitions for usage within CPP.
#define RUN_ALL_TESTS(argc, argv) zxtest::RunAllTests(argc, argv)

#define _ZXTEST_TEST_REF(TestCase, Test) TestCase##_##Test##_Ref

#define _ZXTEST_DEFAULT_FIXTURE ::zxtest::Test

#define _ZXTEST_TEST_CLASS(TestCase, Test) TestCase##_##Test##_Class

#define _ZXTEST_TEST_CLASS_DECL(Fixture, TestClass)                                                \
    class TestClass : public Fixture {                                                             \
    public:                                                                                        \
        TestClass() = default;                                                                     \
        ~TestClass() final = default;                                                              \
                                                                                                   \
    private:                                                                                       \
        void TestBody() final;                                                                     \
    }

#define _ZXTEST_BEGIN_TEST_BODY(TestClass) void TestClass::TestBody()

#define _ZXTEST_REGISTER(TestCase, Test, Fixture)                                                  \
    _ZXTEST_TEST_CLASS_DECL(Fixture, _ZXTEST_TEST_CLASS(TestCase, Test));                          \
    [[maybe_unused]] static zxtest::TestRef _ZXTEST_TEST_REF(TestCase, Test) =                     \
        zxtest::Runner::GetInstance()->RegisterTest<Fixture, _ZXTEST_TEST_CLASS(TestCase, Test)>(  \
            #TestCase, #Test, __FILE__, __LINE__);                                                 \
    _ZXTEST_BEGIN_TEST_BODY(_ZXTEST_TEST_CLASS(TestCase, Test))

#define TEST(TestCase, Test) _ZXTEST_REGISTER(TestCase, Test, _ZXTEST_DEFAULT_FIXTURE)

#define TEST_F(TestCase, Test) _ZXTEST_REGISTER(TestCase, Test, TestCase)

// Common macro interface to generate assertion messages.
#define _ZXTEST_LOAD_PRINT_VAR(var, type, line)                                                    \
    auto str_buffer_##type##_##line = zxtest::PrintValue(var);

// Actually make it print hex value, used with ASSERT_BYTES_EQ, though for C++ that is the default
// behaviour.
#define _ZXTEST_LOAD_PRINT_HEX(var, var_size, type, line)                                          \
    auto str_buffer_##type##_##line = zxtest::internal::ToHex(var, var_size);

#define _ZXTEST_GET_PRINT_VAR(var, type, line) str_buffer_##type##_##line

#define _ZXTEST_ASSERT(desc, expected, expected_var, actual, actual_var, file, line, is_fatal)     \
    do {                                                                                           \
        zxtest::Assertion assertion(desc, expected, expected_var, actual, actual_var,              \
                                    {.filename = file, .line_number = line}, is_fatal);            \
        zxtest::Runner::GetInstance()->NotifyAssertion(assertion);                                 \
    } while (0)

#define _ZXTEST_NULLPTR nullptr

#define _ZXTEST_ABORT_IF_ERROR zxtest::Runner::GetInstance()->CurrentTestHasFatalFailures()

#define _ZXTEST_STRCMP(actual, expected) zxtest::StrCmp(actual, expected)

#define _ZXTEST_AUTO_VAR_TYPE(var) decltype(var)

#define _ZXTEST_TEST_HAS_ERRORS zxtest::Runner::GetInstance()->CurrentTestHasFailures()

#define _RETURN_IF_FATAL_true                                                                      \
    do {                                                                                           \
        if (_ZXTEST_ABORT_IF_ERROR) {                                                              \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define _RETURN_IF_FATAL_false                                                                     \
    do {                                                                                           \
    } while (0)

#define _RETURN_IF_FATAL(fatal) _RETURN_IF_FATAL_##fatal
namespace zxtest {
namespace internal {

// Returns true if the assertion condition is satisfied, and false otherwise. This should not
// be called directly by the user. This function allows performing the operations on values
// by reference, preventing any attempts to either copy or move a value.
template <typename Op, typename Desc, typename Printer, typename Actual, typename Expected>
bool EvalCondition(const Actual& actual, const Expected& expected, const char* actual_str,
                   const char* expected_str, const zxtest::SourceLocation& location, bool is_fatal,
                   bool force_hex, const Op& op, const Desc& desc, const Printer& printer) {
    // Happy case we do nothing.
    if (op(expected, actual)) {
        return true;
    }

    // Generate the string representation of the variables.
    fbl::String actual_value = printer(actual);
    fbl::String expected_value = printer(expected);
    Assertion assertion(desc(), expected_str, expected_value, actual_str, actual_value, location,
                        is_fatal);
    zxtest::Runner::GetInstance()->NotifyAssertion(assertion);
    return false;
}

// Promote integers to a common type when possible. This allows safely comparing different
// integer types and sizes, as long as a bigger int exists.
template <typename Actual, typename Expected, typename Compare>
bool CompareHelper(const Actual& actual, const Expected& expected, const Compare& comp) {
    if constexpr (std::is_integral<Actual>::value && std::is_integral<Expected>::value) {
        return comp(static_cast<typename std::common_type<Actual, Expected>::type>(actual),
                    static_cast<typename std::common_type<Actual, Expected>::type>(expected));
    } else {
        return comp(actual, expected);
    }
}

} // namespace internal
} // namespace zxtest

// Basic assert macro implementation.
#define _EQ(actual, expected)                                                                      \
    zxtest::internal::CompareHelper(actual, expected,                                              \
                                    [](const auto& a, const auto& b) { return a == b; })
#define _NE(actual, expected) !_EQ(actual, expected)
#define _BOOL(actual, expected) (static_cast<bool>(actual) == static_cast<bool>(expected))
#define _LT(actual, expected)                                                                      \
    zxtest::internal::CompareHelper(actual, expected,                                              \
                                    [](const auto& a, const auto& b) { return a < b; })
#define _LE(actual, expected)                                                                      \
    zxtest::internal::CompareHelper(actual, expected,                                              \
                                    [](const auto& a, const auto& b) { return a <= b; })
#define _GT(actual, expected)                                                                      \
    zxtest::internal::CompareHelper(actual, expected,                                              \
                                    [](const auto& a, const auto& b) { return a > b; })
#define _GE(actual, expected)                                                                      \
    zxtest::internal::CompareHelper(actual, expected,                                              \
                                    [](const auto& a, const auto& b) { return a >= b; })

#define _STREQ(actual, expected) (strcmp(actual, expected) == 0)
#define _STRNE(actual, expected) !_STREQ(actual, expected)
#define _BYTEEQ(actual, expected, size) memcmp(actual, expected, size) == 0
#define _BYTENE(actual, expected, size) memcmp(actual, expected, size) != 0

#define _GEN_ASSERT_DESC(out_desc, desc, ...)                                                      \
    fbl::String out_desc;                                                                          \
    do {                                                                                           \
        auto format_msg = fbl::StringPrintf(" " __VA_ARGS__);                                      \
        out_desc = fbl::String::Concat({fbl::String(desc), format_msg});                           \
    } while (0)

#define _ASSERT_VAR(op, expected, actual, fatal, file, line, desc, ...)                            \
    do {                                                                                           \
        auto buffer_compare = [&](const auto& expected_, const auto& actual_) {                    \
            return op(actual_, expected_);                                                         \
        };                                                                                         \
        auto desc_gen = [&]() -> fbl::String {                                                     \
            _GEN_ASSERT_DESC(out_desc, desc, __VA_ARGS__);                                         \
            return out_desc;                                                                       \
        };                                                                                         \
        auto print = [](const auto& val) { return zxtest::PrintValue(val); };                      \
        if (!zxtest::internal::EvalCondition(actual, expected, #actual, #expected,                 \
                                             {.filename = file, .line_number = line}, fatal,       \
                                             false, buffer_compare, desc_gen, print)) {            \
            _RETURN_IF_FATAL(fatal);                                                               \
        }                                                                                          \
    } while (0)

#ifdef __Fuchsia__
#define _ASSERT_VAR_STATUS(op, expected, actual, fatal, file, line, desc, ...)                     \
    do {                                                                                           \
        auto buffer_compare = [&](const auto& expected_, const auto& actual_) {                    \
            return op(actual_, expected_);                                                         \
        };                                                                                         \
        auto desc_gen = [&]() -> fbl::String {                                                     \
            _GEN_ASSERT_DESC(out_desc, desc, __VA_ARGS__);                                         \
            return out_desc;                                                                       \
        };                                                                                         \
        auto print = [](const auto& val) { return zx_status_get_string(val); };                    \
        if (!zxtest::internal::EvalCondition(actual, expected, #actual, #expected,                 \
                                             {.filename = file, .line_number = line}, fatal,       \
                                             false, buffer_compare, desc_gen, print)) {            \
            _RETURN_IF_FATAL(fatal);                                                               \
        }                                                                                          \
    } while (0)
#else
#define _ASSERT_VAR_STATUS(...) _ASSERT_VAR(__VA_ARGS__)
#endif

#define _ASSERT_VAR_COERCE(op, expected, actual, type, fatal, file, line, desc, ...)               \
    do {                                                                                           \
        auto buffer_compare = [&](const auto& expected_, const auto& actual_) {                    \
            return op(static_cast<type>(actual_), static_cast<type>(expected_));                   \
        };                                                                                         \
        auto desc_gen = [&]() -> fbl::String {                                                     \
            _GEN_ASSERT_DESC(out_desc, desc, __VA_ARGS__);                                         \
            return out_desc;                                                                       \
        };                                                                                         \
        auto print = [](const auto& val) { return zxtest::PrintValue(val); };                      \
        if (!zxtest::internal::EvalCondition(actual, expected, #actual, #expected,                 \
                                             {.filename = file, .line_number = line}, fatal,       \
                                             false, buffer_compare, desc_gen, print)) {            \
            _RETURN_IF_FATAL(fatal);                                                               \
        }                                                                                          \
    } while (0)

#define _ASSERT_VAR_BYTES(op, expected, actual, size, fatal, file, line, desc, ...)                \
    do {                                                                                           \
        size_t byte_count = size;                                                                  \
        auto buffer_compare = [byte_count](const auto& expected_, const auto& actual_) {           \
            return op(static_cast<const void*>(actual_), static_cast<const void*>(expected_),      \
                      byte_count);                                                                 \
        };                                                                                         \
        auto desc_gen = [&]() -> fbl::String {                                                     \
            _GEN_ASSERT_DESC(out_desc, desc, __VA_ARGS__);                                         \
            return out_desc;                                                                       \
        };                                                                                         \
        auto print = [byte_count](const auto& val) {                                               \
            return zxtest::internal::ToHex(static_cast<const void*>(val), byte_count);             \
        };                                                                                         \
        if (!zxtest::internal::EvalCondition(actual, expected, #actual, #expected,                 \
                                             {.filename = file, .line_number = line}, fatal,       \
                                             false, buffer_compare, desc_gen, print)) {            \
            _RETURN_IF_FATAL(fatal);                                                               \
        }                                                                                          \
    } while (0)

#define _ZXTEST_FAIL_NO_RETURN(fatal, desc, ...)                                                   \
    do {                                                                                           \
        _GEN_ASSERT_DESC(out_desc, desc, ##__VA_ARGS__);                                           \
        zxtest::Runner::GetInstance()->NotifyAssertion(                                            \
            zxtest::Assertion(out_desc, {.filename = __FILE__, .line_number = __LINE__}, fatal));  \
    } while (0)

#define _ZXTEST_ASSERT_ERROR(has_errors, fatal, desc, ...)                                         \
    do {                                                                                           \
        if (has_errors) {                                                                          \
            _ZXTEST_FAIL_NO_RETURN(fatal, desc, ##__VA_ARGS__);                                    \
            _RETURN_IF_FATAL(fatal);                                                               \
        }                                                                                          \
    } while (0)
