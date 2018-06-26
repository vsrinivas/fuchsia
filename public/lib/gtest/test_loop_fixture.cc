// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/gtest/test_loop_fixture.h"

namespace gtest {

TestLoopFixture::TestLoopFixture() = default;

TestLoopFixture::~TestLoopFixture() = default;

void TestLoopFixture::RunLoopRepeatedlyFor(zx::duration increment) {
  while (RunLoopFor(increment)) {
  }
}

}  // namespace gtest
