// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
 * NOTE: at the moment auto-registration of tests does not work, so in order
 * to register a test case, use unittest_register_test_case() before running them:
 * unittest_register_test_case(&_foo_tests_element);
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

#include <runtime/compiler.h>

#define PRINT_BUFFER_SIZE (512)

__BEGIN_CDECLS

/*
 * Type for unit test result Output
 */
typedef void (*test_output_func)(const char* line, int len, void* arg);

/*
 * Printf dedicated to the unittest library
 * the default output is the printf
 */
void unittest_printf(const char* format, ...);

/*
 * Function to set the callback for printing
 * the unit test output
 */
void unittest_set_output_function(test_output_func fun, void* arg);

/*
 * Macros to format the error string
 */
#define UNITTEST_TRACEF(str, x...)                                  \
    do {                                                            \
        unittest_printf(" [FAILED] \n        %s:%d:\n        " str, \
                        __PRETTY_FUNCTION__, __LINE__, ##x);        \
    } while (0)

/*
 * BEGIN_TEST_CASE and END_TEST_CASE define a function that calls
 * RUN_TEST.
 */
#define BEGIN_TEST_CASE(case_name) \
    bool case_name(void) {         \
        bool all_success = true;   \
        unittest_printf("\nCASE %-50s [STARTED] \n", #case_name);

#define DEFINE_REGISTER_TEST_CASE(case_name)                        \
    static void _register_##case_name(void) {                       \
        unittest_register_test_case(&_##case_name##_element);       \
    }                                                               \
    void (*_register_##case_name##_ptr)(void) __SECTION(".ctors") = \
        _register_##case_name;

#define END_TEST_CASE(case_name)                               \
    if (all_success) {                                         \
        unittest_printf("CASE %-50s [PASSED]\n", #case_name);  \
    } else {                                                   \
        unittest_printf("CASE %-50s [FAILED]\n", #case_name);  \
    }                                                          \
    return all_success;                                        \
    }                                                          \
    static struct test_case_element _##case_name##_element = { \
        .next = NULL,                                          \
        .failed_next = NULL,                                   \
        .name = #case_name,                                    \
        .test_case = case_name,                                \
    };                                                         \
    DEFINE_REGISTER_TEST_CASE(case_name);

#define RUN_TEST(test)                             \
    unittest_printf("    %-51s [RUNNING]", #test); \
    if (!test()) {                                 \
        all_success = false;                       \
    } else {                                       \
        unittest_printf(" [PASSED] \n");           \
    }

/*
 * BEGIN_TEST and END_TEST go in a function that is called by RUN_TEST
 * and that call the EXPECT_ macros.
 */
#define BEGIN_TEST bool all_ok = true, expect_failed = false
// To remove unused variable error using expect_failed
#define END_TEST         \
    (void)expect_failed; \
    return all_ok

#ifdef __cplusplus
#define AUTO_TYPE_VAR(type) auto&
#else
#define AUTO_TYPE_VAR(type) __typeof__(type)
#endif

#define EXPECT_CMP(op, msg, lhs, rhs, lhs_str, rhs_str)               \
    do {                                                              \
        expect_failed = false;                                        \
        const AUTO_TYPE_VAR(lhs) _lhs_val = (lhs);                    \
        const AUTO_TYPE_VAR(rhs) _rhs_val = (rhs);                    \
        if (!(_lhs_val op _rhs_val)) {                                \
            UNITTEST_TRACEF(                                          \
                "%s:\n"                                               \
                "        Comparison failed: %s %s %s is false\n"      \
                "        Specifically, %ld %s %ld is false\n",        \
                msg, lhs_str, #op, rhs_str, _lhs_val, #op, _rhs_val); \
            all_ok = false;                                           \
            expect_failed = true;                                     \
        }                                                             \
    } while (0)

/*
 * Use the EXPECT_* macros to check test results.
 */
#define EXPECT_EQ(lhs, rhs, msg) EXPECT_CMP(==, msg, lhs, rhs, #lhs, #rhs)
#define EXPECT_NEQ(lhs, rhs, msg) EXPECT_CMP(!=, msg, lhs, rhs, #lhs, #rhs)
#define EXPECT_LE(lhs, rhs, msg) EXPECT_CMP(<=, msg, lhs, rhs, #lhs, #rhs)
#define EXPECT_GE(lhs, rhs, msg) EXPECT_CMP(>=, msg, lhs, rhs, #lhs, #rhs)
#define EXPECT_LT(lhs, rhs, msg) EXPECT_CMP(<, msg, lhs, rhs, #lhs, #rhs)
#define EXPECT_GT(lhs, rhs, msg) EXPECT_CMP(>, msg, lhs, rhs, #lhs, #rhs)

#define EXPECT_TRUE(actual, msg)                            \
    expect_failed = false;                                  \
    if (!(actual)) {                                        \
        UNITTEST_TRACEF("%s: %s is false\n", msg, #actual); \
        all_ok = false;                                     \
        expect_failed = true;                               \
    }

#define EXPECT_FALSE(actual, msg)                          \
    expect_failed = false;                                 \
    if (actual) {                                          \
        UNITTEST_TRACEF("%s: %s is true\n", msg, #actual); \
        all_ok = false;                                    \
        expect_failed = true;                              \
    }

#define EXPECT_BYTES_EQ(expected, actual, length, msg)                    \
    expect_failed = false;                                                \
    if (!unittest_expect_bytes_eq((expected), (actual), (length), msg)) { \
        all_ok = false;                                                   \
        expect_failed = true;                                             \
    }

#define EXPECT_BYTES_NE(bytes1, bytes2, length, msg) \
    expect_failed = false;                           \
    if (!memcmp(bytes1, bytes2, length)) {           \
        UNITTEST_TRACEF(                             \
            "%s and %s are the same; "               \
            "expected different\n",                  \
            #bytes1, #bytes2);                       \
        hexdump8(bytes1, length);                    \
        all_ok = false;                              \
        expect_failed = true;                        \
    }

/* For comparing uint64_t, like hw_id_t. */
#define EXPECT_EQ_LL(expected, actual, msg)                                   \
    do {                                                                      \
        expect_failed = false;                                                \
        const AUTO_TYPE_VAR(expected) _e = (expected);                        \
        const AUTO_TYPE_VAR(actual) _a = (actual);                            \
        if (_e != _a) {                                                       \
            UNITTEST_TRACEF("%s: expected %llu, actual %llu\n", msg, _e, _a); \
            all_ok = false;                                                   \
            expect_failed = true;                                             \
        }                                                                     \
    } while (0)

#define RET_ON_ASSERT_FAIL \
    if (expect_failed) {   \
        return false;      \
    }

/*
 * The ASSERT_* macros are similar to the EXPECT_* macros except that
 * they return on failure.
 */
#define ASSERT_NOT_NULL(p)                        \
    if (!p) {                                     \
        UNITTEST_TRACEF("ERROR: NULL pointer\n"); \
        return false;                             \
    }

#define ASSERT_CMP(op, msg, lhs, rhs, lhs_str, rhs_str) \
    EXPECT_CMP(op, msg, lhs, rhs, lhs_str, rhs_str);    \
    RET_ON_ASSERT_FAIL

#define ASSERT_EQ(lhs, rhs, msg) ASSERT_CMP(==, msg, lhs, rhs, #lhs, #rhs)
#define ASSERT_NEQ(lhs, rhs, msg) ASSERT_CMP(!=, msg, lhs, rhs, #lhs, #rhs)
#define ASSERT_LE(lhs, rhs, msg) ASSERT_CMP(<=, msg, lhs, rhs, #lhs, #rhs)
#define ASSERT_GE(lhs, rhs, msg) ASSERT_CMP(>=, msg, lhs, rhs, #lhs, #rhs)
#define ASSERT_LT(lhs, rhs, msg) ASSERT_CMP(<, msg, lhs, rhs, #lhs, #rhs)
#define ASSERT_GT(lhs, rhs, msg) ASSERT_CMP(>, msg, lhs, rhs, #lhs, #rhs)

#define ASSERT_TRUE(actual, msg) \
    EXPECT_TRUE(actual, msg);    \
    RET_ON_ASSERT_FAIL

#define ASSERT_FALSE(actual, msg) \
    EXPECT_FALSE(actual, msg);    \
    RET_ON_ASSERT_FAIL

#define ASSERT_BYTES_EQ(expected, actual, length, msg) \
    EXPECT_BYTES_EQ(expected, actual, length, msg);    \
    RET_ON_ASSERT_FAIL

#define ASSERT_BYTES_NE(bytes1, bytes2, length, msg) \
    EXPECT_BYTES_NE(bytes1, bytes2, length, msg);    \
    RET_ON_ASSERT_FAIL

/* For comparing uint64_t, like hw_id_t. */
#define ASSERT_EQ_LL(expected, actual, msg) \
    EXPECT_EQ_LL(expected, actual, msg);    \
    RET_ON_ASSERT_FAIL

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
 * Registers a test case with the unit test framework.
 */
void unittest_register_test_case(struct test_case_element* elem);

/*
 * Runs all registered test cases.
 */
bool unittest_run_all_tests(void);

/*
 * Returns false if expected does not equal actual and prints msg and a hexdump8
 * of the input buffers.
 */
bool unittest_expect_bytes_eq(const uint8_t* expected, const uint8_t* actual, size_t len,
                              const char* msg);

__END_CDECLS
