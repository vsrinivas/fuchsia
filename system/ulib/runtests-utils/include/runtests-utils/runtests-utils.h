// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Code for running other binaries as tests and recording their results.

#pragma once

#include <stdio.h>
#include <zircon/listnode.h>

namespace runtests {

// Possible results from a test run.
typedef enum {
    SUCCESS,
    FAILED_TO_LAUNCH,
    FAILED_TO_WAIT,
    FAILED_TO_RETURN_CODE,
    FAILED_NONZERO_RETURN_CODE,
} test_result_t;

// Represents a single test result that can be appended to a linked list.
typedef struct test {
    list_node_t node;
    test_result_t result;
    int rc; // Return code.
    // TODO(ZX-2050): Track duration of test binary.
    char name[0];
} test_t;

// Creates a new test_t and appends it to the linked list |tests|.
//
// |tests| is a linked list to which a new test_result_t will be appended.
// |name| is the name of the test.
// |result| is the result of trying to execute the test.
// |rc| is the return code of the test.
void record_test_result(list_node_t* tests, const char* name, test_result_t result, int rc);

// Invokes a test binary and prints results to stdout.
//
// |path| specifies the path to the binary.
// |verbosity| if positive, will be converted to a string and passed on the command line
//   to the test. I.e. |path| v=|verbosity|.
// |out| is a file stream to which the test binary's output will be written. May be
//   nullptr, in which output will not be redirected.
// |tests| is a list of test_t structs. Test results will be appended to this list, one per test
//   binary that is run.
//
// Returns true if the test binary successfully executes and has a return code of zero.
bool run_test(const char* path, signed char verbosity, FILE* out, list_node_t* tests);

}  // namespace runtests
