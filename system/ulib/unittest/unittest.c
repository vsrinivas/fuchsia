// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include <pretty/hexdump.h>

#ifdef UNITTEST_CRASH_HANDLER_SUPPORTED
#include "crash-list.h"
#include "crash-handler.h"
#endif // UNITTEST_CRASH_HANDLER_SUPPORTED

typedef uint64_t nsecs_t;

static nsecs_t now(void) {
#ifdef __Fuchsia__
    return mx_time_get(MX_CLOCK_MONOTONIC);
#else
    // clock_gettime(CLOCK_MONOTONIC) would be better but may not exist on the host
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0)
        return 0u;
    return tv.tv_sec * 1000000000ull + tv.tv_usec * 1000ull;
#endif
}

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

#ifdef UNITTEST_CRASH_HANDLER_SUPPORTED
void unittest_register_crash(struct test_info* current_test_info, mx_handle_t handle) {
    crash_list_register(current_test_info->crash_list, handle);
}

bool unittest_run_death_fn(void (*fn_to_run)(void*), void* arg) {
    test_result_t test_result;
    mx_status_t status = run_fn_with_crash_handler(fn_to_run, arg, &test_result);
    return status == MX_OK && test_result == TEST_CRASHED;
}
#endif // UNITTEST_CRASH_HANDLER_SUPPORTED

void unittest_run_named_test(const char* name, bool (*test)(void),
                             test_type_t test_type,
                             struct test_info** current_test_info,
                             bool* all_success, bool enable_crash_handler) {
    if (utest_test_type & test_type) {
        unittest_printf_critical("    %-51s [RUNNING]", name);
        nsecs_t start_time = now();
        struct test_info test_info = { .all_ok = true, NULL };
        *current_test_info = &test_info;
        // The crash handler is disabled by default. To enable, the test should
        // be run with RUN_TEST_ENABLE_CRASH_HANDLER.
        if (enable_crash_handler) {
#ifdef UNITTEST_CRASH_HANDLER_SUPPORTED
            test_info.crash_list = crash_list_new();

            test_result_t test_result;
            mx_status_t status = run_test_with_crash_handler(test_info.crash_list,
                                                             test,
                                                             &test_result);
            if (status != MX_OK || test_result == TEST_FAILED) {
                test_info.all_ok = false;
            }

            // Check if there were any processes registered to crash but didn't.
            bool missing_crash = crash_list_delete(test_info.crash_list);
            if (missing_crash) {
                // TODO: display which expected crash did not occur.
                UNITTEST_TRACEF("Expected crash did not occur\n");
                test_info.all_ok = false;
            }
#else // UNITTEST_CRASH_HANDLER_SUPPORTED
            UNITTEST_TRACEF("Crash tests not supported\n");
            test_info.all_ok = false;
#endif // UNITTEST_CRASH_HANDLER_SUPPORTED
        } else if (!test()) {
            test_info.all_ok = false;
        }

        // Recheck all_ok in case there was a failure in a C++ destructor
        // after the "return" statement in END_TEST.
        if (!test_info.all_ok)
            *all_success = false;

        nsecs_t end_time = now();
        uint64_t time_taken_ms = (end_time - start_time) / 1000000;
        unittest_printf_critical(" [%s] (%d ms)\n",
                                 test_info.all_ok ? "PASSED" : "FAILED",
                                 (int)time_taken_ms);

        *current_test_info = NULL;
    } else {
        unittest_printf_critical("    %-51s [IGNORED]\n", name);
    }
}
