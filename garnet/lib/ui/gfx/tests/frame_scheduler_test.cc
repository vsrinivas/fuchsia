// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/frame_scheduler_test.h"
#include <lib/gtest/test_loop_fixture.h>
#include "garnet/lib/ui/gfx/engine/default_frame_scheduler.h"

namespace scenic_impl {
namespace gfx {
namespace test {

void FrameSchedulerTest::SetUp() {
  fake_display_ = std::make_unique<FakeDisplay>();
  mock_updater_ = std::make_unique<MockSessionUpdater>();
  mock_renderer_ = std::make_unique<MockFrameRenderer>();
  SetupDefaultVsyncValues();
}

void FrameSchedulerTest::TearDown() {
  fake_display_.reset();
  mock_updater_.reset();
  mock_renderer_.reset();
}

std::unique_ptr<DefaultFrameScheduler>
FrameSchedulerTest::CreateDefaultFrameScheduler() {
  auto scheduler = std::make_unique<DefaultFrameScheduler>(fake_display_.get());
  scheduler->SetDelegate({.session_updater = mock_updater_->GetWeakPtr(),
                          .frame_renderer = mock_renderer_->GetWeakPtr()});
  return scheduler;
}

void FrameSchedulerTest::SetupDefaultVsyncValues() {
  // Needs to be big enough so that FrameScheduler can always fit a latch point
  // in the frame.
  const auto vsync_interval = zx::msec(100).get();
  fake_display_->SetVsyncInterval(vsync_interval);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
