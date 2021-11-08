// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_C_ZXTEST_H_
#define ZXTEST_C_ZXTEST_H_

#ifndef ZXTEST_INCLUDE_INTERNAL_HEADERS
#error This header is not intended for direct inclusion. Include zxtest/zxtest.h instead.
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>

#ifdef __Fuchsia__
#include <zircon/status.h>
#endif

__BEGIN_CDECLS

// Defines acceptable types for functions.
typedef void (*zxtest_test_fn_t)(void);

// C equivalent of zxtest::TestRef
typedef struct zxtest_test_ref {
  size_t test_index;
  size_t test_case_index;
} zxtest_test_ref_t;

// C test registering function.
zxtest_test_ref_t zxtest_runner_register_test(const char* testcase_name, const char* test_name,
                                              const char* file, int line_number,
                                              zxtest_test_fn_t test_fn);

// C test assertion function. Since we have no function overloading, the types of variables that we
// can accept on each macro will be more restricted.
void zxtest_runner_notify_assertion(const char* desc, const char* expected,
                                    const char* expected_var, const char* actual,
                                    const char* actual_var, const char* file, int64_t line,
                                    bool is_fatal);

// Returns true if zxtest is currently executing tests.
bool zxtest_runner_is_running(void);

// When an assertion happens out of the main test body, this allows keeping track of whether the
// current flow should abort.
bool zxtest_runner_current_test_has_fatal_failures(void);

// Returns true when the current test is skipped.
bool zxtest_runner_current_test_is_skipped(void);

// Returns true when the current test has registered any kind of failure.
bool zxtest_runner_current_test_has_failures(void);

// Fails the current running test.
void zxtest_runner_fail_current_test(bool fatal, const char* file, int line_number,
                                     const char* message);

// Skips the current running test.
void zxtest_runner_skip_current_test(const char* file, int line, const char* message);

// Declare an opaque type.
typedef struct zxtest_scoped_trace_t zxtest_scoped_trace_t;

// Use opaque type ptr on the header signatures.
zxtest_scoped_trace_t* zxtest_runner_push_trace(const char* message, const char* filename,
                                                uint64_t line);

void zxtest_runner_pop_trace(zxtest_scoped_trace_t** ptr);

#ifdef __Fuchsia__
// Possible expected results for the death statements.
enum DeathResult {
  kDeathResultRaiseException,
  kDeathResultComplete,
};

// Returns true if |statement| execution results in |result|.
bool zxtest_death_statement_execute(zxtest_test_fn_t statement, enum DeathResult result,
                                    const char* file, int line, const char* message);
#endif

// Entry point for executing all tests.
int zxtest_run_all_tests(int argc, char** argv);

// Internal for generating human readable output in C.
size_t _zxtest_print_int(int val, char* buffer, size_t buffer_size);

size_t _zxtest_print_unsigned_int(unsigned int val, char* buffer, size_t buffer_size);

size_t _zxtest_print_long_long(long long val, char* buffer, size_t buffer_size);

size_t _zxtest_print_unsigned_long_long(unsigned long long val, char* buffer, size_t buffer_size);

size_t _zxtest_print_double(double val, char* buffer, size_t buffer_size);

size_t _zxtest_print_long_double(long double val, char* buffer, size_t buffer_size);

size_t _zxtest_print_bool(bool val, char* buffer, size_t buffer_size);

size_t _zxtest_print_str(const char* val, char* buffer, size_t buffer_size);

size_t _zxtest_print_ptr(const void* val, char* buffer, size_t buffer_size);

size_t _zxtest_print_hex(const void* val, size_t size, char* buffer, size_t buffer_size);

void zxtest_c_clean_buffer(char** buffer);

__END_CDECLS

