// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

/*
 * Macros for writing unit tests.
 *
 * Sample usage:
 *
 * A test case runs a collection of tests like this, with
 * BEGIN_TEST_CASE and END_TEST_CASE and the beginning and end of the
 * function and RUN_TEST to call each individual test, as follows:
 *
 *  BEGIN_TEST_CASE(foo_tests);
 *
 *  RUN_TEST(test_foo);
 *  RUN_TEST(test_bar);
 *  RUN_TEST(test_baz);
 *
 *  END_TEST_CASE(foo_tests);
 *
 * This creates a static function foo_tests() and registers it with the
 * unit test framework.  foo_tests() can be executed either by a shell
 * command or by a call to run_all_tests(), which runs all registered
 * unit tests.
 *
 * A test looks like this, using the BEGIN_TEST and END_TEST macros at
 * the beginning and end of the test and the EXPECT_* macros to
 * validate test results, as shown:
 *
 * static bool test_foo(void)
 * {
 *      BEGIN_TEST;
 *
 *      ...declare variables and do stuff...
 *      int foo_value = foo_func();
 *      ...See if the stuff produced the correct value...
 *      EXPECT_EQ(1, foo_value, "foo_func failed");
 *      ... there are EXPECT_* macros for many conditions...
 *      EXPECT_TRUE(foo_condition(), "condition should be true");
 *      EXPECT_NEQ(ERR_TIMED_OUT, foo_event(), "event timed out");
 *
 *      END_TEST;
 * }
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <magenta/compiler.h>

#define PRINT_BUFFER_SIZE (512)

__BEGIN_CDECLS

extern int utest_verbosity_level;

/*
 * Type for unit test result Output
 */
typedef void (*test_output_func)(const char* line, int len, void* arg);

/*
 * Printf dedicated to the unittest library
 * the default output is the printf
 */
void unittest_printf_critical(const char* format, ...);

/*
 * Printf dedicated to the unittest library which output
 * depends on the verbosity level.
 */

