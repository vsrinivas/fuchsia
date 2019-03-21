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

TEST_F(FrameSchedulerTest, SinglePresent_ShouldGetSingleRenderCall) {
  auto scheduler = CreateDefaultFrameScheduler();

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  scheduler->ScheduleUpdateForSession(Now().get(), /* session id */ 1);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  mock_renderer_->EndFrame();

  // Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // Wait for a very long time.
  RunLoopFor(zx::sec(10));
  mock_renderer_->EndFrame();

  // No further render calls should have been made.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest, PresentsForTheSameFrame_ShouldGetSingleRenderCall) {
  auto scheduler = CreateDefaultFrameScheduler();

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  SessionId session_id1 = 1;
  SessionId session_id2 = 2;

  // Schedule two updates for now.
  zx_time_t now = Now().get();
  scheduler->ScheduleUpdateForSession(now, session_id1);
  scheduler->ScheduleUpdateForSession(now, session_id2);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  mock_renderer_->EndFrame();

  // Both Presents should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  ASSERT_EQ(mock_updater_->last_requested_updates().size(), 2u);
  EXPECT_EQ(
      mock_updater_->last_requested_updates()[0].requested_presentation_time,
      now);

  // Wait for a very long time.
  RunLoopFor(zx::sec(10));
  mock_renderer_->EndFrame();

  // No further render calls should have been made.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest,
       PresentsForDifferentFrames_ShouldGetSeparateRenderCalls) {
  auto scheduler = CreateDefaultFrameScheduler();

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  SessionId session_id = 1;

  // Schedule an update for now.
  zx_time_t now = Now().get();
  scheduler->ScheduleUpdateForSession(now, session_id);

  // Schedule an update for in between the next two vsyncs.
  zx_time_t time_after_vsync = fake_display_->GetLastVsyncTime() +
                               (1.5 * fake_display_->GetVsyncInterval());
  scheduler->ScheduleUpdateForSession(time_after_vsync, session_id);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  mock_renderer_->EndFrame();

  // First Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  ASSERT_EQ(mock_updater_->last_requested_updates().size(), 1u);
  EXPECT_EQ(
      mock_updater_->last_requested_updates()[0].requested_presentation_time,
      now);

  // Wait for one more vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  mock_renderer_->EndFrame();

  // Second Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
  ASSERT_EQ(mock_updater_->last_requested_updates().size(), 1u);
  EXPECT_EQ(
      mock_updater_->last_requested_updates()[0].requested_presentation_time,
      time_after_vsync);
}

TEST_F(FrameSchedulerTest,
       SecondPresentDuringRender_ShouldApplyUpdatesAndReschedule) {
  auto scheduler = CreateDefaultFrameScheduler();

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  SessionId session_id = 1;

  // Schedule an update for now.
  zx_time_t now = Now().get();
  scheduler->ScheduleUpdateForSession(now, session_id);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // Schedule another update for now.
  scheduler->ScheduleUpdateForSession(now, session_id);

  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  // Updates should be applied, but not rendered.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // End previous frame.
  mock_renderer_->EndFrame();

  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  // Second render should have occured.
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
