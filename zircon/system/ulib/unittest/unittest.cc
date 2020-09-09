// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include <pretty/hexdump.h>
#include <unittest/unittest.h>

#ifdef UNITTEST_DEATH_TEST_SUPPORTED

#include <lib/test-exceptions/exception-handling.h>
#include <lib/zx/channel.h>
#include <lib/zx/exception.h>
#include <lib/zx/port.h>
#include <lib/zx/thread.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>

#endif  // UNITTEST_DEATH_TEST_SUPPORTED

#include "watchdog.h"

// Some strings that are used for comparison purposes can be pretty long, and
// when printing the failure message it's important to see what the failing
// text is. That's why this is as large as it is.
#define PRINT_BUFFER_SIZE (4096)

using nsecs_t = uint64_t;

static nsecs_t now() {
#ifdef __Fuchsia__
  return zx_clock_get_monotonic();
#else
  // clock_gettime(CLOCK_MONOTONIC) would be better but may not exist on the host
  struct timeval tv;
  if (gettimeofday(&tv, nullptr) < 0)
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
static void* out_func_arg = nullptr;

// Controls the behavior of unittest_printf.
// To override, specify v=N on the command line.
__EXPORT
int utest_verbosity_level = 0;

// Controls the types of tests which are executed.
// Multiple test types can be "OR-ed" together to
// run a subset of all tests.
__EXPORT
test_type_t utest_test_type = static_cast<test_type>(TEST_DEFAULT);

/**
 * \brief Function called to dump results
 *
 * This function will call the out_func callback
 */
__EXPORT
void unittest_printf_critical(const char* format, ...) {
  static char print_buffer[PRINT_BUFFER_SIZE];

  va_list argp;
  va_start(argp, format);

  if (out_func) {
    // Format the string
    vsnprintf(print_buffer, PRINT_BUFFER_SIZE, format, argp);
    out_func(print_buffer, PRINT_BUFFER_SIZE, out_func_arg);
  }

  va_end(argp);
}

__EXPORT
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

__EXPORT
bool unittest_expect_str_eq(const char* str1_value, const char* str2_value, const char* str1_expr,
                            const char* str2_expr, const char* msg, const char* source_filename,
                            int source_line_num, const char* source_function) {
  if (strcmp(str1_value, str2_value)) {
    unittest_printf_critical(UNITTEST_FAIL_TRACEF_FORMAT
                             "%s:\n"
                             "        Comparison failed: strings not equal:\n"
                             "        String 1 expression: %s\n"
                             "        String 2 expression: %s\n"
                             "        String 1 value: \"%s\"\n"
                             "        String 2 value: \"%s\"\n",
                             source_filename, source_line_num, source_function, msg, str1_expr,
                             str2_expr, str1_value, str2_value);
    return false;
  }
  return true;
}

__EXPORT
bool unittest_expect_str_ne(const char* str1_value, const char* str2_value, const char* str1_expr,
                            const char* str2_expr, const char* msg, const char* source_filename,
                            int source_line_num, const char* source_function) {
  if (!strcmp(str1_value, str2_value)) {
    unittest_printf_critical(UNITTEST_FAIL_TRACEF_FORMAT
                             "%s:\n"
                             "        Comparison failed: strings are equal,"
                             " but expected different strings:\n"
                             "        String 1 expression: %s\n"
                             "        String 2 expression: %s\n"
                             "        Value of both strings: \"%s\"\n",
                             source_filename, source_line_num, source_function, msg, str1_expr,
                             str2_expr, str1_value);
    return false;
  }
  return true;
}

__EXPORT
bool unittest_expect_str_str(const char* str1_value, const char* str2_value, const char* str1_expr,
                             const char* str2_expr, const char* msg, const char* source_filename,
                             int source_line_num, const char* source_function) {
  if (!strstr(str1_value, str2_value)) {
    unittest_printf_critical(UNITTEST_FAIL_TRACEF_FORMAT
                             "%s:\n"
                             "        Comparison failed: String 1 does not"
                             " contain String 2:\n"
                             "        String 1 expression: %s\n"
                             "        String 2 expression: %s\n"
                             "        Value of both strings: \"%s\"\n",
                             source_filename, source_line_num, source_function, msg, str1_expr,
                             str2_expr, str1_value);
    return false;
  }
  return true;
}

__EXPORT
void unittest_set_output_function(test_output_func fun, void* arg) {
  out_func = fun;
  out_func_arg = arg;
}

__EXPORT
void unittest_restore_output_function() {
  out_func = default_printf;
  out_func_arg = nullptr;
}

__EXPORT
int unittest_set_verbosity_level(int new_level) {
  int out = utest_verbosity_level;
  utest_verbosity_level = new_level;
  return out;
}

#ifdef UNITTEST_DEATH_TEST_SUPPORTED

namespace {

enum { kPortKeyThreadException, kPortKeyThreadCompleted };

// All the state that's necessary to share between the main unittest thread
// and the death thread.
struct RunDeathFunctionState {
  // The death function to call and argument to pass it.
  void (*fn_to_run)(void*);
  void* arg;

  // The port to register the exception channel on.
  zx::port port;

  // Thread and channel are filled in by RunDeathFunction().
  zx::thread zx_thread;
  zx::channel exception_channel;
};

// Sets up the necessary state and calls |fn_to_run|.
//
// Basic flow is:
//  1. Creates the exception channel.
//  2. Registers the port for exceptions or thread completion.
//  3. Calls the death function
//
// Returns:
//  ZX_OK if the death function did not hit an exception.
//  Non-OK if setup failed.
//  Does not return if the death function hit an exception.
int RunDeathFunction(void* arg) {
  RunDeathFunctionState* state = reinterpret_cast<RunDeathFunctionState*>(arg);

  // The caller needs a thread handle to kill if it hits an exception. This has
  // to be a full handle (i.e. not unowned_thread) or else it might be destroyed
  // and unregistered from the port wait before we get the signal.
  if (zx_status_t status = zx::thread::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &state->zx_thread);
      status != ZX_OK) {
    unittest_printf_critical("failed to duplicate thread handle: %s\n",
                             zx_status_get_string(status));
    return status;
  }

  // Register for thread completion signal on the port.
  if (zx_status_t status = state->zx_thread.wait_async(state->port, kPortKeyThreadCompleted,
                                                       ZX_THREAD_TERMINATED, 0);
      status != ZX_OK) {
    unittest_printf_critical("failed to wait_async on thread: %s\n", zx_status_get_string(status));
    return status;
  }

  // We have to create the exception channel here, since we don't have access
  // to the thread handle until we're in the thread.
  if (zx_status_t status = state->zx_thread.create_exception_channel(0, &state->exception_channel);
      status != ZX_OK) {
    unittest_printf_critical("failed to create exception channel: %s\n",
                             zx_status_get_string(status));
    return status;
  }

  // Register for exception signal on the port.
  if (zx_status_t status = state->exception_channel.wait_async(state->port, kPortKeyThreadException,
                                                               ZX_CHANNEL_READABLE, 0);
      status != ZX_OK) {
    unittest_printf_critical("failed to wait_async on exception channel: %s\n",
                             zx_status_get_string(status));
    return status;
  }

  state->fn_to_run(state->arg);
  return ZX_OK;
}

}  // namespace

