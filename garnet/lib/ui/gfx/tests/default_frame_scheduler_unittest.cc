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
  mock_renderer_->EndFrame(/* frame number */ 0, Now().get());

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

  // Nothing should have been scheduled yet.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one more vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, Now().get());

  // Should have been scheduled and handled now.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest, SinglePresent_ShouldGetSingleRenderCall) {
  auto scheduler = CreateDefaultFrameScheduler();

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->ratchet_present_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);

  scheduler->ScheduleUpdateForSession(Now().get(), /* session id */ 1);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  // Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->ratchet_present_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);

  // End the pending frame.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, Now().get());
  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 1u);

  // Wait for a very long time.
  RunLoopFor(zx::sec(10));

  // No further render calls should have been made.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 1u);
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

  // Both Presents should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // End the pending frame.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, Now().get());
  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);

  // Wait for a very long time.
  RunLoopFor(zx::sec(10));

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

  // First Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, Now().get());

  // Wait for one more vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  // Second Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
}

TEST_F(FrameSchedulerTest,
       SecondPresentDuringRender_ShouldApplyUpdatesAndReschedule) {
  auto scheduler = CreateDefaultFrameScheduler();

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->ratchet_present_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);

  SessionId session_id = 1;

  // Schedule an update for now.
  zx_time_t now = Now().get();
  scheduler->ScheduleUpdateForSession(now, session_id);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->ratchet_present_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);

  // Schedule another update for now.
  scheduler->ScheduleUpdateForSession(now, session_id);
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  // Updates should be applied, but not rendered.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_updater_->ratchet_present_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);

  // End previous frame.
  mock_renderer_->EndFrame(/* frame number */ 0, Now().get());
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 1u);

  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  // Second render should have occurred.
  EXPECT_EQ(mock_updater_->ratchet_present_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
  mock_renderer_->EndFrame(/* frame number */ 1, Now().get());
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 2u);
}

TEST_F(FrameSchedulerTest, RenderCalls_ShouldNotExceed_MaxOutstandingFrames) {
  auto scheduler = CreateDefaultFrameScheduler();
  SessionId session_id = 1;
  auto maximum_allowed_render_calls = scheduler->kMaxOutstandingFrames;
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule more updates than the maximum, and signal them rendered but not
  // presented.
  zx_time_t now = Now().get();
  for (size_t i = 0; i < maximum_allowed_render_calls + 1; ++i) {
    scheduler->ScheduleUpdateForSession(now, session_id);
    // Wait for a long time
    zx::duration schedule_frame_wait(5 * fake_display_->GetVsyncInterval());
    RunLoopFor(schedule_frame_wait);

    if (mock_renderer_->render_frame_call_count() <= i) {
      break;
    }

    // Signal frame rendered.
    mock_renderer_->SignalFrameRendered(i, now + schedule_frame_wait.get());
  }

  EXPECT_LE(mock_renderer_->render_frame_call_count(),
            maximum_allowed_render_calls);
}

TEST_F(FrameSchedulerTest,
       SignalSuccessfulPresentCallbackOnlyWhenFramePresented) {
  auto scheduler = CreateDefaultFrameScheduler();
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  SessionId session_id = 1;

  // Schedule an update for now.
  zx_time_t now = Now().get();
  scheduler->ScheduleUpdateForSession(now, session_id);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // Schedule another update.
  scheduler->ScheduleUpdateForSession(now, session_id);
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  // Next render doesn't trigger until the last render is finished.
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);

  // Drop first frame. This should not trigger a frame presented signal.
  mock_renderer_->SignalFrameDropped(/* frame number */ 0);
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);

  // The frame should still have rendered on the GPU.
  mock_renderer_->SignalFrameRendered(/* frame number */ 0, Now().get());
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);
  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);

  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  // Presented frame should trigger frame presented signal.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->SignalFrameRendered(/* frame number */ 1, Now().get());
  mock_renderer_->SignalFramePresented(/* frame number */ 1, Now().get());
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 1u);
}

TEST_F(FrameSchedulerTest, FailedUpdate_ShouldNotTriggerRenderCall) {
  auto scheduler = CreateDefaultFrameScheduler();
  SessionId session_id = 1;
  mock_updater_->SetUpdateSessionsReturnValue({.needs_render = false});

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  scheduler->ScheduleUpdateForSession(Now().get(), session_id);
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
}

TEST_F(FrameSchedulerTest,
       NoOpUpdateWithSecondPendingUpdate_ShouldBeRescheduled) {
  auto scheduler = CreateDefaultFrameScheduler();
  SessionId session_id = 1;
  mock_updater_->SetUpdateSessionsReturnValue({.needs_render = false});

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  scheduler->ScheduleUpdateForSession(Now().get(), session_id);
  scheduler->ScheduleUpdateForSession(
      Now().get() + fake_display_->GetVsyncInterval(), session_id);

  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  mock_updater_->SetUpdateSessionsReturnValue({.needs_render = true});

  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