#define unittest_printf(format, ...)                           \
    do {                                                       \
        if (utest_verbosity_level > 0)                         \
            unittest_printf_critical(format, ##__VA_ARGS__);   \
    } while (0)

/*
 * Function to set the callback for printing
 * the unit test output
 */
void unittest_set_output_function(test_output_func fun, void* arg);

/*
 * Function to set the verbosity level. This affects
 * the output of unittest_printf().
 */
int unittest_set_verbosity_level(int new_level);

/*
 * Macros to format the error string
 */
#define UNITTEST_TRACEF(str, x...)                                            \
    do {                                                                      \
        unittest_printf_critical(" [FAILED] \n        %s:%d:\n        " str,  \
                                 __PRETTY_FUNCTION__, __LINE__, ##x);         \
    } while (0)

/*
 * BEGIN_TEST_CASE and END_TEST_CASE define a function that calls
 * RUN_TEST.
 */
#define BEGIN_TEST_CASE(case_name)                                          \
    bool case_name(void) {                                                  \
        bool all_success = true;                                            \
        unittest_printf_critical("\nCASE %-50s [STARTED] \n", #case_name);

#define DEFINE_REGISTER_TEST_CASE(case_name)                            \
    __attribute__((constructor)) static void _register_##case_name(void) { \
        unittest_register_test_case(&_##case_name##_element);           \
    }

#define END_TEST_CASE(case_name)                                        \
    if (all_success) {                                                  \
        unittest_printf_critical("CASE %-50s [PASSED]\n", #case_name);  \
    } else {                                                            \
        unittest_printf_critical("CASE %-50s [FAILED]\n", #case_name);  \
    }                                                                   \
    return all_success;                                                 \
    }                                                                   \
    static struct test_case_element _##case_name##_element = {          \
        .next = NULL,                                                   \
        .failed_next = NULL,                                            \
        .name = #case_name,                                             \
        .test_case = case_name,                                         \
    };                                                                  \
    DEFINE_REGISTER_TEST_CASE(case_name);

#define RUN_NAMED_TEST(name, test)                                  \
    {                                                               \
        unittest_printf_critical("    %-51s [RUNNING]", name);      \
        struct test_info test_info;                                 \
        current_test_info = &test_info;                             \
        if (!test()) {                                              \
            all_success = false;                                    \
        } else {                                                    \
            unittest_printf_critical(" [PASSED] \n");               \
        }                                                           \
    }

#define RUN_TEST(test) RUN_NAMED_TEST(#test, test)

/*
 * BEGIN_TEST and END_TEST go in a function that is called by RUN_TEST
 * and that call the EXPECT_ macros.
 */
#define BEGIN_TEST current_test_info->all_ok = true
#define END_TEST return current_test_info->all_ok

/*
 * BEGIN_HELPER and END_HELPER go in helper programs.
 * For example, if a test needs to start a second helper program, and you want
 * to use the ASSERT_*,EXPECT_* macros in the helper program, then surround the
 * usage with these macros.
 */
#define BEGIN_HELPER \
    struct test_info _test_info; \
    current_test_info = &_test_info; \
    current_test_info->all_ok = true
#define END_HELPER return current_test_info->all_ok

#ifdef __cplusplus
#define AUTO_TYPE_VAR(type) auto&
#else
#define AUTO_TYPE_VAR(type) __typeof__(type)
#endif

#define RET_FALSE return false
#define DONOT_RET

#define UT_CMP(op, msg, lhs, rhs, lhs_str, rhs_str, ret)              \
    do {                                                              \
        const AUTO_TYPE_VAR(lhs) _lhs_val = (lhs);                    \
        const AUTO_TYPE_VAR(rhs) _rhs_val = (rhs);                    \
        if (!(_lhs_val op _rhs_val)) {                                \
            UNITTEST_TRACEF(                                          \
                "%s:\n"                                               \
                "        Comparison failed: %s %s %s is false\n"      \
                "        Specifically, %ld %s %ld is false\n",        \
                msg, lhs_str, #op, rhs_str, _lhs_val, #op, _rhs_val); \
            current_test_info->all_ok = false;                        \
            ret;                                                      \
        }                                                             \
    } while (0)

#define UT_TRUE(actual, msg, ret)                           \
    if (!(actual)) {                                        \
        UNITTEST_TRACEF("%s: %s is false\n", msg, #actual); \
        current_test_info->all_ok = false;                  \
        ret;                                                \
    }

#define UT_FALSE(actual, msg, ret)                         \
    if (actual) {                                          \
        UNITTEST_TRACEF("%s: %s is true\n", msg, #actual); \
        current_test_info->all_ok = false;                 \
        ret;                                               \
    }

#define UT_NULL(actual, msg, ret)                               \
    if (actual != NULL) {                                       \
        UNITTEST_TRACEF("%s: %s is non-null!\n", msg, #actual); \
        current_test_info->all_ok = false;                      \
        ret;                                                    \
    }

#define UT_NONNULL(actual, msg, ret)                        \
    if (actual == NULL) {                                   \
        UNITTEST_TRACEF("%s: %s is null!\n", msg, #actual); \
        current_test_info->all_ok = false;                  \
        ret;                                                \
    }

#define UT_BYTES_EQ(expected, actual, length, msg, ret)                   \
    if (!unittest_expect_bytes_eq((expected), (actual), (length), msg)) { \
        current_test_info->all_ok = false;                                \
        ret;                                                              \
    }

#define UT_BYTES_NE(bytes1, bytes2, length, msg, ret) \
    if (!memcmp(bytes1, bytes2, length)) {            \
        UNITTEST_TRACEF(                              \
            "%s and %s are the same; "                \
            "expected different\n",                   \
            #bytes1, #bytes2);                        \
        hexdump8(bytes1, length);                     \
        current_test_info->all_ok = false;            \
        ret;                                          \
    }

/* For comparing uint64_t, like hw_id_t. */
#define UT_EQ_LL(expected, actual, msg, ret)                                  \
    do {                                                                      \
        const AUTO_TYPE_VAR(expected) _e = (expected);                        \
        const AUTO_TYPE_VAR(actual) _a = (actual);                            \
        if (_e != _a) {                                                       \
            UNITTEST_TRACEF("%s: expected %llu, actual %llu\n", msg, _e, _a); \
            current_test_info->all_ok = false;                                \
            ret;                                                              \
        }                                                                     \
    } while (0)

#define EXPECT_CMP(op, msg, lhs, rhs, lhs_str, rhs_str) UT_CMP(op, msg, lhs, rhs, lhs_str, rhs_str, DONOT_RET)

/*
 * Use the EXPECT_* macros to check test results.
 */
#define EXPECT_EQ(lhs, rhs, msg) EXPECT_CMP(==, msg, lhs, rhs, #lhs, #rhs)
#define EXPECT_NEQ(lhs, rhs, msg) EXPECT_CMP(!=, msg, lhs, rhs, #lhs, #rhs)
#define EXPECT_LE(lhs, rhs, msg) EXPECT_CMP(<=, msg, lhs, rhs, #lhs, #rhs)
#define EXPECT_GE(lhs, rhs, msg) EXPECT_CMP(>=, msg, lhs, rhs, #lhs, #rhs)
#define EXPECT_LT(lhs, rhs, msg) EXPECT_CMP(<, msg, lhs, rhs, #lhs, #rhs)
#define EXPECT_GT(lhs, rhs, msg) EXPECT_CMP(>, msg, lhs, rhs, #lhs, #rhs)

#define EXPECT_TRUE(actual, msg) UT_TRUE(actual, msg, DONOT_RET)
#define EXPECT_FALSE(actual, msg) UT_FALSE(actual, msg, DONOT_RET)
#define EXPECT_NULL(actual, msg) UT_NULL(actual, msg, DONOT_RET)
#define EXPECT_NONNULL(actual, msg) UT_NONNULL(actual, msg, DONOT_RET)
#define EXPECT_BYTES_EQ(expected, actual, length, msg) UT_BYTES_EQ(expected, actual, length, msg, DONOT_RET)
#define EXPECT_BYTES_NE(bytes1, bytes2, length, msg) UT_BYTES_NE(bytes1, bytes2, length, msg, DONOT_RET)

/* For comparing uint64_t, like hw_id_t. */
#define EXPECT_EQ_LL(expected, actual, msg) UT_EQ_LL(expected, actual, msg, DONOT_RET)

/*
 * The ASSERT_* macros are similar to the EXPECT_* macros except that
 * they return on failure.
 */
#define ASSERT_NOT_NULL(p)                        \
    if (!p) {                                     \
        UNITTEST_TRACEF("ERROR: NULL pointer\n"); \
        return false;                             \
    }

#define ASSERT_CMP(op, msg, lhs, rhs, lhs_str, rhs_str) UT_CMP(op, msg, lhs, rhs, lhs_str, rhs_str, RET_FALSE)

#define ASSERT_EQ(lhs, rhs, msg) ASSERT_CMP(==, msg, lhs, rhs, #lhs, #rhs)
#define ASSERT_NEQ(lhs, rhs, msg) ASSERT_CMP(!=, msg, lhs, rhs, #lhs, #rhs)
#define ASSERT_LE(lhs, rhs, msg) ASSERT_CMP(<=, msg, lhs, rhs, #lhs, #rhs)
#define ASSERT_GE(lhs, rhs, msg) ASSERT_CMP(>=, msg, lhs, rhs, #lhs, #rhs)
#define ASSERT_LT(lhs, rhs, msg) ASSERT_CMP(<, msg, lhs, rhs, #lhs, #rhs)
#define ASSERT_GT(lhs, rhs, msg) ASSERT_CMP(>, msg, lhs, rhs, #lhs, #rhs)

#define ASSERT_TRUE(actual, msg) UT_TRUE(actual, msg, RET_FALSE)
#define ASSERT_FALSE(actual, msg) UT_FALSE(actual, msg, RET_FALSE)
#define ASSERT_NULL(actual, msg) UT_NULL(actual, msg, DONOT_RET)
#define ASSERT_NONNULL(actual, msg) UT_NONNULL(actual, msg, DONOT_RET)
#define ASSERT_BYTES_EQ(expected, actual, length, msg) UT_BYTES_EQ(expected, actual, length, msg, RET_FALSE)
#define ASSERT_BYTES_NE(bytes1, bytes2, length, msg) UT_BYTES_NE(bytes1, bytes2, length, msg, RET_FALSE)

/* For comparing uint64_t, like hw_id_t. */
#define ASSERT_EQ_LL(expected, actual, msg) UT_EQ_LL(expected, actual, msg, RET_FALSE)

/*
 * The list of test cases is made up of these elements.
 */
struct test_case_element {
    struct test_case_element* next;
    struct test_case_element* failed_next;
    const char* name;
    bool (*test_case)(void);
};

/*
 * Struct to store current test case info
 */
struct test_info {
    bool all_ok;
};

/*
 * Object which stores current test info
 */
__UNUSED static struct test_info* current_test_info;

/*
 * Registers a test case with the unit test framework.
 */
void unittest_register_test_case(struct test_case_element* elem);

/*
 * Runs all registered test cases.
 */
bool unittest_run_all_tests(int argc, char** argv);

/*
 * Returns false if expected does not equal actual and prints msg and a hexdump8
 * of the input buffers.
 */
bool unittest_expect_bytes_eq(const uint8_t* expected, const uint8_t* actual, size_t len,
                              const char* msg);

__END_CDECLS
