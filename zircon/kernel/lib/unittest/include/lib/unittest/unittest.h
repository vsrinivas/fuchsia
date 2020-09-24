// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_UNITTEST_INCLUDE_LIB_UNITTEST_UNITTEST_H_
#define ZIRCON_KERNEL_LIB_UNITTEST_INCLUDE_LIB_UNITTEST_UNITTEST_H_
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
 *                        "Test to be sure that your foos have proper bars");
 *
 * This creates an entry in the global unittest table and registers it with the
 * unit test framework.
 *
 * A test looks like this, using the BEGIN_TEST and END_TEST macros at
 * the beginning and end of the test and the EXPECT_* macros to
 * validate test results, as shown:
 *
 * static bool test_foo()
 * {
 *      BEGIN_TEST;
 *
 *      ...declare variables and do stuff...
 *      int foo_value = foo_func();
 *      ...See if the stuff produced the correct value...
 *      EXPECT_EQ(1, foo_value, "foo_func failed");
 *      ... there are EXPECT_* macros for many conditions...
 *      EXPECT_TRUE(foo_condition(), "condition should be true");
 *      EXPECT_NE(ZX_ERR_TIMED_OUT, foo_event(), "event timed out");
 *      ... and the "message" parameter is optional...
 *      EXPECT_NONNULL(get_data());
 *
 *      END_TEST;
 * }
 *
 * To your rules.mk file, add lib/unittest to MODULE_DEPS:
 *
 * MODULE_DEPS += \
 *         lib/unittest   \
 */
#include <lib/special-sections/special-sections.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <ktl/iterator.h>

// This function will help terminate the static analyzer when it reaches
// an assertion failure site. The bugs discovered by the static analyzer will
// be suppressed as they are expected by the test cases.
__ANALYZER_CREATE_SINK
static inline void unittest_fails(void) {}

/*
 * Printf dedicated to the unittest library
 * the default output is the printf
 */
int unittest_printf(const char* format, ...) __PRINTFLIKE(1, 2);

/*
 * Macros to format the error string
 */
