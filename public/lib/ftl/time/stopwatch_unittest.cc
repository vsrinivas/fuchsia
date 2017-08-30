// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/time/stopwatch.h"

#include "gtest/gtest.h"

namespace ftl {
namespace {

TEST(Stopwatch, Control) {
  Stopwatch stopwatch;
  stopwatch.Start();
  EXPECT_GE(stopwatch.Elapsed().ToNanoseconds(), 0);
}

}  // namespace
}  // namespace ftl
