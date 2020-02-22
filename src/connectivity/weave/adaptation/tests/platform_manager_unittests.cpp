// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>

#include <Weave/DeviceLayer/PlatformManager.h>

#include "gtest/gtest.h"

namespace adaptation {
namespace testing {
namespace {
using nl::Weave::DeviceLayer::PlatformMgr;
}  // namespace

class PlatformManagerTest : public ::gtest::TestLoopFixture {
 public:
  PlatformManagerTest() {}
  void SetUp() override { TestLoopFixture::SetUp(); }
};

TEST_F(PlatformManagerTest, InitWeaveStackTest) {
  EXPECT_EQ(PlatformMgr().InitWeaveStack(), WEAVE_NO_ERROR);
}

}  // namespace testing
}  // namespace adaptation
