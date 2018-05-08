// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Code for running other binaries as tests and recording their results.

#pragma once

#include <stdio.h>
#include <fbl/string.h>

namespace runtests {

// Status of launching a test subprocess.
enum LaunchStatus {
    SUCCESS,
    FAILED_TO_LAUNCH,
    FAILED_TO_WAIT,
    FAILED_TO_RETURN_CODE,
    FAILED_NONZERO_RETURN_CODE,
};

// Represents the result of a single test run.
struct Result {
    fbl::String name;
    LaunchStatus launch_status;
    int return_code;  // Only valid if launch_stauts == SUCCESS or FAILED_NONZERO_RETURN_CODE.
    // TODO(ZX-2050): Track duration of test binary.

    // Constructor really only needed until we have C++14, which will allow call-sites to use
    // aggregate initializer syntax.
    Result(const char* name_arg, LaunchStatus launch_status_arg, int return_code_arg)
        : name(name_arg), launch_status(launch_status_arg), return_code(return_code_arg) {}
};

// Invokes a test binary and prints results to stdout.
//
// |path| specifies the path to the binary.
// |verbosity| if positive, will be converted to a string and passed on the command line
//   to the test. I.e. |path| v=|verbosity|.
// |out| is a file stream to which the test binary's output will be written. May be
//   nullptr, in which output will not be redirected.
//
// Returns a Result indicating the result.
Result RunTest(const char* path, signed char verbosity, FILE* out);

}  // namespace runtests
