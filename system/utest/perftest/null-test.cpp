// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <perftest/perftest.h>

namespace {

// This is a test that does nothing.  This is useful for measuring the
// overhead of the performance testing framework.  There will be some
// overhead in the perftest framework's loop that calls this function, and
// in the KeepRunning() calls that collect timing data.
bool NullTest() {
    return true;
}

void RegisterTests() {
    perftest::RegisterSimpleTest<NullTest>("Null");
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
