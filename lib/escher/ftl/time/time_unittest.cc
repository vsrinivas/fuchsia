// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <thread>

#include "ftl/time/time.h"
#include "gtest/gtest.h"

namespace ftl {
namespace {

TEST(Time, Now) {
  auto start = Now();
  for (int i=0; i < 3; ++i) {
    auto now = Now();
    EXPECT_GE(now, start);
    std::this_thread::yield();
  }
}

}  // namespace
}  // namespace ftl
