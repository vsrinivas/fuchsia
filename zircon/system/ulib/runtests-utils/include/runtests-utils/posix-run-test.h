// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>

#include <memory>

#include <runtests-utils/runtests-utils.h>

namespace runtests {

// Invokes a POSIX test binary and writes its output to a file.
//
// |argv| is a null-terminated array of argument strings passed to the test
//   program.
// |output_dir| is currently ignored.
// |output_filename| is the name of the file to which the test binary's output
//   will be written. May be nullptr, in which case the output will not be
//   redirected.
// |test_name| is used to populate Result properly and in log messages.
// |timeout_millis| is a number of milliseconds to wait for the test. If 0,
//   will wait indefinitely.
std::unique_ptr<Result> PosixRunTest(const char* argv[], const char* output_dir,
                                     const char* output_filename, const char* test_name,
                                     uint64_t timeout_millis);

}  // namespace runtests
