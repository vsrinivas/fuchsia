// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unittest/unittest.h>

#include "crash-list.h"

__BEGIN_CDECLS

typedef enum {
    // The test function returned true and did not have any unregistered crashes.
    TEST_PASSED,
    // The test function returned false and did not have any unregistered crashes.
    TEST_FAILED,
    // The test function crashed before completion and was registered to crash.
    TEST_CRASHED
    // TODO(jocelyndang): add TEST_CRASHED_UNEXPECTEDLY for unregistered crashes -
    // this should be returned rather than exiting the test process.
} test_result_t;

/**
 * Runs the test in a separate thread, catching any crashes.
 *
 * A crash is expected if the process or thread handle is present in the
 * crash_list. crash_list_register can be used to register expected crashes, or
 * via the unittest helper macro REGISTER_CRASH.
 *
 * If an unexpected crash occurs, the test will be terminated immediately.
 *
 * Returns MX_OK if setup succeeded, otherwise a negative error value is
 * returned. If the return value is MX_OK, test_result will also be populated.
 */
mx_status_t run_test_with_crash_handler(crash_list_t crash_list,
                                        bool (*test_to_run)(void),
                                        test_result_t* test_result);

/**
 * Runs the function in a separate thread, passing in the given argument.
 * This will block until the function either crashes or returns.
 *
 * Returns MX_OK if setup succeeded, otherwise a negative error value is
 * returned. If the return value is MX_OK, test_result will also be populated.
 */
mx_status_t run_fn_with_crash_handler(void (*fn_to_run)(void*), void* arg,
                                      test_result_t* test_result);

__END_CDECLS
