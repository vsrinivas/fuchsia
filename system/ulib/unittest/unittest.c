// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pretty/hexdump.h>

/**
 * \brief Default function to dump unit test results
 *
 * \param[in] line is the buffer to dump
 * \param[in] len is the length of the buffer to dump
 * \param[in] arg can be any kind of arguments needed to dump the values
 */
static void default_printf(const char* line, int len, void* arg) {
    fputs(line, stdout);
    fflush(stdout);
}

// Default output function is the printf
static test_output_func out_func = default_printf;
// Buffer the argument to be sent to the output function
static void* out_func_arg = NULL;

// Controls the behavior of unittest_printf.
// To override, specify v=N on the command line.
int utest_verbosity_level = 0;

// Controls the types of tests which are executed.
// Multiple test types can be "OR-ed" together to
// run a subset of all tests.
test_type_t utest_test_type = TEST_DEFAULT;

/**
 * \brief Function called to dump results
 *
 * This function will call the out_func callback
 */
void unittest_printf_critical(const char* format, ...) {
    static char print_buffer[PRINT_BUFFER_SIZE];

    va_list argp;
    va_start(argp, format);

    if (out_func != NULL) {
        // Format the string
        vsnprintf(print_buffer, PRINT_BUFFER_SIZE, format, argp);
        out_func(print_buffer, PRINT_BUFFER_SIZE, out_func_arg);
    }

    va_end(argp);
}

bool unittest_expect_bytes_eq(const uint8_t* expected, const uint8_t* actual, size_t len,
                              const char* msg) {
    if (memcmp(expected, actual, len)) {
        printf("%s. expected\n", msg);
        hexdump8(expected, len);
        printf("actual\n");
        hexdump8(actual, len);
        return false;
    }
    return true;
}

bool unittest_expect_str_eq(const char* expected, const char* actual, size_t len,
                            const char* msg) {
    if (strncmp(expected, actual, len)) {
        printf("%s. expected\n'%s'\nactual\n'%s'\n", msg, expected, actual);
        return false;
    }
    return true;
}

void unittest_set_output_function(test_output_func fun, void* arg) {
    out_func = fun;
    out_func_arg = arg;
}

int unittest_set_verbosity_level(int new_level) {
    int out = utest_verbosity_level;
    utest_verbosity_level = new_level;
    return out;
}

void unittest_run_named_test(const char* name, bool (*test)(void),
                             test_type_t test_type,
                             struct test_info** current_test_info,
                             bool* all_success) {
    if (utest_test_type & test_type) {
        unittest_printf_critical("    %-51s [RUNNING]", name);
        struct test_info test_info;
        *current_test_info = &test_info;
        if (!test()) {
            *all_success = false;
        } else {
            unittest_printf_critical(" [PASSED] \n");
        }
        current_test_info = NULL;
    } else {
        unittest_printf_critical("    %-51s [IGNORED]\n", name);
    }
}
