// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_FRAME_SCHEDULER_TEST_H_
#define GARNET_LIB_UI_GFX_TESTS_FRAME_SCHEDULER_TEST_H_

#include "garnet/lib/ui/gfx/engine/default_frame_scheduler.h"
#include "garnet/lib/ui/gfx/tests/error_reporting_test.h"
#include "garnet/lib/ui/gfx/tests/frame_scheduler_mocks.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class FrameSchedulerTest : public ErrorReportingTest {
 protected:
  // | ::testing::Test |
  void SetUp() override;
  void TearDown() override;

  std::unique_ptr<DefaultFrameScheduler> CreateDefaultFrameScheduler();

  void SetupDefaultVsyncValues();

  std::unique_ptr<FakeDisplay> fake_display_;
  std::unique_ptr<MockSessionUpdater> mock_updater_;
  std::unique_ptr<MockFrameRenderer> mock_renderer_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_FRAME_SCHEDULER_TEST_H_