#define LIB_ZXTEST_EQ(actual, expected) actual == expected
#define LIB_ZXTEST_NE(actual, expected) actual != expected
#define LIB_ZXTEST_BOOL(actual, expected) (bool)actual == expected
#define LIB_ZXTEST_LT(actual, expected) actual < expected
#define LIB_ZXTEST_LE(actual, expected) actual <= expected
#define LIB_ZXTEST_GT(actual, expected) actual > expected
#define LIB_ZXTEST_GE(actual, expected) actual >= expected
#define LIB_ZXTEST_STREQ(actual, expected) \
  ((actual == NULL && expected == NULL) || \
   ((!actual == !expected) && strcmp(actual, expected) == 0))
#define LIB_ZXTEST_STRNE(actual, expected) !LIB_ZXTEST_STREQ(actual, expected)
#define LIB_ZXTEST_SUBSTR(str, substr) (strstr(str, substr) != NULL)
#define LIB_ZXTEST_BYTEEQ(actual, expected, size) memcmp(actual, expected, size) == 0
#define LIB_ZXTEST_BYTENE(actual, expected, size) memcmp(actual, expected, size) != 0

#define LIB_ZXTEST_CONCAT_TOKEN(foo, bar) LIB_ZXTEST_CONCAT_TOKEN_IMPL(foo, bar)
#define LIB_ZXTEST_CONCAT_TOKEN_IMPL(foo, bar) foo##bar

// C specific macros for registering tests.
#define RUN_ALL_TESTS(argc, argv) zxtest_run_all_tests(argc, argv)

#define LIB_ZXTEST_TEST_REF(TestCase, Test) TestCase##_##Test##_ref

#define LIB_ZXTEST_TEST_FN(TestCase, Test) TestCase##_##Test##_fn

#define LIB_ZXTEST_REGISTER_FN(TestCase, Test) TestCase##_##Test##_register_fn

