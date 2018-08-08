// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_runner.h"

namespace {

// This is a test that does nothing.  This is useful for measuring the
// overhead of the performance testing framework.  There will be some
// overhead involved in calling the function (via a direct or indirect
// call if the call is not inlined) and collecting timing data around
// the function call.
void NullTest() {}

__attribute__((constructor)) void RegisterTests() {
  fbenchmark::RegisterTestFunc<NullTest>("Null");
}

}  // namespace