#define EXPECTED_STRING "%s:\n        expected "
#define UNITTEST_FAIL_TRACEF_FORMAT "\n        [FAILED]\n        %s:%d:\n        "
#define UNITTEST_FAIL_TRACEF(str, x...)                                                   \
  do {                                                                                    \
    unittest_printf(UNITTEST_FAIL_TRACEF_FORMAT str, __PRETTY_FUNCTION__, __LINE__, ##x); \
  } while (0)

/*
 * BEGIN_TEST and END_TEST go in a function that is called by RUN_TEST
 * and that call the EXPECT_ macros.
 */

#define BEGIN_TEST bool all_ok = true
#define END_TEST return all_ok

#define AUTO_TYPE_VAR(type) auto&

// The following helper function makes the "msg" argument optional so that you can write either:
//   ASSERT_EQ(x, y, "Check that x equals y");
// or
//   ASSERT_EQ(x, y);
static inline constexpr const char* unittest_get_msg(const char* msg = "") { return msg; }

/*
 * UTCHECK_* macros are used to check test results.  Generally, one should
 * prefer to use either the EXPECT_* (non-terminating) or ASSERT_*
 * (terminating) forms of the macros.  See below.
 *
 * The parameter after |term| is an optional message (const char*) to be printed
 * if the check fails.
 */
#define UTCHECK_EQ(expected, actual, term, ...)                                                    \
  do {                                                                                             \
    const AUTO_TYPE_VAR(expected) _e = (expected);                                                 \
    const AUTO_TYPE_VAR(actual) _a = (actual);                                                     \
    if (_e != _a) {                                                                                \
      UNITTEST_FAIL_TRACEF(EXPECTED_STRING                                                         \
                           "%s (%ld), "                                                            \
                           "actual %s (%ld)\n",                                                    \
                           unittest_get_msg(__VA_ARGS__), #expected, (long)_e, #actual, (long)_a); \
      unittest_fails();                                                                            \
      if (term) {                                                                                  \
        return false;                                                                              \
      }                                                                                            \
      all_ok = false;                                                                              \
    }                                                                                              \
  } while (0)

#define UTCHECK_NE(expected, actual, term, ...)                                                    \
  do {                                                                                             \
    const AUTO_TYPE_VAR(expected) _e = (expected);                                                 \
    const AUTO_TYPE_VAR(actual) _a = (actual);                                                     \
    if (_e == (_a)) {                                                                              \
      UNITTEST_FAIL_TRACEF(EXPECTED_STRING                                                         \
                           "%s (%ld), %s"                                                          \
                           " to differ, but they are the same %ld\n",                              \
                           unittest_get_msg(__VA_ARGS__), #expected, (long)_e, #actual, (long)_a); \
      unittest_fails();                                                                            \
      if (term) {                                                                                  \
        return false;                                                                              \
      }                                                                                            \
      all_ok = false;                                                                              \
    }                                                                                              \
  } while (0)

#define UTCHECK_LE(expected, actual, term, ...)                                                    \
  do {                                                                                             \
    const AUTO_TYPE_VAR(expected) _e = (expected);                                                 \
    const AUTO_TYPE_VAR(actual) _a = (actual);                                                     \
    if (_e > _a) {                                                                                 \
      UNITTEST_FAIL_TRACEF(EXPECTED_STRING                                                         \
                           "%s (%ld) to be"                                                        \
                           " less-than-or-equal-to actual %s (%ld)\n",                             \
                           unittest_get_msg(__VA_ARGS__), #expected, (long)_e, #actual, (long)_a); \
      unittest_fails();                                                                            \
      if (term) {                                                                                  \
        return false;                                                                              \
      }                                                                                            \
      all_ok = false;                                                                              \
    }                                                                                              \
  } while (0)

#define UTCHECK_LT(expected, actual, term, ...)                                                    \
  do {                                                                                             \
    const AUTO_TYPE_VAR(expected) _e = (expected);                                                 \
    const AUTO_TYPE_VAR(actual) _a = (actual);                                                     \
    if (_e >= _a) {                                                                                \
      UNITTEST_FAIL_TRACEF(EXPECTED_STRING                                                         \
                           "%s (%ld) to be"                                                        \
                           " less-than actual %s (%ld)\n",                                         \
                           unittest_get_msg(__VA_ARGS__), #expected, (long)_e, #actual, (long)_a); \
      unittest_fails();                                                                            \
      if (term) {                                                                                  \
        return false;                                                                              \
      }                                                                                            \
      all_ok = false;                                                                              \
    }                                                                                              \
  } while (0)

#define UTCHECK_GE(expected, actual, term, ...)                                                    \
  do {                                                                                             \
    const AUTO_TYPE_VAR(expected) _e = (expected);                                                 \
    const AUTO_TYPE_VAR(actual) _a = (actual);                                                     \
    if (_e < _a) {                                                                                 \
      UNITTEST_FAIL_TRACEF(EXPECTED_STRING                                                         \
                           "%s (%ld) to be"                                                        \
                           " greater-than-or-equal-to actual %s (%ld)\n",                          \
                           unittest_get_msg(__VA_ARGS__), #expected, (long)_e, #actual, (long)_a); \
      unittest_fails();                                                                            \
      if (term) {                                                                                  \
        return false;                                                                              \
      }                                                                                            \
      all_ok = false;                                                                              \
    }                                                                                              \
  } while (0)

#define UTCHECK_GT(expected, actual, term, ...)                                                    \
  do {                                                                                             \
    const AUTO_TYPE_VAR(expected) _e = (expected);                                                 \
    const AUTO_TYPE_VAR(actual) _a = (actual);                                                     \
    if (_e <= _a) {                                                                                \
      UNITTEST_FAIL_TRACEF(EXPECTED_STRING                                                         \
                           "%s (%ld) to be"                                                        \
                           " greater-than actual %s (%ld)\n",                                      \
                           unittest_get_msg(__VA_ARGS__), #expected, (long)_e, #actual, (long)_a); \
      unittest_fails();                                                                            \
      if (term) {                                                                                  \
        return false;                                                                              \
      }                                                                                            \
      all_ok = false;                                                                              \
    }                                                                                              \
  } while (0)

#define UTCHECK_TRUE(actual, term, ...)                                                \
  if (!(actual)) {                                                                     \
    UNITTEST_FAIL_TRACEF("%s: %s is false\n", unittest_get_msg(__VA_ARGS__), #actual); \
    unittest_fails();                                                                  \
    if (term) {                                                                        \
      return false;                                                                    \
    }                                                                                  \
    all_ok = false;                                                                    \
  }

#define UTCHECK_FALSE(actual, term, ...)                                              \
  if (actual) {                                                                       \
    UNITTEST_FAIL_TRACEF("%s: %s is true\n", unittest_get_msg(__VA_ARGS__), #actual); \
    unittest_fails();                                                                 \
    if (term) {                                                                       \
      return false;                                                                   \
    }                                                                                 \
    all_ok = false;                                                                   \
  }

#define UTCHECK_NULL(actual, term, ...)                                                    \
  if (actual != NULL) {                                                                    \
    UNITTEST_FAIL_TRACEF("%s: %s is non-null!\n", unittest_get_msg(__VA_ARGS__), #actual); \
    unittest_fails();                                                                      \
    if (term) {                                                                            \
      return false;                                                                        \
    }                                                                                      \
    all_ok = false;                                                                        \
  }

#define UTCHECK_NONNULL(actual, term, ...)                                             \
  if (actual == NULL) {                                                                \
    UNITTEST_FAIL_TRACEF("%s: %s is null!\n", unittest_get_msg(__VA_ARGS__), #actual); \
    unittest_fails();                                                                  \
    if (term) {                                                                        \
      return false;                                                                    \
    }                                                                                  \
    all_ok = false;                                                                    \
  }

#define UTCHECK_BYTES_EQ(expected, actual, length, term, ...)                              \
  if (!unittest_expect_bytes((expected), #expected, (actual), #actual, (length),           \
                             unittest_get_msg(__VA_ARGS__), __PRETTY_FUNCTION__, __LINE__, \
                             true)) {                                                      \
    unittest_fails();                                                                      \
    if (term) {                                                                            \
      return false;                                                                        \
    }                                                                                      \
    all_ok = false;                                                                        \
  }

#define UTCHECK_BYTES_NE(expected, actual, length, term, ...)                              \
  if (!unittest_expect_bytes((expected), #expected, (actual), #actual, (length),           \
                             unittest_get_msg(__VA_ARGS__), __PRETTY_FUNCTION__, __LINE__, \
                             false)) {                                                     \
    unittest_fails();                                                                      \
    if (term) {                                                                            \
      return false;                                                                        \
    }                                                                                      \
    all_ok = false;                                                                        \
  }

/* EXPECT_* macros check the supplied condition and will print a diagnostic
 * message and flag the test as having failed if the condition fails.  The test
 * will continue to run, even if the condition fails.
 *
 * The last parameter is an optional const char* message to be included in the
 * print diagnostic message.
 */
#define EXPECT_EQ(expected, actual, ...) UTCHECK_EQ(expected, actual, false, __VA_ARGS__)
#define EXPECT_NE(expected, actual, ...) UTCHECK_NE(expected, actual, false, __VA_ARGS__)
#define EXPECT_LE(expected, actual, ...) UTCHECK_LE(expected, actual, false, __VA_ARGS__)
#define EXPECT_LT(expected, actual, ...) UTCHECK_LT(expected, actual, false, __VA_ARGS__)
#define EXPECT_GE(expected, actual, ...) UTCHECK_GE(expected, actual, false, __VA_ARGS__)
#define EXPECT_GT(expected, actual, ...) UTCHECK_GT(expected, actual, false, __VA_ARGS__)
#define EXPECT_TRUE(actual, ...) UTCHECK_TRUE(actual, false, __VA_ARGS__)
#define EXPECT_FALSE(actual, ...) UTCHECK_FALSE(actual, false, __VA_ARGS__)
#define EXPECT_BYTES_EQ(expected, actual, length, ...) \
  UTCHECK_BYTES_EQ(expected, actual, length, false, __VA_ARGS__)
#define EXPECT_BYTES_NE(bytes1, bytes2, length, ...) \
  UTCHECK_BYTES_NE(bytes1, bytes2, length, false, __VA_ARGS__)
#define EXPECT_EQ_LL(expected, actual, ...) UTCHECK_EQ_LL(expected, actual, false, __VA_ARGS__)
#define EXPECT_NULL(actual, ...) UTCHECK_NULL(actual, false, __VA_ARGS__)
#define EXPECT_NONNULL(actual, ...) UTCHECK_NONNULL(actual, false, __VA_ARGS__)

/* ASSERT_* macros check the condition and will print a message and immediately
 * abort a test with a filure status if the condition fails.
 */
#define ASSERT_EQ(expected, actual, ...) UTCHECK_EQ(expected, actual, true, __VA_ARGS__)
#define ASSERT_NE(expected, actual, ...) UTCHECK_NE(expected, actual, true, __VA_ARGS__)
#define ASSERT_LE(expected, actual, ...) UTCHECK_LE(expected, actual, true, __VA_ARGS__)
#define ASSERT_LT(expected, actual, ...) UTCHECK_LT(expected, actual, true, __VA_ARGS__)
#define ASSERT_GE(expected, actual, ...) UTCHECK_GE(expected, actual, true, __VA_ARGS__)
#define ASSERT_GT(expected, actual, ...) UTCHECK_GT(expected, actual, true, __VA_ARGS__)
#define ASSERT_TRUE(actual, ...) UTCHECK_TRUE(actual, true, __VA_ARGS__)
#define ASSERT_FALSE(actual, ...) UTCHECK_FALSE(actual, true, __VA_ARGS__)
#define ASSERT_BYTES_EQ(expected, actual, length, ...) \
  UTCHECK_BYTES_EQ(expected, actual, length, true, __VA_ARGS__)
#define ASSERT_BYTES_NE(bytes1, bytes2, length, ...) \
  UTCHECK_BYTES_NE(bytes1, bytes2, length, true, __VA_ARGS__)
#define ASSERT_EQ_LL(expected, actual, ...) UTCHECK_EQ_LL(expected, actual, true, __VA_ARGS__)
#define ASSERT_NULL(actual, ...) UTCHECK_NULL(actual, true, __VA_ARGS__)
#define ASSERT_NONNULL(actual, ...) UTCHECK_NONNULL(actual, true, __VA_ARGS__)

/*
 * Returns false if expected does or does not equal actual (based on expect_eq).
 * Will print msg and a hexdump8 of the input buffers if the check fails.
 */
bool unittest_expect_bytes(const uint8_t* expected, const char* expected_name,
                           const uint8_t* actual, const char* actual_name, size_t len,
                           const char* msg, const char* func, int line, bool expect_eq);

typedef bool (*unittest_fn_t)(void);

#ifndef _KERNEL
// In phys executables rather than the kernel proper, there is no
// infrastructure code for collecting the tests.  Each suite is just
// a function that has to be called explicitly.

struct test_case_element {
  const char* name;
  unittest_fn_t fn;
};

bool unittest_testcase(const char* name, const test_case_element*, size_t n);

#define UNITTEST_START_TESTCASE(global_id) \
  bool global_id() {                       \
    const test_case_element cases[] = {
// The assembly silliness is to prevent the compiler from deciding it
// can move the whole array into a static initializer with relocs.
#define UNITTEST(name, fn)                                    \
  []() -> test_case_element {                                 \
    const char* _n;                                           \
    unittest_fn_t _f;                                         \
    __asm__("nop" : "=g"(_n), "=g"(_f) : "0"(name), "1"(fn)); \
    return {_n, _f};                                          \
  }(),
#define UNITTEST_END_TESTCASE(global_id, name, desc)       \
  }                                                        \
  ;                                                        \
  return unittest_testcase(name, cases, ktl::size(cases)); \
  }

#else  // _KERNEL

/*
 * The list of test cases is made up of these elements.
 */
struct test_case_element {
  struct test_case_element* next;
  struct test_case_element* failed_next;
  const char* name;
  unittest_fn_t test_case;
};

/*
 * Registers a test case with the unit test framework.
 */
void unittest_register_test_case(struct test_case_element* elem);

/*
 * Runs all registered test cases.
 */
bool run_all_tests(void);

typedef struct unitest_registration {
  const char* name;
  unittest_fn_t fn;
} unittest_registration_t;

typedef struct unitest_testcase_registration {
  const char* name;
  const char* desc;
  const unittest_registration_t* tests;
  size_t test_cnt;
} unittest_testcase_registration_t;

#if LK_DEBUGLEVEL == 0

#define UNITTEST_START_TESTCASE(_global_id) \
  [[maybe_unused]] static void __unittest_table_##_global_id() {
#define UNITTEST(_name, _fn) (void)(_fn);
#define UNITTEST_END_TESTCASE(_global_id, _name, _desc) }

#else  // LK_DEBUGLEVEL != 0

#define UNITTEST_START_TESTCASE(_global_id) \
  static const unittest_registration_t __unittest_table_##_global_id[] = {
#define UNITTEST(_name, _fn) {.name = _name, .fn = _fn},

#define UNITTEST_END_TESTCASE(_global_id, _name, _desc)                                       \
  }                                                                                           \
  ; /* __unittest_table_##_global_id */                                                       \
  static const unittest_testcase_registration_t __unittest_case_##_global_id SPECIAL_SECTION( \
      ".data.rel.ro.unittest_testcases", unittest_testcase_registration_t) = {                \
      .name = _name,                                                                          \
      .desc = _desc,                                                                          \
      .tests = __unittest_table_##_global_id,                                                 \
      .test_cnt = countof(__unittest_table_##_global_id),                                     \
  };

#endif  // LK_DEBUGLEVEL == 0

#endif  // !_KERNEL

#endif  // ZIRCON_KERNEL_LIB_UNITTEST_INCLUDE_LIB_UNITTEST_UNITTEST_H_
