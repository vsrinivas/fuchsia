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
 *      EXPECT_NE(MX_ERR_TIMED_OUT, foo_event(), "event timed out");
 *
 *      END_TEST;
 * }
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/compiler.h>

#ifdef __Fuchsia__
#include <magenta/types.h>
#define UNITTEST_CRASH_HANDLER_SUPPORTED
#endif // __Fuchsia__

#define PRINT_BUFFER_SIZE (512)

// The following helper function makes the "msg" argument optional in
// C++, so that you can write either of the following:
//   ASSERT_EQ(x, y, "Check that x equals y");
//   ASSERT_EQ(x, y);
// (We could allow the latter in C by making unittest_get_message() a
// var-args function, but that would be less type safe.)
static inline const char* unittest_get_message(const char* arg) {
    return arg;
}
#ifdef __cplusplus
static inline const char* unittest_get_message() {
    return "<no message>";
}
#endif

// A workaround to help static analyzer identify assertion failures
#if defined(__clang__)
#define MX_ANALYZER_CREATE_SINK     __attribute__((annotate("mx_create_sink")))
#else
#define MX_ANALYZER_CREATE_SINK     //no-op
#endif
// This function will help terminate the static analyzer when it reaches
// an assertion failure site which returns from test case function. The bugs
// discovered by the static analyzer will be suppressed as they are expected
// by the test cases.
static inline void unittest_returns_early(void) MX_ANALYZER_CREATE_SINK {}

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
        unittest_printf_critical(                                             \
            " [FAILED] \n        %s:%d:%s:\n        " str,                    \
            __FILE__, __LINE__, __PRETTY_FUNCTION__, ##x);                    \
    } while (0)

/*
 * Internal-only.
 * Used by macros to check that the test state is set up correctly.
 */
