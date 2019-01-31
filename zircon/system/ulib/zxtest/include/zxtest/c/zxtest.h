// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

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
bool zxtest_runner_should_abort_current_test(void);

// Entry point for executing all tests.
int zxtest_run_all_tests(int argc, char** argv);

// Internal for generating human readable output in C.
size_t _zxtest_print_int32(int32_t val, char* buffer, size_t buffer_size);

size_t _zxtest_print_uint32(int32_t val, char* buffer, size_t buffer_size);

size_t _zxtest_print_int64(int64_t val, char* buffer, size_t buffer_size);

size_t _zxtest_print_uint64(uint64_t val, char* buffer, size_t buffer_size);

size_t _zxtest_print_bool(bool val, char* buffer, size_t buffer_size);

size_t _zxtest_print_str(const char* val, char* buffer, size_t buffer_size);

size_t _zxtest_print_ptr(const void* val, char* buffer, size_t buffer_size);

size_t _zxtest_print_hex(const void* val, size_t size, char* buffer, size_t buffer_size);

void zxtest_c_clean_buffer(char** buffer);
#ifdef __cplusplus
}
#endif

// C specific macros for registering tests.
#define RUN_ALL_TESTS(argc, argv) zxtest_run_all_tests(argc, argv)

#define _ZXTEST_TEST_REF(TestCase, Test) TestCase##_##Test##_ref

#define _ZXTEST_TEST_FN(TestCase, Test) TestCase##_##Test##_fn

#define _ZXTEST_REGISTER_FN(TestCase, Test) TestCase##_##Test##_register_fn

// Register a test as part of a TestCase.
#define TEST(TestCase, Test)                                                                       \
    static zxtest_test_ref_t _ZXTEST_TEST_REF(TestCase, Test) = {.test_index = 0,                  \
                                                                 .test_case_index = 0};            \
    static void _ZXTEST_TEST_FN(TestCase, Test)(void);                                             \
    static void _ZXTEST_REGISTER_FN(TestCase, Test)(void) __attribute__((constructor));            \
    void _ZXTEST_REGISTER_FN(TestCase, Test)(void) {                                               \
        _ZXTEST_TEST_REF(TestCase, Test) = zxtest_runner_register_test(                            \
            #TestCase, #Test, __FILE__, __LINE__, &_ZXTEST_TEST_FN(TestCase, Test));               \
    }                                                                                              \
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
            default: _zxtest_print_ptr)(var, buffer, size)
// clang-format on

#define _ZXTEST_NULLPTR NULL

#define _ZXTEST_HEX_PRINTER(var, buffer, size)                                                     \
    _zxtest_print_hex((const void*)&var, sizeof(__typeof__(var)), buffer, size)

#define _ZXTEST_PRINT_BUFFER_NAME(var, type, line, printer)                                        \
    char str_placeholder_##type##_##line = '\0';                                                   \
    size_t buff_size_##type##_##line = printer(var, &str_placeholder_##type##_##line, 1) + 1;      \
    char* str_buffer_##type##_##line __attribute__((cleanup(zxtest_c_clean_buffer))) =             \
        (char*)malloc(buff_size_##type##_##line * sizeof(char));                                   \
    memset(str_buffer_##type##_##line, '\0', buff_size_##type##_##line);                           \
    printer(var, str_buffer_##type##_##line, buff_size_##type##_##line)

#define _ZXTEST_LOAD_PRINT_VAR(var, type, line)                                                    \
    _ZXTEST_PRINT_BUFFER_NAME(var, type, line, _ZXTEST_SPRINT_PRINTER)

#define _ZXTEST_LOAD_PRINT_HEX(var, type, line)                                                    \
    _ZXTEST_PRINT_BUFFER_NAME(var, type, line, _ZXTEST_HEX_PRINTER)

#define _ZXTEST_TOKEN(token) token
#define _ZXTEST_GET_PRINT_VAR(var, type, line) str_buffer_##type##_##line

// Provides an alias for assertion mechanisms.
#define _ZXTEST_ASSERT(desc, expected, expected_var, actual, actual_var, file, line, is_fatal)     \
    zxtest_runner_notify_assertion(desc, expected, expected_var, actual, actual_var, file, line,   \
                                   is_fatal)

#define _ZXTEST_ABORT_IF_ERROR zxtest_runner_should_abort_current_test()
#define _ZXTEST_STRCMP(actual, expected) (strcmp(actual, expected) == 0)

#define _ZXTEST_AUTO_VAR_TYPE(var) __typeof__(var)
