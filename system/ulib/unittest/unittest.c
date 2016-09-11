// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <hexdump/hexdump.h>

/**
 * \brief Default function to dump unit test results
 *
 * \param[in] line is the buffer to dump
 * \param[in] len is the length of the buffer to dump
 * \param[in] arg can be any kind of arguments needed to dump the values
 */
static void default_printf(const char* line, int len, void* arg) {
    printf("%s", line);
    fflush(stdout);
}

// Default output function is the printf
static test_output_func out_func = default_printf;
// Buffer the argument to be sent to the output function
static void* out_func_arg = NULL;

// Controls the behavior of unittest_printf.
// To override, specify v=N on the command line.
int utest_verbosity_level = 0;

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

void unittest_set_output_function(test_output_func fun, void* arg) {
    out_func = fun;
    out_func_arg = arg;
}

int unittest_set_verbosity_level(int new_level) {
    int out = utest_verbosity_level;
    utest_verbosity_level = new_level;
    return out;
}
