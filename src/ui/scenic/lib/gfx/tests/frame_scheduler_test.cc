// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/frame_scheduler_test.h"

#include <lib/gtest/test_loop_fixture.h>

#include "src/ui/scenic/lib/gfx/engine/default_frame_scheduler.h"
#include "src/ui/scenic/lib/gfx/engine/windowed_frame_predictor.h"

namespace scenic_impl {
namespace gfx {
namespace test {

void FrameSchedulerTest::SetUp() {
  ErrorReportingTest::SetUp();

  fake_vsync_timing_ = std::make_shared<FakeVsyncTiming>();
  mock_updater_ = std::make_unique<MockSessionUpdater>();
  mock_renderer_ = std::make_unique<MockFrameRenderer>();
  SetupDefaultVsyncValues();
}

void FrameSchedulerTest::TearDown() {
  fake_vsync_timing_.reset();
  mock_updater_.reset();
  mock_renderer_.reset();

  ErrorReportingTest::TearDown();
}

std::unique_ptr<DefaultFrameScheduler> FrameSchedulerTest::CreateDefaultFrameScheduler() {
  auto scheduler = std::make_unique<DefaultFrameScheduler>(
      fake_vsync_timing_,
      std::make_unique<WindowedFramePredictor>(DefaultFrameScheduler::kInitialRenderDuration,
                                               DefaultFrameScheduler::kInitialUpdateDuration));
  scheduler->SetFrameRenderer(mock_renderer_->GetWeakPtr());
  scheduler->AddSessionUpdater(mock_updater_->GetWeakPtr());

  return scheduler;
}

void FrameSchedulerTest::SetupDefaultVsyncValues() {
  // Needs to be big enough so that FrameScheduler can always fit a latch point
  // in the frame.
  const auto vsync_interval = zx::msec(100);
  fake_vsync_timing_->SetVsyncInterval(vsync_interval);
  fake_vsync_timing_->SetLastVsyncTime(zx::time(0));
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
