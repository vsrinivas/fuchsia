// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/loop_fixture/test_loop_fixture.h"

#include <stdlib.h>

#include "third_party/abseil-cpp/absl/flags/flag.h"
#include "third_party/abseil-cpp/absl/flags/parse.h"

namespace ledger {

TestLoopFixture::TestLoopFixture() = default;

TestLoopFixture::~TestLoopFixture() = default;

void TestLoopFixture::RunLoopRepeatedlyFor(zx::duration increment) {
  while (RunLoopFor(increment)) {
  }
}

}  // namespace ledger
