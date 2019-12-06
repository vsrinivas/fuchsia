// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_C_ZXTEST_H_
#define ZXTEST_C_ZXTEST_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// When an assertion happens out of the main test body, this allows keeping track of whether the
// current flow should abort.
bool zxtest_runner_current_test_has_fatal_failures(void);

// Returns true when the current test has registered any kind of failure.
bool zxtest_runner_current_test_has_failures(void);

// Fails the current running test.
void zxtest_runner_fail_current_test(bool fatal, const char* file, int line_number,
                                     const char* message);

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
size_t _zxtest_print_int32(int32_t val, char* buffer, size_t buffer_size);

size_t _zxtest_print_uint32(uint32_t val, char* buffer, size_t buffer_size);

size_t _zxtest_print_int64(int64_t val, char* buffer, size_t buffer_size);

size_t _zxtest_print_uint64(uint64_t val, char* buffer, size_t buffer_size);

size_t _zxtest_print_bool(bool val, char* buffer, size_t buffer_size);

size_t _zxtest_print_str(const char* val, char* buffer, size_t buffer_size);

size_t _zxtest_print_ptr(const void* val, char* buffer, size_t buffer_size);

size_t _zxtest_print_hex(const void* val, size_t size, char* buffer, size_t buffer_size);

void zxtest_c_clean_buffer(char** buffer);

__END_CDECLS

#define _EQ(actual, expected) actual == expected
#define _NE(actual, expected) actual != expected
#define _BOOL(actual, expected) (bool)actual == expected
#define _LT(actual, expected) actual < expected
#define _LE(actual, expected) actual <= expected
#define _GT(actual, expected) actual > expected
#define _GE(actual, expected) actual >= expected
#define _STREQ(actual, expected) (strcmp(actual, expected) == 0)
#define _STRNE(actual, expected) !_STREQ(actual, expected)
#define _BYTEEQ(actual, expected, size) memcmp(actual, expected, size) == 0
#define _BYTENE(actual, expected, size) memcmp(actual, expected, size) != 0

// C specific macros for registering tests.
#define RUN_ALL_TESTS(argc, argv) zxtest_run_all_tests(argc, argv)

#define _ZXTEST_TEST_REF(TestCase, Test) TestCase##_##Test##_ref

#define _ZXTEST_TEST_FN(TestCase, Test) TestCase##_##Test##_fn

#define _ZXTEST_REGISTER_FN(TestCase, Test) TestCase##_##Test##_register_fn

