// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>
#include "garnet/lib/ui/gfx/tests/frame_scheduler_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

TEST_F(FrameSchedulerTest, PresentTimeZero_ShouldBeScheduledBeforeNextVsync) {
  auto scheduler = CreateDefaultFrameScheduler();

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule an update for as soon as possible.
  scheduler->ScheduleUpdateForSession(/* presentation time*/ 0,
                                      /* session id */ 1);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  mock_renderer_->EndFrame();

  // Should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest,
       PresentBiggerThanNextVsync_ShouldBeScheduledAfterNextVsync) {
  auto scheduler = CreateDefaultFrameScheduler();

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule an update for in between the next two vsyncs.
  zx_time_t time_after_vsync = fake_display_->GetLastVsyncTime() +
                          (1.5 * fake_display_->GetVsyncInterval());

  scheduler->ScheduleUpdateForSession(/* presentation time*/ time_after_vsync,
                                      /* session id */ 1);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  mock_renderer_->EndFrame();

  // Should not have been scheduled yet.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one more vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  mock_renderer_->EndFrame();

  // Should have been scheduled and handled now.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