__EXPORT
death_test_result_t unittest_run_death_fn(void (*fn_to_run)(void*), void* arg) {
  RunDeathFunctionState state;
  state.fn_to_run = fn_to_run;
  state.arg = arg;

  if (zx_status_t status = zx::port::create(0, &state.port); status != ZX_OK) {
    unittest_printf_critical("failed to create port: %s\n", zx_status_get_string(status));
    return DEATH_TEST_RESULT_INTERNAL_ERROR;
  }

  // We intentionally use C thread APIs rather than std::thread to avoid
  // leaking memory. The thread still doesn't get fully cleaned up (see notes
  // below) but the C APIs at least allow us to pass on the LSAN build.
  thrd_t thread;
  if (int result = thrd_create(&thread, RunDeathFunction, &state); result != thrd_success) {
    unittest_printf_critical("failed to create thread: thrd code %d\n", result);
    return DEATH_TEST_RESULT_INTERNAL_ERROR;
  }

  // Wait for either thread exception or normal completion.
  zx_port_packet_t packet;
  if (zx_status_t status = state.port.wait(zx::time::infinite(), &packet); status != ZX_OK) {
    unittest_printf_critical("failed to wait on port: %s\n", zx_status_get_string(status));
    return DEATH_TEST_RESULT_INTERNAL_ERROR;
  }

  if (packet.key == kPortKeyThreadCompleted) {
    // The thread returned, either due to setup failure or no death.
    int thread_exit_code;
    if (int result = thrd_join(thread, &thread_exit_code); result != thrd_success) {
      unittest_printf_critical("failed to join thread: thrd code %d\n", result);
      return DEATH_TEST_RESULT_INTERNAL_ERROR;
    }
    return thread_exit_code == ZX_OK ? DEATH_TEST_RESULT_LIVED : DEATH_TEST_RESULT_INTERNAL_ERROR;
  }

  zx_exception_info_t info;
  zx::exception exception;
  zx_status_t status = state.exception_channel.read(0, &info, exception.reset_and_get_address(),
                                                    sizeof(info), 1, nullptr, nullptr);
  if (status != ZX_OK) {
    unittest_printf_critical("Failed to read exception: %d\n", status);
    return DEATH_TEST_RESULT_INTERNAL_ERROR;
  }

  // This causes the thread to exit via thrd_exit. It's impossible to to fully clean
  // up a thread that has hit an exception however and there are likely small leaks.
  status = test_exceptions::ExitExceptionCThread(std::move(exception));
  if (status != ZX_OK) {
    unittest_printf_critical("Failed to exit the exception thread: %d\n", status);
    return DEATH_TEST_RESULT_INTERNAL_ERROR;
  }

  // Check that the thrd exited successfully.
  int thread_exit_code;
  if (int result = thrd_join(thread, &thread_exit_code); result != thrd_success) {
    unittest_printf_critical("failed to join exception thread: thrd code %d\n", result);
    return DEATH_TEST_RESULT_INTERNAL_ERROR;
  }

  return DEATH_TEST_RESULT_DIED;
}

