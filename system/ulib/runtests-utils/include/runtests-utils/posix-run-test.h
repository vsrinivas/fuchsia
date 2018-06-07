// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>

#include <runtests-utils/runtests-utils.h>

namespace runtests {

// Invokes a POSIX test binary and writes its output to a file.
//
// |argv| is list of argument strings passed to the test program.
// |argc| is the number of strings in argv.
// |output_filename| is the name of the file to which the test binary's output
//   will be written. May be nullptr, in which case the output will not be
//   redirected.
Result PosixRunTest(const char* argv[], int argc, const char* output_filename);

} // namespace runtests
