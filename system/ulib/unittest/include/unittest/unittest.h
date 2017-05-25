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

typedef enum test_type {
    TEST_SMALL       = 0x00000001,
    TEST_MEDIUM      = 0x00000002,
    TEST_LARGE       = 0x00000004,
    TEST_PERFORMANCE = 0x00000008,
    TEST_ALL         = 0xFFFFFFFF,
} test_type_t;

#define TEST_ENV_NAME "RUNTESTS_TEST_CLASS"
#define TEST_DEFAULT (TEST_SMALL | TEST_MEDIUM)

extern test_type_t utest_test_type;

/*
 * Type for unit test result Output
 */
typedef void (*test_output_func)(const char* line, int len, void* arg);

/*
 * Printf dedicated to the unittest library
 * the default output is the printf
 */
void unittest_printf_critical(const char* format, ...)
    __attribute__((format (printf, 1, 2)));

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

#define RUN_NAMED_TEST_TYPE(name, test, test_type)                     \
    unittest_run_named_test(name, test, test_type, &current_test_info, \
                            &all_success);

#define TEST_CASE_ELEMENT(case_name) &_##case_name##_element

/*
 * Test classes:
 *
 * Small: Isolated tests for functions and classes. These must be totally
 * synchronous and single-threaded. These tests should be parallelizable;
 * there shouldn't be any shared resources between them.
 *
 * Medium: Single-process integration tests. Ideally these are also synchronous
 * and single-threaded but they might run through a large chunk of code in each
 * test case, or they might use disk, making them a bit slower.
 *
 * Large: Multi-process (or particularly incomprehensible single-process)
 * integration tests. These tests are often too flaky to run in a CQ, and we
 * should try to limit how many we have.
 *
 * Performance: Tests which are expected to pass, but which are measured
 * using other metrics (thresholds, statistical techniques) to identify
 * regressions.
*/
#define RUN_TEST_SMALL(test) RUN_NAMED_TEST_TYPE(#test, test, TEST_SMALL)
#define RUN_TEST_MEDIUM(test) RUN_NAMED_TEST_TYPE(#test, test, TEST_MEDIUM)
#define RUN_TEST_LARGE(test) RUN_NAMED_TEST_TYPE(#test, test, TEST_LARGE)
#define RUN_TEST_PERFORMANCE(test) RUN_NAMED_TEST_TYPE(#test, test, TEST_PERFORMANCE)

// "RUN_TEST" implies the test is small
#define RUN_TEST(test) RUN_NAMED_TEST_TYPE(#test, test, TEST_SMALL)
#define RUN_NAMED_TEST(name, test) RUN_NAMED_TEST_TYPE(name, test, TEST_SMALL)

/*
 * BEGIN_TEST and END_TEST go in a function that is called by RUN_TEST
 * and that call the EXPECT_ macros.
 */
#define BEGIN_TEST current_test_info->all_ok = true
#define END_TEST return current_test_info->all_ok

/*
 * BEGIN_HELPER and END_HELPER let helper threads and files use
 * the ASSERT_*,EXPECT_* macros, which require an in-scope, non-NULL
 * |test_info* current_test_info|.
 *
 * This header file defines a static |current_test_info|, which is unlocked and
 * should only be touched by the main thread; also, it is not visible to
 * functions in other compilation units.
 *
 * Example usage:
 *
 *   bool my_helper_in_another_file_or_thread() {
 *       BEGIN_HELPER;
 *       // Use ASSERT_* or EXPECT_*
 *       END_HELPER;  // Returns false if any EXPECT calls failed.
 *   }
 */
// Intentionally shadows the global current_test_info to avoid accidentally
// leaking dangling stack pointers.
#define BEGIN_HELPER \
    struct test_info _test_info; \
    struct test_info* current_test_info = &_test_info; \
    current_test_info->all_ok = true
#define END_HELPER \
    return current_test_info->all_ok

#ifdef __cplusplus
#define AUTO_TYPE_VAR(type) auto
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
                "        Specifically, %lld %s %lld is false\n",      \
                msg, lhs_str, #op, rhs_str, (long long int)_lhs_val,  \
                #op, (long long int)_rhs_val);                        \
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
            "%s: %s and %s are the same; "            \
            "expected different\n",                   \
            msg, #bytes1, #bytes2);                   \
        hexdump8(bytes1, length);                     \
        current_test_info->all_ok = false;            \
        ret;                                          \
    }

#define UT_STR_EQ(expected, actual, length, msg, ret)                   \
    if (!unittest_expect_str_eq((expected), (actual), (length), msg)) { \
        current_test_info->all_ok = false;                              \
        ret;                                                            \
    }

#define UT_STR_NE(str1, str2, length, msg, ret) \
    if (!strncmp(str1, str2, length)) {         \
        UNITTEST_TRACEF(                        \
            "%s: %s and %s are the same; "      \
            "expected different:\n"             \
            "        '%s'\n",                   \
            msg, #str1, #str2, str1);           \
        current_test_info->all_ok = false;      \
        ret;                                    \
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
#define EXPECT_STR_EQ(expected, actual, length, msg) UT_STR_EQ(expected, actual, length, msg, DONOT_RET)
#define EXPECT_STR_NE(str1, str2, length, msg) UT_STR_NE(str1, str2, length, msg, DONOT_RET)

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
#define ASSERT_NULL(actual, msg) UT_NULL(actual, msg, RET_FALSE)
#define ASSERT_NONNULL(actual, msg) UT_NONNULL(actual, msg, RET_FALSE)
#define ASSERT_BYTES_EQ(expected, actual, length, msg) UT_BYTES_EQ(expected, actual, length, msg, RET_FALSE)
#define ASSERT_BYTES_NE(bytes1, bytes2, length, msg) UT_BYTES_NE(bytes1, bytes2, length, msg, RET_FALSE)
#define ASSERT_STR_EQ(expected, actual, length, msg) UT_STR_EQ(expecetd, actual, length, msg, RET_FALSE)
#define ASSERT_STR_NE(str1, str2, length, msg) UT_STR_NE(str1, str2, length, msg, RET_FALSE)

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
 * Runs a single test case.
 */
bool unittest_run_one_test(struct test_case_element* elem, test_type_t type);

/*
 * Returns false if expected does not equal actual and prints msg and a hexdump8
 * of the input buffers.
 */
bool unittest_expect_bytes_eq(const uint8_t* expected, const uint8_t* actual, size_t len,
                              const char* msg);

bool unittest_expect_str_eq(const char* expected, const char* actual, size_t len, const char* msg);

/* Used to implement RUN_TEST() and other variants. */
void unittest_run_named_test(const char* name, bool (*test)(void),
                             test_type_t test_type,
                             struct test_info** current_test_info,
                             bool* all_success);

__END_CDECLS
