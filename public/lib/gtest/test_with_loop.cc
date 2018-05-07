// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/gtest/test_with_loop.h"

#include <lib/async/cpp/task.h>

namespace gtest {

TestWithLoop::TestWithLoop() = default;

TestWithLoop::~TestWithLoop() = default;

void TestWithLoop::RunLoopRepeatedlyFor(zx::duration increment) {
  while (RunLoopFor(increment)) {
  }
}

}  // namespace gtest
