// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef _LIB_UNITTEST_INCLUDE_UNITTEST_H_
#define _LIB_UNITTEST_INCLUDE_UNITTEST_H_
/*
 * Macros for writing unit tests.
 *
 * Sample usage:
 *
 * A test case runs a collection of unittests, with
 * UNITTEST_START_TESTCASE and UNITTEST_END_TESTCASE
 * BEGIN_TEST_CASE and END_TEST_CASE at the beginning and end of the list of
 * unitests, and UNITTEST for each individual test, as follows:
 *
 *  UNITTEST_START_TESTCASE(foo_tests)
 *
 *  UNITTEST(test_foo);
 *  UNITTEST(test_bar);
 *  UNITTEST(test_baz);
 *
 *  UNITTEST_END_TESTCASE(foo_tests,
 *                        "footest",
 *                        "Test to be sure that your foos have proper bars",
 *                        init_foo_test_env,
 *                        cleanup_foo_test_env);
 *
 * This creates an entry in the global unittest table and registers it with the
 * unit test framework.
 *
 * A test looks like this, using the BEGIN_TEST and END_TEST macros at
 * the beginning and end of the test and the EXPECT_* macros to
 * validate test results, as shown:
 *
 * static bool test_foo(void* context)
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
 *
 * A test case may have an init and cleanup function registered with it in order
 * to set up a shared test environment.  A pointer to the shared environment
 * will be passed as the "context" parameter to each unittest.
 *
 * The init function might look something like...
 *
 * static status_t init_foo_test_env(void** context)
 * {
 *      *context = new FooTestEnvironment(...);
 *
 *      if (!(*context))
 *          return ERR_NO_MEMORY;
 *
 *      return NO_ERROR;
 * }
 *
 * While the cleanup function might look like...
 *
 * static void cleanup_foo_test_env(void* context)
 * {
 *      delete static_cast<FooTestEnvironment*>(context);
 * }
 *
 * To your rules.mk file, add lib/unittest to MODULE_DEPS:
 *
 * MODULE_DEPS += \
 *         lib/unittest   \
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <magenta/compiler.h>
#include <trace.h>

__BEGIN_CDECLS

/*
 * Printf dedicated to the unittest library
 * the default output is the printf
 */
int unittest_printf(const char* format, ...);

/*
 * Macros to format the error string
 */