#define LIB_ZXTEST_REGISTER(TestCase, Test)                                              \
  static zxtest_test_ref_t LIB_ZXTEST_TEST_REF(TestCase, Test) = {.test_index = 0,       \
                                                                  .test_case_index = 0}; \
  static void LIB_ZXTEST_TEST_FN(TestCase, Test)(void);                                  \
  static void LIB_ZXTEST_REGISTER_FN(TestCase, Test)(void) __attribute__((constructor)); \
  void LIB_ZXTEST_REGISTER_FN(TestCase, Test)(void) {                                    \
    LIB_ZXTEST_TEST_REF(TestCase, Test) = zxtest_runner_register_test(                   \
        #TestCase, #Test, __FILE__, __LINE__, &LIB_ZXTEST_TEST_FN(TestCase, Test));      \
  }                                                                                      \
  void LIB_ZXTEST_TEST_FN(TestCase, Test)(void)

// Register a test as part of a TestCase.
//
// The extra level of indirection here (i.e. having TEST invoke
// _ZXTEST_REGISTER) is a C/C++ preprocessor hack that causes the tokens
// TestCase and Test to be macro-expanded when taking the strings #TestCase
// and #Test.
#define TEST(TestCase, Test) LIB_ZXTEST_REGISTER(TestCase, Test)

#define LIB_ZXTEST_CHECK_RUNNING()                                            \
  do {                                                                        \
    ZX_ASSERT_MSG(zxtest_runner_is_running(), "See Context check in README"); \
  } while (0)

// Helper function to print variables.
#define LIB_ZXTEST_SPRINT_PRINTER(var, buffer, size) \
  _Generic((var),                                                     \
             char: _zxtest_print_int,                                   \
             signed char: _zxtest_print_int,                            \
             short: _zxtest_print_int,                                  \
             int: _zxtest_print_int,                                    \
             long: _zxtest_print_long_long,                             \
             long long: _zxtest_print_long_long,                        \
             unsigned char: _zxtest_print_unsigned_int,                 \
             unsigned short: _zxtest_print_unsigned_int,                \
             unsigned int: _zxtest_print_unsigned_int,                  \
             unsigned long: _zxtest_print_unsigned_long_long,           \
             unsigned long long: _zxtest_print_unsigned_long_long,      \
             float: _zxtest_print_double,                               \
             double: _zxtest_print_double,                              \
             long double: _zxtest_print_long_double,                    \
             bool: _zxtest_print_bool,                                  \
             const char*: _zxtest_print_str,                            \
             default: _zxtest_print_ptr) \
  (var, buffer, size)

#define LIB_ZXTEST_NULLPTR NULL

#define LIB_ZXTEST_HEX_PRINTER(var, var_size, buffer, size) \
  _zxtest_print_hex((const void*)var, var_size, buffer, size)

#define LIB_ZXTEST_PRINT_BUFFER_NAME(var, type, line)                                \
  char str_placeholder_##type##_##line = '\0';                                       \
  size_t buff_size_##type##_##line =                                                 \
      LIB_ZXTEST_SPRINT_PRINTER(var, &str_placeholder_##type##_##line, 1) + 1;       \
  char* str_buffer_##type##_##line __attribute__((cleanup(zxtest_c_clean_buffer))) = \
      (char*)malloc(buff_size_##type##_##line * sizeof(char));                       \
  memset(str_buffer_##type##_##line, '\0', buff_size_##type##_##line);               \
  LIB_ZXTEST_SPRINT_PRINTER(var, str_buffer_##type##_##line, buff_size_##type##_##line)

#define LIB_ZXTEST_PRINT_BUFFER_NAME_HEX(var, var_size, type, line)                   \
  char str_placeholder_##type##_##line = '\0';                                        \
  size_t buff_size_##type##_##line =                                                  \
      LIB_ZXTEST_HEX_PRINTER(var, var_size, &str_placeholder_##type##_##line, 1) + 1; \
  char* str_buffer_##type##_##line __attribute__((cleanup(zxtest_c_clean_buffer))) =  \
      (char*)malloc(buff_size_##type##_##line * sizeof(char));                        \
  memset(str_buffer_##type##_##line, '\0', buff_size_##type##_##line);                \
  LIB_ZXTEST_HEX_PRINTER(var, var_size, str_buffer_##type##_##line, buff_size_##type##_##line)

#define LIB_ZXTEST_LOAD_PRINT_VAR(var, type, line) LIB_ZXTEST_PRINT_BUFFER_NAME(var, type, line)

#define LIB_ZXTEST_LOAD_PRINT_HEX(var, var_size, type, line) \
  LIB_ZXTEST_PRINT_BUFFER_NAME_HEX(var, var_size, type, line)

#define LIB_ZXTEST_GET_PRINT_VAR(var, type, line) str_buffer_##type##_##line

// Provides an alias for assertion mechanisms.
#define LIB_ZXTEST_ASSERT(desc, expected, expected_var, actual, actual_var, file, line, is_fatal) \
  zxtest_runner_notify_assertion(desc, expected, expected_var, actual, actual_var, file, line,    \
                                 is_fatal)

#define LIB_ZXTEST_IS_SKIPPED zxtest_runner_current_test_is_skipped()
#define LIB_ZXTEST_TEST_HAS_ERRORS zxtest_runner_current_test_has_failures()
#define LIB_ZXTEST_ABORT_IF_ERROR zxtest_runner_current_test_has_fatal_failures()
#define LIB_ZXTEST_STRCMP(actual, expected) (strcmp(actual, expected) == 0)

#define LIB_ZXTEST_AUTO_VAR_TYPE(var) __typeof__(var)

// Basic macros for assertions.

// Used to cleanup allocated buffers for formatted messages.
// Marked unused to prevent compiler warnings.
static void zxtest_clean_buffer(char** buffer) __attribute__((unused));
static void zxtest_clean_buffer(char** buffer) { free(*buffer); }

#define LIB_ZXTEST_GEN_ASSERT_DESC(out_desc, desc, ...)                     \
  char* out_desc __attribute__((cleanup(zxtest_c_clean_buffer))) = NULL;    \
  do {                                                                      \
    char tmp;                                                               \
    size_t req_size = snprintf(&tmp, 1, " " __VA_ARGS__) + 1;               \
    out_desc = (char*)malloc(sizeof(char) * (req_size + strlen(desc) + 2)); \
    memset(out_desc, '\0', sizeof(char) * (req_size + strlen(desc) + 2));   \
    memcpy(out_desc, desc, strlen(desc));                                   \
    out_desc[strlen(desc)] = ' ';                                           \
    snprintf(out_desc + strlen(desc) + 1, req_size, " "__VA_ARGS__);        \
  } while (0)

#define LIB_ZXTEST_RETURN_IF_FATAL_1 \
  do {                               \
    unittest_fails();                \
    if (LIB_ZXTEST_ABORT_IF_ERROR) { \
      return;                        \
    }                                \
  } while (0)

#define LIB_ZXTEST_RETURN_IF_FATAL_0 \
  do {                               \
    unittest_fails();                \
  } while (0)

#define LIB_ZXTEST_RETURN_IF_FATAL(fatal) LIB_ZXTEST_RETURN_IF_FATAL_##fatal

#define LIB_ZXTEST_CHECK_VAR_BYTES(op, expected, actual, size, fatal, file, line, desc, ...)       \
  do {                                                                                             \
    LIB_ZXTEST_CHECK_RUNNING();                                                                    \
    const void* _actual = (const void*)(actual);                                                   \
    const void* _expected = (const void*)(expected);                                               \
    if (!(op(_actual, _expected, size))) {                                                         \
      LIB_ZXTEST_GEN_ASSERT_DESC(msg_buffer, desc, ##__VA_ARGS__);                                 \
      LIB_ZXTEST_LOAD_PRINT_HEX(_actual, size, act, line);                                         \
      LIB_ZXTEST_LOAD_PRINT_HEX(_expected, size, exptd, line);                                     \
      LIB_ZXTEST_ASSERT(msg_buffer, #expected, LIB_ZXTEST_GET_PRINT_VAR(_expected, exptd, line),   \
                        #actual, LIB_ZXTEST_GET_PRINT_VAR(_actual, act, line), file, line, fatal); \
      LIB_ZXTEST_RETURN_IF_FATAL(fatal);                                                           \
    }                                                                                              \
  } while (0)

#define LIB_ZXTEST_CHECK_VAR_COERCE(op, expected, actual, type, fatal, file, line, desc, ...)      \
  do {                                                                                             \
    LIB_ZXTEST_CHECK_RUNNING();                                                                    \
    const type _actual = (const type)(actual);                                                     \
    const type _expected = (const type)(expected);                                                 \
    if (!(op(_actual, _expected))) {                                                               \
      LIB_ZXTEST_GEN_ASSERT_DESC(msg_buffer, desc, ##__VA_ARGS__);                                 \
      LIB_ZXTEST_LOAD_PRINT_VAR(_actual, act, line);                                               \
      LIB_ZXTEST_LOAD_PRINT_VAR(_expected, exptd, line);                                           \
      LIB_ZXTEST_ASSERT(msg_buffer, #expected, LIB_ZXTEST_GET_PRINT_VAR(_expected, exptd, line),   \
                        #actual, LIB_ZXTEST_GET_PRINT_VAR(_actual, act, line), file, line, fatal); \
      LIB_ZXTEST_RETURN_IF_FATAL(fatal);                                                           \
    }                                                                                              \
  } while (0)

#define LIB_ZXTEST_CHECK_VAR(op, expected, actual, fatal, file, line, desc, ...)               \
  LIB_ZXTEST_CHECK_VAR_COERCE(op, expected, actual, LIB_ZXTEST_AUTO_VAR_TYPE(expected), fatal, \
                              file, line, desc, ##__VA_ARGS__)

#define LIB_ZXTEST_FAIL_NO_RETURN(fatal, desc, ...)                         \
  do {                                                                      \
    LIB_ZXTEST_CHECK_RUNNING();                                             \
    LIB_ZXTEST_GEN_ASSERT_DESC(msg_buffer, desc, ##__VA_ARGS__);            \
    zxtest_runner_fail_current_test(fatal, __FILE__, __LINE__, msg_buffer); \
  } while (0)

#define LIB_ZXTEST_ASSERT_ERROR(has_errors, fatal, desc, ...) \
  do {                                                        \
    if (has_errors) {                                         \
      LIB_ZXTEST_FAIL_NO_RETURN(fatal, desc, ##__VA_ARGS__);  \
      LIB_ZXTEST_RETURN_IF_FATAL(fatal);                      \
    }                                                         \
  } while (0)

#define FAIL(...)                                       \
  do {                                                  \
    LIB_ZXTEST_CHECK_RUNNING();                         \
    LIB_ZXTEST_FAIL_NO_RETURN(true, "", ##__VA_ARGS__); \
    return;                                             \
  } while (0)

#define ZXTEST_SKIP(desc, ...)                                       \
  do {                                                               \
    LIB_ZXTEST_CHECK_RUNNING();                                      \
    LIB_ZXTEST_GEN_ASSERT_DESC(msg_buffer, desc, ##__VA_ARGS__);     \
    zxtest_runner_skip_current_test(__FILE__, __LINE__, msg_buffer); \
    return;                                                          \
  } while (0)

#ifdef __Fuchsia__
#define LIB_ZXTEST_CHECK_VAR_STATUS(op, expected, actual, fatal, file, line, desc, ...)  \
  do {                                                                                   \
    LIB_ZXTEST_CHECK_RUNNING();                                                          \
    const zx_status_t _actual = (const zx_status_t)(actual);                             \
    const zx_status_t _expected = (const zx_status_t)(expected);                         \
    if (!(op(_actual, _expected))) {                                                     \
      LIB_ZXTEST_GEN_ASSERT_DESC(msg_buffer, desc, ##__VA_ARGS__);                       \
      LIB_ZXTEST_ASSERT(msg_buffer, #expected, zx_status_get_string(_expected), #actual, \
                        zx_status_get_string(_actual), file, line, fatal);               \
      LIB_ZXTEST_RETURN_IF_FATAL(fatal);                                                 \
    }                                                                                    \
  } while (0)

#define LIB_ZXTEST_DEATH_STATUS_COMPLETE kDeathResultComplete
#define LIB_ZXTEST_DEATH_STATUS_EXCEPTION kDeathResultRaiseException
#define LIB_ZXTEST_DEATH_STATEMENT(statement, expected_result, desc, ...)               \
  do {                                                                                  \
    LIB_ZXTEST_CHECK_RUNNING();                                                         \
    LIB_ZXTEST_GEN_ASSERT_DESC(msg_buffer, desc, ##__VA_ARGS__);                        \
    if (!zxtest_death_statement_execute(statement, expected_result, __FILE__, __LINE__, \
                                        msg_buffer)) {                                  \
      return;                                                                           \
    }                                                                                   \
  } while (0)

#else
#define LIB_ZXTEST_CHECK_VAR_STATUS(...) LIB_ZXTEST_CHECK_VAR(__VA_ARGS__)
#endif

#define SCOPED_TRACE(message)                                             \
  zxtest_scoped_trace_t* LIB_ZXTEST_CONCAT_TOKEN(zxtest_trace_, __LINE__) \
      __attribute__((unused, cleanup(zxtest_runner_pop_trace))) =         \
          zxtest_runner_push_trace(message, __FILE__, __LINE__)

#endif  // ZXTEST_C_ZXTEST_H_
