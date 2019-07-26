// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Declarations of symbols that will have different definitions on different platforms.
// These are defined by <platform>-test-main.cpp.

#include <runtests-utils/runtests-utils.h>

namespace runtests {

// Shell script shebang.
extern const char kScriptShebang[32];

// Invokes a test binary and writes its output to a file.
extern const RunTestFn PlatformRunTest;

// Returns the root directory of filesystem used for testing. For Fuchsia, we
// use the in-memory memfs; for POSIX systems, we use a unique subdirectory of
// TMPDIR if set, else that of /tmp.
const char* TestFsRoot();

}  // namespace runtests
