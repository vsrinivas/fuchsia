// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/PlatformManager.h>
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
// clang-format on

#include <lib/gtest/test_loop_fixture.h>

#include "src/connectivity/weave/adaptation/platform_manager_impl.h"

namespace weave::adaptation::testing {
namespace {

using nl::Weave::DeviceLayer::PlatformMgr;
using nl::Weave::DeviceLayer::PlatformMgrImpl;

}  // namespace

class PlatformManagerTest : public ::gtest::TestLoopFixture {
 public:
  void SetUp() override { PlatformMgrImpl().SetDispatcher(dispatcher()); }
};

TEST_F(PlatformManagerTest, ScheduleWork) {
  size_t counter = 0;
  auto increment_counter = [](intptr_t context) { (*reinterpret_cast<size_t*>(context))++; };

  PlatformMgr().ScheduleWork(increment_counter, reinterpret_cast<intptr_t>(&counter));
  RunLoopUntilIdle();
  EXPECT_EQ(counter, 1U);

  PlatformMgr().ScheduleWork(increment_counter, reinterpret_cast<intptr_t>(&counter));
  PlatformMgr().ScheduleWork(increment_counter, reinterpret_cast<intptr_t>(&counter));
  RunLoopUntilIdle();
  EXPECT_EQ(counter, 3U);
}

}  // namespace weave::adaptation::testing
