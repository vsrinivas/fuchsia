// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_TESTS_FRAME_SCHEDULER_TEST_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_TESTS_FRAME_SCHEDULER_TEST_H_

#include <lib/gtest/test_loop_fixture.h>

#include "src/ui/scenic/lib/scheduling/default_frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/tests/mocks/frame_scheduler_mocks.h"

namespace scheduling {
namespace test {

class FrameSchedulerTest : public ::gtest::TestLoopFixture {
 protected:
  // | ::testing::Test |
  void SetUp() override;
  void TearDown() override;

  std::unique_ptr<DefaultFrameScheduler> CreateDefaultFrameScheduler();

  void SetupDefaultVsyncValues();

  std::shared_ptr<VsyncTiming> vsync_timing_;
  std::unique_ptr<MockSessionUpdater> mock_updater_;
  std::unique_ptr<MockFrameRenderer> mock_renderer_;
};

}  // namespace test
}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_TESTS_FRAME_SCHEDULER_TEST_H_