#endif  // UNITTEST_DEATH_TEST_SUPPORTED

static void unittest_run_test(const char* name, bool (*test)(),
                              struct test_info** current_test_info, bool* all_success) {
  unittest_printf_critical("    %-51s [RUNNING]", name);
  nsecs_t start_time = now();
  test_info test_info = {.all_ok = true, nullptr};
  *current_test_info = &test_info;
  if (!test()) {
    test_info.all_ok = false;
    *all_success = false;
  }

  nsecs_t end_time = now();
  uint64_t time_taken_ms = (end_time - start_time) / 1000000;
  unittest_printf_critical(" [%s] (%d ms)\n", test_info.all_ok ? "PASSED" : "FAILED",
                           static_cast<int>(time_taken_ms));

  *current_test_info = nullptr;
}

template <typename F>
void run_with_watchdog(test_type_t test_type, const char* name, F fn) {
  if (watchdog_is_enabled()) {
    watchdog_start(test_type, name);
    fn();
    watchdog_cancel();
  } else {
    fn();
  }
}

__EXPORT
void unittest_run_named_test(const char* name, bool (*test)(), test_type_t test_type,
                             struct test_info** current_test_info, bool* all_success) {
  if (utest_test_type & test_type) {
    run_with_watchdog(test_type, name,
                      [&]() { unittest_run_test(name, test, current_test_info, all_success); });
  } else {
    unittest_printf_critical("    %-51s [IGNORED]\n", name);
  }
}

__EXPORT
void unittest_cancel_timeout(void) { watchdog_cancel(); }
