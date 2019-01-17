// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>

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
void zxtest_runner_notify_assertion(const char* desc, const char* expected, const char* actual,
                                    const char* actual_var, const char* file, int64_t line,
                                    bool is_fatal);

// When an assertion happens out of the main test body, this allows keeping track of whether the
// current flow should abort.
bool zxtest_runner_should_abort_current_test(void);

// Entry point for executing all tests.
int zxtest_run_all_tests(int argc, char** argv);

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