#define UT_ASSERT_VALID_TEST_STATE                                   \
    do {                                                             \
        if (current_test_info == NULL) {                             \
            unittest_printf_critical(                                \
                "FATAL: %s:%d:%s: Invalid state for EXPECT/ASSERT: " \
                "possible missing BEGIN_TEST or BEGIN_HELPER\n",     \
                __FILE__, __LINE__, __PRETTY_FUNCTION__);            \
            exit(101); /* Arbitrary, atypical exit status */         \
        }                                                            \
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

#define RUN_NAMED_TEST_TYPE(name, test, test_type, enable_crash_handler) \
    unittest_run_named_test(name, test, test_type, &current_test_info,   \
                            &all_success, enable_crash_handler);

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
#define RUN_TEST_SMALL(test) RUN_NAMED_TEST_TYPE(#test, test, TEST_SMALL, false)
#define RUN_TEST_MEDIUM(test) RUN_NAMED_TEST_TYPE(#test, test, TEST_MEDIUM, false)
#define RUN_TEST_LARGE(test) RUN_NAMED_TEST_TYPE(#test, test, TEST_LARGE, false)
#define RUN_TEST_PERFORMANCE(test) RUN_NAMED_TEST_TYPE(#test, test, TEST_PERFORMANCE, false)

// "RUN_TEST" implies the test is small
#define RUN_TEST(test) RUN_NAMED_TEST_TYPE(#test, test, TEST_SMALL, false)
#define RUN_NAMED_TEST(name, test) RUN_NAMED_TEST_TYPE(name, test, TEST_SMALL, false)

#ifdef UNITTEST_CRASH_HANDLER_SUPPORTED

#define RUN_TEST_ENABLE_CRASH_HANDLER(test) RUN_NAMED_TEST_TYPE(#test, test, TEST_SMALL, true)

/**
 * Registers the process or thread as expected to crash. Tests utilizing this
 * should be run with RUN_TEST_ENABLE_CRASH_HANDLER. If a crash occurs and
 * matches a registered process or thread, it is not bubbled up to the crashlogger
 * and the test continues. If any crash was registered but did not occur,
 * the test fails.
 * Unregistered crashes will also fail the test.
 *
 * A use case could be as follows:
 *
 * static bool test_foo_process_expected_crash(void)
 * {
 *      BEGIN_TEST;
 *
 *      ...create a process...
 *      mx_handle_t process;
 *      mx_handle_t vmar;
 *      ASSERT_EQ(mx_process_create(mx_job_default(), fooName, sizeof(fooName),
 *                                  0, &process, &vmar),
 *                MX_OK, ""));
 *      ...register the process as expected to crash...
 *      REGISTER_CRASH(process);
 *      ...trigger the crash...
 *
 *      END_TEST;
 * }
 */
#define REGISTER_CRASH(handle) \
    unittest_register_crash(current_test_info, handle)

#endif // UNITTEST_CRASH_HANDLER_SUPPORTED

/*
 * BEGIN_TEST and END_TEST go in a function that is called by RUN_TEST
 * and that call the EXPECT_ macros.
 */
#define BEGIN_TEST                        \
    do {                                  \
        UT_ASSERT_VALID_TEST_STATE;       \
    } while (0)

#define END_TEST                          \
    do {                                  \
        UT_ASSERT_VALID_TEST_STATE;       \
        return current_test_info->all_ok; \
    } while (0)

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
    struct test_info _ut_helper_test_info = { .all_ok = true, .crash_list = NULL }; \
    struct test_info* current_test_info = &_ut_helper_test_info; \
// By referring to _ut_helper_test_info, we guarantee that
// END_HELPER is matched with BEGIN_HELPER.
#define END_HELPER \
    return _ut_helper_test_info.all_ok

#ifdef __cplusplus
#define AUTO_TYPE_VAR(type) auto
#else
#define AUTO_TYPE_VAR(type) __typeof__(type)
#endif

#define RET_FALSE do { unittest_returns_early(); return false; } while (0)
#define DONOT_RET

#define UT_CMP(op, lhs, rhs, lhs_str, rhs_str, ret, ...)                \
    do {                                                                \
        UT_ASSERT_VALID_TEST_STATE;                                     \
        const AUTO_TYPE_VAR(lhs) _lhs_val = (lhs);                      \
        const AUTO_TYPE_VAR(rhs) _rhs_val = (rhs);                      \
        if (!(_lhs_val op _rhs_val)) {                                  \
            UNITTEST_TRACEF(                                            \
                "%s:\n"                                                 \
                "        Comparison failed: %s %s %s is false\n"        \
                "        Specifically, %lld (0x%llx) %s %lld (0x%llx) is false\n", \
                unittest_get_message(__VA_ARGS__),                      \
                lhs_str, #op, rhs_str, (long long int)_lhs_val,         \
                (unsigned long long)_lhs_val,                           \
                #op, (long long int)_rhs_val,                           \
                (unsigned long long)_rhs_val);                          \
            current_test_info->all_ok = false;                          \
            ret;                                                        \
        }                                                               \
    } while (0)

#define UT_TRUE(actual, ret, ...)                                       \
    do {                                                                \
        UT_ASSERT_VALID_TEST_STATE;                                     \
        if (!(actual)) {                                                \
            UNITTEST_TRACEF("%s: %s is false\n",                        \
                            unittest_get_message(__VA_ARGS__),          \
                            #actual);                                   \
            current_test_info->all_ok = false;                          \
            ret;                                                        \
        }                                                               \
    } while (0)

#define UT_FALSE(actual, ret, ...)                                      \
    do {                                                                \
        UT_ASSERT_VALID_TEST_STATE;                                     \
        if (actual) {                                                   \
            UNITTEST_TRACEF("%s: %s is true\n",                         \
                            unittest_get_message(__VA_ARGS__),          \
                            #actual);                                   \
            current_test_info->all_ok = false;                          \
            ret;                                                        \
        }                                                               \
    } while (0)

#define UT_NULL(actual, ret, ...)                                   \
    do {                                                            \
        UT_ASSERT_VALID_TEST_STATE;                                 \
        if (actual != NULL) {                                       \
            UNITTEST_TRACEF("%s: %s is non-null!\n",                \
                            unittest_get_message(__VA_ARGS__),      \
                            #actual);                               \
            current_test_info->all_ok = false;                      \
            ret;                                                    \
        }                                                           \
    } while (0)

#define UT_NONNULL(actual, ret, ...)                            \
    do {                                                        \
        UT_ASSERT_VALID_TEST_STATE;                             \
        if (actual == NULL) {                                   \
            UNITTEST_TRACEF("%s: %s is null!\n",                \
                            unittest_get_message(__VA_ARGS__),  \
                            #actual);                           \
            current_test_info->all_ok = false;                  \
            ret;                                                \
        }                                                       \
    } while (0)

#define UT_BYTES_EQ(expected, actual, length, msg, ret)                       \
    do {                                                                      \
        UT_ASSERT_VALID_TEST_STATE;                                           \
        if (!unittest_expect_bytes_eq((expected), (actual), (length), msg)) { \
            current_test_info->all_ok = false;                                \
            ret;                                                              \
        }                                                                     \
    } while (0)

#define UT_BYTES_NE(bytes1, bytes2, length, msg, ret) \
    do {                                              \
        UT_ASSERT_VALID_TEST_STATE;                   \
        if (!memcmp(bytes1, bytes2, length)) {        \
            UNITTEST_TRACEF(                          \
                "%s: %s and %s are the same; "        \
                "expected different\n",               \
                msg, #bytes1, #bytes2);               \
            hexdump8(bytes1, length);                 \
            current_test_info->all_ok = false;        \
            ret;                                      \
        }                                             \
    } while (0)

#define UT_STR_EQ(expected, actual, length, msg, ret)                       \
    do {                                                                    \
        UT_ASSERT_VALID_TEST_STATE;                                         \
        if (!unittest_expect_str_eq((expected), (actual), (length), msg)) { \
            current_test_info->all_ok = false;                              \
            ret;                                                            \
        }                                                                   \
    } while (0)

#define UT_STR_NE(str1, str2, length, msg, ret) \
    do {                                        \
        UT_ASSERT_VALID_TEST_STATE;             \
        if (!strncmp(str1, str2, length)) {     \
            UNITTEST_TRACEF(                    \
                "%s: %s and %s are the same; "  \
                "expected different:\n"         \
                "        '%s'\n",               \
                msg, #str1, #str2, str1);       \
            current_test_info->all_ok = false;  \
            ret;                                \
        }                                       \
    } while (0)

/* For comparing uint64_t, like hw_id_t. */
#define UT_EQ_LL(expected, actual, msg, ret)                                  \
    do {                                                                      \
        UT_ASSERT_VALID_TEST_STATE;                                           \
        const AUTO_TYPE_VAR(expected) _e = (expected);                        \
        const AUTO_TYPE_VAR(actual) _a = (actual);                            \
        if (_e != _a) {                                                       \
            UNITTEST_TRACEF("%s: expected %llu, actual %llu\n", msg, _e, _a); \
            current_test_info->all_ok = false;                                \
            ret;                                                              \
        }                                                                     \
    } while (0)

#define EXPECT_CMP(op, lhs, rhs, lhs_str, rhs_str, ...) \
    UT_CMP(op, lhs, rhs, lhs_str, rhs_str, DONOT_RET, ##__VA_ARGS__)

/*
 * Use the EXPECT_* macros to check test results.
 */
#define EXPECT_EQ(lhs, rhs, ...) EXPECT_CMP(==, lhs, rhs, #lhs, #rhs, ##__VA_ARGS__)
#define EXPECT_NE(lhs, rhs, ...) EXPECT_CMP(!=, lhs, rhs, #lhs, #rhs, ##__VA_ARGS__)
#define EXPECT_LE(lhs, rhs, ...) EXPECT_CMP(<=, lhs, rhs, #lhs, #rhs, ##__VA_ARGS__)
#define EXPECT_GE(lhs, rhs, ...) EXPECT_CMP(>=, lhs, rhs, #lhs, #rhs, ##__VA_ARGS__)
#define EXPECT_LT(lhs, rhs, ...) EXPECT_CMP(<, lhs, rhs, #lhs, #rhs, ##__VA_ARGS__)
#define EXPECT_GT(lhs, rhs, ...) EXPECT_CMP(>, lhs, rhs, #lhs, #rhs, ##__VA_ARGS__)

#define EXPECT_TRUE(actual, ...) UT_TRUE(actual, DONOT_RET, ##__VA_ARGS__)
#define EXPECT_FALSE(actual, ...) UT_FALSE(actual, DONOT_RET, ##__VA_ARGS__)
#define EXPECT_NULL(actual, ...) UT_NULL(actual, DONOT_RET, ##__VA_ARGS__)
#define EXPECT_NONNULL(actual, ...) UT_NONNULL(actual, DONOT_RET, ##__VA_ARGS__)
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
#define ASSERT_NOT_NULL(p)                            \
    do {                                              \
        UT_ASSERT_VALID_TEST_STATE;                   \
        if (!p) {                                     \
            UNITTEST_TRACEF("ERROR: NULL pointer\n"); \
            return false;                             \
        }                                             \
    } while (0)

#define ASSERT_CMP(op, lhs, rhs, lhs_str, rhs_str, ...) \
    UT_CMP(op, lhs, rhs, lhs_str, rhs_str, RET_FALSE, ##__VA_ARGS__)

#define ASSERT_EQ(lhs, rhs, ...) ASSERT_CMP(==, lhs, rhs, #lhs, #rhs, ##__VA_ARGS__)
#define ASSERT_NE(lhs, rhs, ...) ASSERT_CMP(!=, lhs, rhs, #lhs, #rhs, ##__VA_ARGS__)
#define ASSERT_LE(lhs, rhs, ...) ASSERT_CMP(<=, lhs, rhs, #lhs, #rhs, ##__VA_ARGS__)
#define ASSERT_GE(lhs, rhs, ...) ASSERT_CMP(>=, lhs, rhs, #lhs, #rhs, ##__VA_ARGS__)
#define ASSERT_LT(lhs, rhs, ...) ASSERT_CMP(<, lhs, rhs, #lhs, #rhs, ##__VA_ARGS__)
#define ASSERT_GT(lhs, rhs, ...) ASSERT_CMP(>, lhs, rhs, #lhs, #rhs, ##__VA_ARGS__)

#define ASSERT_TRUE(actual, ...) UT_TRUE(actual, RET_FALSE, ##__VA_ARGS__)
#define ASSERT_FALSE(actual, ...) UT_FALSE(actual, RET_FALSE, ##__VA_ARGS__)
#define ASSERT_NULL(actual, ...) UT_NULL(actual, RET_FALSE, ##__VA_ARGS__)
#define ASSERT_NONNULL(actual, ...) UT_NONNULL(actual, RET_FALSE, ##__VA_ARGS__)
#define ASSERT_BYTES_EQ(expected, actual, length, msg) UT_BYTES_EQ(expected, actual, length, msg, RET_FALSE)
#define ASSERT_BYTES_NE(bytes1, bytes2, length, msg) UT_BYTES_NE(bytes1, bytes2, length, msg, RET_FALSE)
#define ASSERT_STR_EQ(expected, actual, length, msg) UT_STR_EQ(expecetd, actual, length, msg, RET_FALSE)
#define ASSERT_STR_NE(str1, str2, length, msg) UT_STR_NE(str1, str2, length, msg, RET_FALSE)

/* For comparing uint64_t, like hw_id_t. */
#define ASSERT_EQ_LL(expected, actual, msg) UT_EQ_LL(expected, actual, msg, RET_FALSE)

#ifdef UNITTEST_CRASH_HANDLER_SUPPORTED

/**
 * Runs the given function in a separate thread, and fails if the function does not crash.
 * This is a blocking call.
 *
 * static void crash(void* arg) {
 *      ...trigger the crash...
 * }
 *
 * static bool test_crash(void)
 * {
 *      BEGIN_TEST;
 *
 *      ...construct arg...
 *
 *      ASSERT_DEATH(crash, arg, "msg about crash");
 *
 *      END_TEST;
 * }
 */
#define ASSERT_DEATH(fn, arg, msg) ASSERT_TRUE(unittest_run_death_fn(fn, arg), msg)

#endif // UNITTEST_CRASH_HANDLER_SUPPORTED

/*
 * The list of test cases is made up of these elements.
 */
struct test_case_element {
    struct test_case_element* next;
    struct test_case_element* failed_next;
    const char* name;
    bool (*test_case)(void);
};

/* List of processes or threads which are expected to crash. */
typedef struct crash_list* crash_list_t;

/*
 * Struct to store current test case info
 */
struct test_info {
    bool all_ok;
    crash_list_t crash_list;
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
                             bool* all_success, bool enable_crash_handler);

#ifdef UNITTEST_CRASH_HANDLER_SUPPORTED
void unittest_register_crash(struct test_info* current_test_info, mx_handle_t handle);
bool unittest_run_death_fn(void (*fn_to_run)(void*), void* arg);
#endif // UNITTEST_CRASH_HANDLER_SUPPORTED

__END_CDECLS