// Register a test as part of a TestCase.
#define TEST(TestCase, Test)                                                          \
  static zxtest_test_ref_t _ZXTEST_TEST_REF(TestCase, Test) = {.test_index = 0,       \
                                                               .test_case_index = 0}; \
  static void _ZXTEST_TEST_FN(TestCase, Test)(void);                                  \
  static void _ZXTEST_REGISTER_FN(TestCase, Test)(void) __attribute__((constructor)); \
  void _ZXTEST_REGISTER_FN(TestCase, Test)(void) {                                    \
    _ZXTEST_TEST_REF(TestCase, Test) = zxtest_runner_register_test(                   \
        #TestCase, #Test, __FILE__, __LINE__, &_ZXTEST_TEST_FN(TestCase, Test));      \
  }                                                                                   \
  void _ZXTEST_TEST_FN(TestCase, Test)(void)

// Helper function to print variables.
// clang-format off
#define _ZXTEST_SPRINT_PRINTER(var, buffer, size)                                                  \
    _Generic((var),                                                                                \
            int8_t: _zxtest_print_int32,                                                           \
            uint8_t: _zxtest_print_int32,                                                          \
            int16_t: _zxtest_print_int32,                                                          \
            uint16_t: _zxtest_print_int32,                                                         \
            int32_t: _zxtest_print_int32,                                                          \
            uint32_t: _zxtest_print_uint32,                                                        \
            int64_t: _zxtest_print_int64,                                                          \
            uint64_t: _zxtest_print_uint64,                                                        \
            bool: _zxtest_print_bool,                                                              \
            const char*: _zxtest_print_str,                                                        \
            default: _zxtest_print_ptr)(var, buffer, size)
// clang-format on

#define _ZXTEST_NULLPTR NULL

#define _ZXTEST_HEX_PRINTER(var, var_size, buffer, size) \
  _zxtest_print_hex((const void*)var, var_size, buffer, size)

#define _ZXTEST_PRINT_BUFFER_NAME(var, type, line)                                   \
  char str_placeholder_##type##_##line = '\0';                                       \
  size_t buff_size_##type##_##line =                                                 \
      _ZXTEST_SPRINT_PRINTER(var, &str_placeholder_##type##_##line, 1) + 1;          \
  char* str_buffer_##type##_##line __attribute__((cleanup(zxtest_c_clean_buffer))) = \
      (char*)malloc(buff_size_##type##_##line * sizeof(char));                       \
  memset(str_buffer_##type##_##line, '\0', buff_size_##type##_##line);               \
  _ZXTEST_SPRINT_PRINTER(var, str_buffer_##type##_##line, buff_size_##type##_##line)

#define _ZXTEST_PRINT_BUFFER_NAME_HEX(var, var_size, type, line)                     \
  char str_placeholder_##type##_##line = '\0';                                       \
  size_t buff_size_##type##_##line =                                                 \
      _ZXTEST_HEX_PRINTER(var, var_size, &str_placeholder_##type##_##line, 1) + 1;   \
  char* str_buffer_##type##_##line __attribute__((cleanup(zxtest_c_clean_buffer))) = \
      (char*)malloc(buff_size_##type##_##line * sizeof(char));                       \
  memset(str_buffer_##type##_##line, '\0', buff_size_##type##_##line);               \
  _ZXTEST_HEX_PRINTER(var, var_size, str_buffer_##type##_##line, buff_size_##type##_##line)

#define _ZXTEST_LOAD_PRINT_VAR(var, type, line) _ZXTEST_PRINT_BUFFER_NAME(var, type, line)

#define _ZXTEST_LOAD_PRINT_HEX(var, var_size, type, line) \
  _ZXTEST_PRINT_BUFFER_NAME_HEX(var, var_size, type, line)

#define _ZXTEST_GET_PRINT_VAR(var, type, line) str_buffer_##type##_##line

// Provides an alias for assertion mechanisms.
#define _ZXTEST_ASSERT(desc, expected, expected_var, actual, actual_var, file, line, is_fatal) \
  zxtest_runner_notify_assertion(desc, expected, expected_var, actual, actual_var, file, line, \
                                 is_fatal)

#define _ZXTEST_TEST_HAS_ERRORS zxtest_runner_current_test_has_failures()
#define _ZXTEST_ABORT_IF_ERROR zxtest_runner_current_test_has_fatal_failures()
#define _ZXTEST_STRCMP(actual, expected) (strcmp(actual, expected) == 0)

#define _ZXTEST_AUTO_VAR_TYPE(var) __typeof__(var)

// Basic macros for assertions.

// Used to cleanup allocated buffers for formatted messages.
// Marked unused to prevent compiler warnings.
static void zxtest_clean_buffer(char** buffer) __attribute__((unused));
static void zxtest_clean_buffer(char** buffer) { free(*buffer); }

#define _GEN_ASSERT_DESC(out_desc, desc, ...)                               \
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

#define _RETURN_IF_FATAL_1        \
  do {                            \
    if (_ZXTEST_ABORT_IF_ERROR) { \
      unittest_returns_early();   \
      return;                     \
    }                             \
  } while (0)

#define _RETURN_IF_FATAL_0 \
  do {                     \
  } while (0)

#define _RETURN_IF_FATAL(fatal) _RETURN_IF_FATAL_##fatal

#define _ASSERT_VAR_BYTES(op, expected, actual, size, fatal, file, line, desc, ...)          \
  do {                                                                                       \
    const void* _actual = (const void*)(actual);                                             \
    const void* _expected = (const void*)(expected);                                         \
    if (!(op(_actual, _expected, size))) {                                                   \
      _GEN_ASSERT_DESC(msg_buffer, desc, ##__VA_ARGS__);                                     \
      _ZXTEST_LOAD_PRINT_HEX(_actual, size, act, line);                                      \
      _ZXTEST_LOAD_PRINT_HEX(_expected, size, exptd, line);                                  \
      _ZXTEST_ASSERT(msg_buffer, #expected, _ZXTEST_GET_PRINT_VAR(_expected, exptd, line),   \
                     #actual, _ZXTEST_GET_PRINT_VAR(_actual, act, line), file, line, fatal); \
      _RETURN_IF_FATAL(fatal);                                                               \
    }                                                                                        \
  } while (0)

#define _ASSERT_VAR_COERCE(op, expected, actual, type, fatal, file, line, desc, ...)         \
  do {                                                                                       \
    const type _actual = (const type)(actual);                                               \
    const type _expected = (const type)(expected);                                           \
    if (!(op(_actual, _expected))) {                                                         \
      _GEN_ASSERT_DESC(msg_buffer, desc, ##__VA_ARGS__);                                     \
      _ZXTEST_LOAD_PRINT_VAR(_actual, act, line);                                            \
      _ZXTEST_LOAD_PRINT_VAR(_expected, exptd, line);                                        \
      _ZXTEST_ASSERT(msg_buffer, #expected, _ZXTEST_GET_PRINT_VAR(_expected, exptd, line),   \
                     #actual, _ZXTEST_GET_PRINT_VAR(_actual, act, line), file, line, fatal); \
      _RETURN_IF_FATAL(fatal);                                                               \
    }                                                                                        \
  } while (0)

#define _ASSERT_VAR(op, expected, actual, fatal, file, line, desc, ...)                        \
  _ASSERT_VAR_COERCE(op, expected, actual, _ZXTEST_AUTO_VAR_TYPE(expected), fatal, file, line, \
                     desc, ##__VA_ARGS__)

#define _ZXTEST_FAIL_NO_RETURN(fatal, desc, ...)                            \
  do {                                                                      \
    _GEN_ASSERT_DESC(msg_buffer, desc, ##__VA_ARGS__);                      \
    zxtest_runner_fail_current_test(fatal, __FILE__, __LINE__, msg_buffer); \
  } while (0)

#define _ZXTEST_ASSERT_ERROR(has_errors, fatal, desc, ...) \
  do {                                                     \
    if (has_errors) {                                      \
      _ZXTEST_FAIL_NO_RETURN(fatal, desc, ##__VA_ARGS__);  \
      _RETURN_IF_FATAL(fatal);                             \
    }                                                      \
  } while (0)

#ifdef __Fuchsia__
#define _ASSERT_VAR_STATUS(op, expected, actual, fatal, file, line, desc, ...)        \
  do {                                                                                \
    const zx_status_t _actual = (const zx_status_t)(actual);                          \
    const zx_status_t _expected = (const zx_status_t)(expected);                      \
    if (!(op(_actual, _expected))) {                                                  \
      _GEN_ASSERT_DESC(msg_buffer, desc, ##__VA_ARGS__);                              \
      _ZXTEST_ASSERT(msg_buffer, #expected, zx_status_get_string(_expected), #actual, \
                     zx_status_get_string(_actual), file, line, fatal);               \
      _RETURN_IF_FATAL(fatal);                                                        \
    }                                                                                 \
  } while (0)

#define _ZXTEST_DEATH_STATUS_COMPLETE kDeathResultComplete
#define _ZXTEST_DEATH_STATUS_EXCEPTION kDeathResultRaiseException
#define _ZXTEST_DEATH_STATEMENT(statement, expected_result, desc, ...)                  \
  do {                                                                                  \
    _GEN_ASSERT_DESC(msg_buffer, desc, ##__VA_ARGS__);                                  \
    if (!zxtest_death_statement_execute(statement, expected_result, __FILE__, __LINE__, \
                                        msg_buffer)) {                                  \
      return;                                                                           \
    }                                                                                   \
  } while (0)

#else
#define _ASSERT_VAR_STATUS(...) _ASSERT_VAR(__VA_ARGS__)
#endif

#endif  // ZXTEST_C_ZXTEST_H_