#define EXPECTED_STRING "%s:\n        expected "
#define UNITTEST_TRACEF_FORMAT "\n        [FAILED]\n        %s:%d:\n        "
#define UNITTEST_TRACEF(str, x...)                                             \
    do {                                                                       \
        unittest_printf(UNITTEST_TRACEF_FORMAT str,                            \
                        __PRETTY_FUNCTION__, __LINE__, ##x);                   \
    } while (0)

/*
 * BEGIN_TEST and END_TEST go in a function that is called by RUN_TEST
 * and that call the EXPECT_ macros.
 */

#define BEGIN_TEST bool all_ok = true
#define END_TEST return all_ok

#ifdef __cplusplus
#define AUTO_TYPE_VAR(type) auto&
#else
#define AUTO_TYPE_VAR(type) __typeof__(type)
#endif

/*
 * UTCHECK_* macros are used to check test results.  Generally, one should
 * prefer to use either the EXPECT_* (non-terminating) or REQUIRE_*
 * (terminating) forms of the macros.  See below.
 */
#define UTCHECK_EQ(expected, actual, msg, term)                                \
    do {                                                                       \
        const AUTO_TYPE_VAR(expected) _e = (expected);                         \
        const AUTO_TYPE_VAR(actual) _a = (actual);                             \
        if (_e != _a) {                                                        \
            UNITTEST_TRACEF(EXPECTED_STRING                                    \
                            "%s (%ld), "                                       \
                            "actual %s (%ld)\n",                               \
                            msg, #expected, (long)_e, #actual, (long)_a);      \
            if (term) return false; else all_ok = false;                       \
        }                                                                      \
    } while (0)

#define UTCHECK_NEQ(expected, actual, msg, term)                               \
    do {                                                                       \
        const AUTO_TYPE_VAR(expected) _e = (expected);                         \
        if (_e == (actual)) {                                                  \
            UNITTEST_TRACEF(EXPECTED_STRING                                    \
                            "%s (%ld), %s"                                     \
                            " to differ, but they are the same %ld\n",         \
                            msg, #expected, (long)_e, #actual);                \
            if (term) return false; else all_ok = false;                       \
        }                                                                      \
    } while (0)

#define UTCHECK_LE(expected, actual, msg, term)                                \
    do {                                                                       \
        const AUTO_TYPE_VAR(expected) _e = (expected);                         \
        const AUTO_TYPE_VAR(actual) _a = (actual);                             \
        if (_e > _a) {                                                         \
            UNITTEST_TRACEF(EXPECTED_STRING                                    \
                            "%s (%ld) to be"                                   \
                            " less-than-or-equal-to actual %s (%ld)\n",        \
                            msg, #expected, (long)_e, #actual, (long)_a);      \
            if (term) return false; else all_ok = false;                       \
        }                                                                      \
    } while (0)

#define UTCHECK_LT(expected, actual, msg, term)                                \
    do {                                                                       \
        const AUTO_TYPE_VAR(expected) _e = (expected);                         \
        const AUTO_TYPE_VAR(actual) _a = (actual);                             \
        if (_e >= _a) {                                                        \
            UNITTEST_TRACEF(EXPECTED_STRING                                    \
                            "%s (%ld) to be"                                   \
                            " less-than actual %s (%ld)\n",                    \
                            msg, #expected, (long)_e, #actual, (long)_a);      \
            if (term) return false; else all_ok = false;                       \
        }                                                                      \
    } while (0)

#define UTCHECK_GE(expected, actual, msg, term)                                \
    do {                                                                       \
        const AUTO_TYPE_VAR(expected) _e = (expected);                         \
        const AUTO_TYPE_VAR(actual) _a = (actual);                             \
        if (_e < _a) {                                                         \
            UNITTEST_TRACEF(EXPECTED_STRING                                    \
                            "%s (%ld) to be"                                   \
                            " greater-than-or-equal-to actual %s (%ld)\n",     \
                            msg, #expected, (long)_e, #actual, (long)_a);      \
            if (term) return false; else all_ok = false;                       \
        }                                                                      \
    } while (0)

#define UTCHECK_GT(expected, actual, msg, term)                                \
    do {                                                                       \
        const AUTO_TYPE_VAR(expected) _e = (expected);                         \
        const AUTO_TYPE_VAR(actual) _a = (actual);                             \
        if (_e <= _a) {                                                        \
            UNITTEST_TRACEF(EXPECTED_STRING                                    \
                            "%s (%ld) to be"                                   \
                            " greater-than actual %s (%ld)\n",                 \
                            msg, #expected, (long)_e, #actual, (long)_a);      \
            if (term) return false; else all_ok = false;                       \
        }                                                                      \
    } while (0)

#define UTCHECK_TRUE(actual, msg, term)                                        \
    if (!(actual)) {                                                           \
        UNITTEST_TRACEF("%s: %s is false\n", msg, #actual);                    \
        if (term) return false; else all_ok = false;                           \
    }

#define UTCHECK_FALSE(actual, msg, term)                                       \
    if (actual) {                                                              \
        UNITTEST_TRACEF("%s: %s is true\n", msg, #actual);                     \
        if (term) return false; else all_ok = false;                           \
    }

#define UTCHECK_NULL(actual, msg, term)                                        \
    if (actual != NULL) {                                                      \
        UNITTEST_TRACEF("%s: %s is non-null!\n", msg, #actual);                \
        if (term) return false; else all_ok = false;                           \
    }

#define UTCHECK_NONNULL(actual, msg, term)                                     \
    if (actual == NULL) {                                                      \
        UNITTEST_TRACEF("%s: %s is null!\n", msg, #actual);                    \
        if (term) return false; else all_ok = false;                           \
    }

#define UTCHECK_BYTES_EQ(expected, actual, length, msg, term)                  \
    if (!unittest_expect_bytes((expected), #expected,                          \
                               (actual), #actual,                              \
                               (len), msg, __PRETTY_FUNCTION__, __LINE__,      \
                               true)) {                                        \
        if (term) return false; else all_ok = false;                           \
    }

#define UTCHECK_BYTES_NE(expected, actual, length, msg, term)                  \
    if (!unittest_expect_bytes((expected), #expected,                          \
                               (actual), #actual,                              \
                               (len), msg, __PRETTY_FUNCTION__, __LINE__,      \
                               false)) {                                       \
        if (term) return false; else all_ok = false;                           \
    }

/* EXPECT_* macros check the supplied condition and will print a message and flag the test
 * as having failed if the condition fails.  The test will continue to run, even
 * if the condition fails.
 */
#define EXPECT_EQ(expected, actual, msg)               UTCHECK_EQ(expected, actual, msg, false)
#define EXPECT_NEQ(expected, actual, msg)              UTCHECK_NEQ(expected, actual, msg, false)
#define EXPECT_LE(expected, actual, msg)               UTCHECK_LE(expected, actual, msg, false)
#define EXPECT_LT(expected, actual, msg)               UTCHECK_LT(expected, actual, msg, false)
#define EXPECT_GE(expected, actual, msg)               UTCHECK_GE(expected, actual, msg, false)
#define EXPECT_GT(expected, actual, msg)               UTCHECK_GT(expected, actual, msg, false)
#define EXPECT_TRUE(actual, msg)                       UTCHECK_TRUE(actual, msg, false)
#define EXPECT_FALSE(actual, msg)                      UTCHECK_FALSE(actual, msg, false)
#define EXPECT_BYTES_EQ(expected, actual, length, msg) UTCHECK_BYTES_EQ(expected, actual, length, msg, false)
#define EXPECT_BYTES_NE(bytes1, bytes2, length, msg)   UTCHECK_BYTES_NE(bytes1, bytes2, length, msg, false)
#define EXPECT_EQ_LL(expected, actual, msg)            UTCHECK_EQ_LL(expected, actual, msg, false)
#define EXPECT_EQ_LL(expected, actual, msg)            UTCHECK_EQ_LL(expected, actual, msg, false)
#define EXPECT_NULL(actual, msg)                       UTCHECK_NULL(actual, msg, false)
#define EXPECT_NONNULL(actual, msg)                    UTCHECK_NONNULL(actual, msg, false)

/* REQUIRE_* macros check the condition and will print a message and immediately
 * abort a test with a filure status if the condition fails.
 */
#define REQUIRE_EQ(expected, actual, msg)               UTCHECK_EQ(expected, actual, msg, true)
#define REQUIRE_NEQ(expected, actual, msg)              UTCHECK_NEQ(expected, actual, msg, true)
#define REQUIRE_LE(expected, actual, msg)               UTCHECK_LE(expected, actual, msg, true)
#define REQUIRE_LT(expected, actual, msg)               UTCHECK_LT(expected, actual, msg, true)
#define REQUIRE_GE(expected, actual, msg)               UTCHECK_GE(expected, actual, msg, true)
#define REQUIRE_GT(expected, actual, msg)               UTCHECK_GT(expected, actual, msg, true)
#define REQUIRE_TRUE(actual, msg)                       UTCHECK_TRUE(actual, msg, true)
#define REQUIRE_FALSE(actual, msg)                      UTCHECK_FALSE(actual, msg, true)
#define REQUIRE_BYTES_EQ(expected, actual, length, msg) UTCHECK_BYTES_EQ(expected, actual, length, msg, true)
#define REQUIRE_BYTES_NE(bytes1, bytes2, length, msg)   UTCHECK_BYTES_NE(bytes1, bytes2, length, msg, true)
#define REQUIRE_EQ_LL(expected, actual, msg)            UTCHECK_EQ_LL(expected, actual, msg, true)
#define REQUIRE_EQ_LL(expected, actual, msg)            UTCHECK_EQ_LL(expected, actual, msg, true)
#define REQUIRE_NULL(actual, msg)                       UTCHECK_NULL(actual, msg, true)
#define REQUIRE_NONNULL(actual, msg)                    UTCHECK_NONNULL(actual, msg, true)

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
bool run_all_tests(void);

/*
 * Returns false if expected does or does not equal actual (based on expect_eq).
 * Will print msg and a hexdump8 of the input buffers if the check fails.
 */
bool unittest_expect_bytes(const uint8_t* expected,
                           const char* expected_name,
                           const uint8_t* actual,
                           const char* actual_name,
                           size_t len,
                           const char *msg,
                           const char* func,
                           int line,
                           bool expect_eq);

typedef bool     (*unitest_fn_t)(void* context);
typedef status_t (*unitest_testcase_init_fn_t)(void** context);
typedef void     (*unitest_testcase_cleanup_fn_t)(void* context);

typedef struct unitest_registration {
    const char*  name;
    unitest_fn_t fn;
} unittest_registration_t;

typedef struct unitest_testcase_registration {
    const char*                     name;
    const char*                     desc;
    unitest_testcase_init_fn_t      init;
    unitest_testcase_cleanup_fn_t   cleanup;
    const unittest_registration_t*  tests;
    size_t                          test_cnt;
} unittest_testcase_registration_t;

#ifdef WITH_LIB_UNITTEST
#define UNITTEST_START_TESTCASE(_global_id)  \
    static const unittest_registration_t __unittest_table_##_global_id[] = {

#define UNITTEST(_name, _fn) \
    { .name = _name, .fn = _fn },

#define UNITTEST_END_TESTCASE(_global_id, _name, _desc, _init, _cleanup)           \
    };  /* __unittest_table_##_global_id */                                        \
    extern const unittest_testcase_registration_t __unittest_case_##_global_id;    \
    const unittest_testcase_registration_t __unittest_case_##_global_id            \
    __ALIGNED(sizeof(void *)) __SECTION("unittest_testcases") =                    \
    {                                                                              \
        .name = _name,                                                             \
        .desc = _desc,                                                             \
        .init = _init,                                                             \
        .cleanup = _cleanup,                                                       \
        .tests =  __unittest_table_##_global_id,                                   \
        .test_cnt =  countof(__unittest_table_##_global_id),                       \
    }
#else   // WITH_LIB_UNITTEST
#define UNITTEST_START_TESTCASE(_global_id)
#define UNITTEST(_name, _fn)
#define UNITTEST_END_TESTCASE(_global_id, _name, _desc, _init, _cleanup)
#endif  // WITH_LIB_UNITTEST

__END_CDECLS

#endif /* _LIB_UNITTEST_INCLUDE_UNITTEST_H_ */
