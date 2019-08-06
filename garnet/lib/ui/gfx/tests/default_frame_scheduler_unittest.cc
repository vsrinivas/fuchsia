// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>

#include "garnet/lib/ui/gfx/tests/frame_scheduler_test.h"

namespace fuchsia {
namespace images {
inline bool operator==(const PresentationInfo& a, const PresentationInfo& b) {
  return fidl::Equals(a, b);
}
}  // namespace images
}  // namespace fuchsia

namespace scenic_impl {
namespace gfx {
namespace test {

// Schedule an update on the scheduler, and also add a callback in the mock updater which will be
// invoked when the frame is finished "rendering".
static void ScheduleUpdateAndCallback(const std::unique_ptr<DefaultFrameScheduler>& scheduler,
                                      const std::unique_ptr<MockSessionUpdater>& updater,
                                      SessionId session_id, zx::time presentation_time,
                                      zx::time acquire_fence_time = zx::time(0)) {
  scheduler->ScheduleUpdateForSession(presentation_time, session_id);
  updater->AddCallback(session_id, presentation_time, acquire_fence_time);
}

// Schedule an update on the update manager, and also add a callback in the mock updater which will
// be invoked when the frame is finished "rendering".
static std::shared_ptr<const MockSessionUpdater::CallbackStatus> ScheduleUpdateAndCallback(
    const std::unique_ptr<DefaultFrameScheduler::UpdateManager>& update_manager,
    MockSessionUpdater* updater, SessionId session_id, zx::time presentation_time,
    zx::time acquire_fence_time = zx::time(0)) {
  update_manager->ScheduleUpdate(presentation_time, session_id);
  return updater->AddCallback(session_id, presentation_time, acquire_fence_time);
}

TEST_F(FrameSchedulerTest, PresentTimeZero_ShouldBeScheduledBeforeNextVsync) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule an update for as soon as possible.
  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId, /* presentation */ zx::time(0));

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  mock_renderer_->EndFrame(/* frame number */ 0, Now());

  // Should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest, PresentBiggerThanNextVsync_ShouldBeScheduledAfterNextVsync) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  EXPECT_EQ(Now(), fake_display_->GetLastVsyncTime());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule an update for in between the next two vsyncs.
  const auto vsync_interval = fake_display_->GetVsyncInterval();
  zx::time time_after_vsync =
      fake_display_->GetLastVsyncTime() + (vsync_interval + vsync_interval / 2);

  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId,
                            /* presentation time*/ time_after_vsync);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  // Nothing should have been scheduled yet.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one more vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, Now());

  // Should have been scheduled and handled now.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest, SinglePresent_ShouldGetSingleRenderCall) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);

  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId, Now());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  // Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);

  // End the pending frame.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, Now());
  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 1u);

  // Wait for a very long time.
  RunLoopFor(zx::sec(10));

  // No further render calls should have been made.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 1u);
}

TEST_F(FrameSchedulerTest, SinglePresent_ShouldGetSingleRenderCallExactlyOnTime) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  // Set the LastVsyncTime arbitrarily in the future.
  //
  // We want to test our ability to schedule a frame "next time" given an arbitrary start,
  // vs in a certain duration from Now() = 0, so this makes that distinction clear.
  zx::time future_vsync_time =
      zx::time(fake_display_->GetLastVsyncTime() + fake_display_->GetVsyncInterval() * 6);

  fake_display_->SetLastVsyncTime(future_vsync_time);

  EXPECT_GT(fake_display_->GetLastVsyncTime(), Now());

  // Start the test.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);

  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId,
                            future_vsync_time + fake_display_->GetVsyncInterval());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopUntil(zx::time(future_vsync_time + fake_display_->GetVsyncInterval()));

  // Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);

  // End the pending frame.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, Now());
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

  constexpr SessionId kSessionId1 = 1;
  constexpr SessionId kSessionId2 = 2;

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule two updates for now.
  zx::time now = Now();
  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId1, now);
  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId2, now);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  // Both Presents should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // End the pending frame.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, Now());
  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);

  // Wait for a very long time.
  RunLoopFor(zx::sec(10));

  // No further render calls should have been made.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest, PresentsForDifferentFrames_ShouldGetSeparateRenderCalls) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  EXPECT_EQ(Now(), fake_display_->GetLastVsyncTime());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule an update for now.
  zx::time now = Now();
  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId, now);

  // Schedule an update for in between the next two vsyncs.
  const auto vsync_interval = fake_display_->GetVsyncInterval();
  zx::time time_after_vsync =
      fake_display_->GetLastVsyncTime() + vsync_interval + vsync_interval / 2;
  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId, time_after_vsync);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  // First Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, Now());

  // Wait for one more vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  // Second Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
}

TEST_F(FrameSchedulerTest, SecondPresentDuringRender_ShouldApplyUpdatesAndReschedule) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);

  // Schedule an update for now.
  zx::time now = Now();
  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId, now);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);

  // Schedule another update for now.
  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId, now);
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  // Updates should be applied, but not rendered.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);

  // End previous frame.
  mock_renderer_->EndFrame(/* frame number */ 0, Now());
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 1u);

  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));

  // Second render should have occurred.
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
  mock_renderer_->EndFrame(/* frame number */ 1, Now());
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 2u);
}

TEST_F(FrameSchedulerTest, RenderCalls_ShouldNotExceed_MaxOutstandingFrames) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  auto maximum_allowed_render_calls = scheduler->kMaxOutstandingFrames;
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule more updates than the maximum, and signal them rendered but not
  // presented.
  zx::time now = Now();
  for (size_t i = 0; i < maximum_allowed_render_calls + 1; ++i) {
    ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId, now);
    // Wait for a long time
    zx::duration schedule_frame_wait(5 * fake_display_->GetVsyncInterval().get());
    RunLoopFor(schedule_frame_wait);

    if (mock_renderer_->render_frame_call_count() <= i) {
      break;
    }

    // Signal frame rendered.
    mock_renderer_->SignalFrameCpuRendered(i, now + schedule_frame_wait);
    mock_renderer_->SignalFrameRendered(i, now + schedule_frame_wait);
  }

  EXPECT_LE(mock_renderer_->render_frame_call_count(), maximum_allowed_render_calls);
}

TEST_F(FrameSchedulerTest, SignalSuccessfulPresentCallbackOnlyWhenFramePresented) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  SessionId session_id = 1;

  // Schedule an update for now.
  zx::time now = Now();
  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId, now);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // Schedule another update.
  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId, now);
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  // Next render doesn't trigger until the previous render is finished.
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);

  // Drop frame #0. This should not trigger a frame presented signal.
  mock_renderer_->SignalFrameDropped(/* frame number */ 0);
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);

  // Frame #0 should still have rendered on the GPU; simulate this.
  mock_renderer_->SignalFrameCpuRendered(/* frame number */ 0, Now());
  mock_renderer_->SignalFrameRendered(/* frame number */ 0, Now());
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);
  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);

  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  // Presenting frame #1 should trigger frame presented signal.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->SignalFrameCpuRendered(/* frame number */ 1, Now());
  mock_renderer_->SignalFrameRendered(/* frame number */ 1, Now());
  mock_renderer_->SignalFramePresented(/* frame number */ 1, Now());
  // Both callbacks are signaled (the failed frame #0, and the successful
  // frame #1).
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
}

TEST_F(FrameSchedulerTest, FailedUpdate_ShouldNotTriggerRenderCall) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId, Now());

  mock_updater_->SuppressNeedsRendering(true);
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
}

TEST_F(FrameSchedulerTest, NoOpUpdateWithSecondPendingUpdate_ShouldBeRescheduled) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId,
                            Now() + fake_display_->GetVsyncInterval());
  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId,
                            Now() + (fake_display_->GetVsyncInterval() + zx::duration(1)));

  mock_updater_->SuppressNeedsRendering(true);
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  mock_updater_->SuppressNeedsRendering(false);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest, LowGpuRenderTime_ShouldNotMatter) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  // Guarantee the vsync interval here is what we expect.
  zx::duration interval = zx::msec(100);
  fake_display_->SetVsyncInterval(interval);
  EXPECT_EQ(0, Now().get());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);

  // Schedule a frame where the GPU render work finished before the CPU work.
  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId, Now());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Latch an early time here for the GPU rendering to finish at.
  RunLoopFor(zx::msec(91));
  auto gpu_render_time_finish = Now();

  // Go to vsync.
  RunLoopUntil(zx::time(fake_display_->GetLastVsyncTime() + fake_display_->GetVsyncInterval()));
  fake_display_->SetLastVsyncTime(Now());

  // Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 0u);

  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);

  // End the frame, at different render times.
  mock_renderer_->SignalFrameCpuRendered(/* frame number */ 0, Now());
  mock_renderer_->SignalFrameRendered(/* frame number */ 0, gpu_render_time_finish);
  mock_renderer_->SignalFramePresented(/* frame number */ 0, Now());

  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);
  EXPECT_EQ(mock_updater_->signal_successful_present_callback_count(), 1u);

  // Now we assert that we predict reasonably, given that we had 0 GPU rendering time.
  // Specifically, we should assume we will miss the upcoming frame and aim for the next
  // one, because the large render duration pushes our prediction up.
  RunLoopFor(zx::msec(91));

  // Schedule the frame just a tad too late, given the CPU render duration.
  ScheduleUpdateAndCallback(scheduler, mock_updater_, kSessionId, zx::time(0));

  // Go to vsync.
  RunLoopUntil(zx::time(fake_display_->GetLastVsyncTime() + fake_display_->GetVsyncInterval()));
  fake_display_->SetLastVsyncTime(Now());

  // Nothing should have been scheduled yet.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // Wait for one more vsync period.
  RunLoopFor(zx::duration(fake_display_->GetVsyncInterval()));
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 1, Now());

  // Should have been scheduled and handled now.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
}

// Without calling UpdateManager::RatchetPresentCallbacks(), updates can be applied but the present
// callbacks will never be invoked.
TEST(UpdateManagerTest, NoRatchetingMeansNoCallbacks) {
  auto sum = std::make_unique<DefaultFrameScheduler::UpdateManager>();

  MockSessionUpdater updater;
  sum->AddSessionUpdater(updater.GetWeakPtr());

  constexpr SessionId kSession1 = 1;

  auto status = ScheduleUpdateAndCallback(sum, &updater, kSession1, zx::time(1), zx::time(1));

  PresentationInfo info;
  info.presentation_interval = 1;
  info.presentation_time = 1;
  uint64_t frame_number = 1;

  auto [render, reschedule] = sum->ApplyUpdates(
      zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
  EXPECT_TRUE(render);
  EXPECT_FALSE(reschedule);
  EXPECT_TRUE(status->callback_passed);
  EXPECT_FALSE(status->callback_invoked);

  // Without calling RatchetPresentCallbacks(), the callbacks won't be invoked. NOTE: this wouldn't
  // happen in practice; this is just testing/documenting the behavior.
  sum->SignalPresentCallbacks(info);
  EXPECT_FALSE(status->callback_invoked);
  EXPECT_EQ(updater.signal_successful_present_callback_count(), 0U);

  // Do it a few more times to prove that we're not just lucky when the callback is finally invoked.
  sum->SignalPresentCallbacks(info);
  EXPECT_FALSE(status->callback_invoked);
  sum->SignalPresentCallbacks(info);
  EXPECT_FALSE(status->callback_invoked);
  sum->SignalPresentCallbacks(info);
  EXPECT_FALSE(status->callback_invoked);
  sum->SignalPresentCallbacks(info);
  EXPECT_FALSE(status->callback_invoked);
  EXPECT_EQ(updater.signal_successful_present_callback_count(), 0U);

  // Finally, verify that calling RatchetPresentCallbacks() allows the signal to occur.
  sum->RatchetPresentCallbacks(zx::time(info.presentation_time), frame_number);
  sum->SignalPresentCallbacks(info);
  EXPECT_TRUE(status->callback_invoked);
  EXPECT_EQ(updater.signal_successful_present_callback_count(), 1U);

  // Verify that re-signaling doesn't result in callbacks being invoked again.
  sum->SignalPresentCallbacks(info);
  EXPECT_EQ(updater.signal_successful_present_callback_count(), 1U);
}

// A really slow fence can be repeatedly rescheduled until it is ready.  It will block other updates
// from running, even if their fences are done.
TEST(UpdateManagerTest, ReallySlowFence) {
  auto sum = std::make_unique<DefaultFrameScheduler::UpdateManager>();

  MockSessionUpdater updater;
  sum->AddSessionUpdater(updater.GetWeakPtr());

  constexpr SessionId kSession1 = 1;

  auto status1 = ScheduleUpdateAndCallback(sum, &updater, kSession1, zx::time(1), zx::time(3));
  auto status2 = ScheduleUpdateAndCallback(sum, &updater, kSession1, zx::time(2), zx::time(2));
  auto status3 = ScheduleUpdateAndCallback(sum, &updater, kSession1, zx::time(3), zx::time(4));

  PresentationInfo info;
  info.presentation_interval = 1;

  // Frame 1: Blocked on first update's fences.
  info.presentation_time = 1;
  uint64_t frame_number = 1;
  {
    auto [render, reschedule] = sum->ApplyUpdates(
        zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
    EXPECT_FALSE(render);
    EXPECT_TRUE(reschedule);
  }
  EXPECT_FALSE(status1->callback_passed);
  EXPECT_FALSE(status2->callback_passed);
  EXPECT_FALSE(status3->callback_passed);

  // Frame 2: Still blocked on first update's fences.
  info.presentation_time = 2;
  frame_number = 2;
  {
    auto [render, reschedule] = sum->ApplyUpdates(
        zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
    EXPECT_FALSE(render);
    EXPECT_TRUE(reschedule);
  }
  EXPECT_FALSE(status1->callback_passed);
  EXPECT_FALSE(status2->callback_passed);
  EXPECT_FALSE(status3->callback_passed);

  // Frame 3: First two updates are unblocked, but third is blocked on fences.
  info.presentation_time = 3;
  frame_number = 3;
  {
    auto [render, reschedule] = sum->ApplyUpdates(
        zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
    EXPECT_TRUE(render);
    EXPECT_TRUE(reschedule);
  }
  EXPECT_FALSE(status3->callback_passed);
  sum->RatchetPresentCallbacks(zx::time(info.presentation_time), frame_number);
  sum->SignalPresentCallbacks(info);
  EXPECT_TRUE(status1->callback_invoked);
  EXPECT_TRUE(status2->callback_invoked);
  EXPECT_EQ(status1->presentation_info, info);
  EXPECT_EQ(status2->presentation_info, info);

  // Frame 4: The third update is unblocked, so no reschedule is required.
  info.presentation_time = 4;
  frame_number = 4;
  {
    auto [render, reschedule] = sum->ApplyUpdates(
        zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
    EXPECT_TRUE(render);
    EXPECT_FALSE(reschedule);
  }
  sum->RatchetPresentCallbacks(zx::time(info.presentation_time), frame_number);
  sum->SignalPresentCallbacks(info);
  EXPECT_TRUE(status3->callback_invoked);
  EXPECT_EQ(status3->presentation_info, info);
}

// Verify that we properly observe all 4 possible responses from ApplyUpdates() in a
// multi-session/multi-updater scenario.
TEST(UpdateManagerTest, MultiUpdaterMultiSession) {
  auto sum = std::make_unique<DefaultFrameScheduler::UpdateManager>();

  // Pre-declare the Session IDs used in this test.
  constexpr SessionId kSession1 = 1;
  constexpr SessionId kSession2 = 2;
  constexpr SessionId kSession3 = 3;
  constexpr SessionId kSession4 = 4;

  PresentationInfo info;
  info.presentation_interval = 1;

  MockSessionUpdater updater1;
  MockSessionUpdater updater2;
  sum->AddSessionUpdater(updater1.GetWeakPtr());
  sum->AddSessionUpdater(updater2.GetWeakPtr());
  updater1.BeRelaxedAboutUnexpectedSessionUpdates();
  updater2.BeRelaxedAboutUnexpectedSessionUpdates();

  // Frame 1: Too early for any to run.
  auto status1_1 = ScheduleUpdateAndCallback(sum, &updater1, kSession1, zx::time(2), zx::time(3));
  {
    uint64_t frame_number = info.presentation_time = 1;
    auto [render, reschedule] = sum->ApplyUpdates(
        zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
    EXPECT_FALSE(render);
    EXPECT_TRUE(reschedule);
  }

  // Frame 2: Blocked on first update's fences.
  {
    uint64_t frame_number = info.presentation_time = 2;
    auto [render, reschedule] = sum->ApplyUpdates(
        zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
    EXPECT_FALSE(render);
    EXPECT_TRUE(reschedule);
  }

  // Frame 3: Sessions 1,2,3 unblocked, Session 4 still blocked on fences.
  auto status2_1 = ScheduleUpdateAndCallback(sum, &updater1, kSession2, zx::time(3), zx::time(3));
  auto status3_1 = ScheduleUpdateAndCallback(sum, &updater2, kSession3, zx::time(3), zx::time(3));
  auto status4_1 = ScheduleUpdateAndCallback(sum, &updater2, kSession4, zx::time(3), zx::time(4));
  {
    uint64_t frame_number = info.presentation_time = 3;
    auto [render, reschedule] = sum->ApplyUpdates(
        zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
    EXPECT_TRUE(render);
    EXPECT_TRUE(reschedule);
  }

  // Frame 4: Session 4 unblocked (both updates).
  auto status4_2 = ScheduleUpdateAndCallback(sum, &updater2, kSession4, zx::time(4), zx::time(4));
  {
    uint64_t frame_number = info.presentation_time = 4;
    auto [render, reschedule] = sum->ApplyUpdates(
        zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
    EXPECT_TRUE(render);
    EXPECT_FALSE(reschedule);
  }

  // Frame 5: Session 4 schedules update, then dies before update applied.
  auto status4_3 = ScheduleUpdateAndCallback(sum, &updater2, kSession4, zx::time(5), zx::time(5));
  updater2.KillSession(kSession4);
  {
    uint64_t frame_number = info.presentation_time = 5;
    auto [render, reschedule] = sum->ApplyUpdates(
        zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
    EXPECT_FALSE(render);
    EXPECT_FALSE(reschedule);
  }
}

// Verify that updaters can be dynamically updated and removed.
TEST(SessionUpdaterManagerTest, DynamicUpdaterAddRemove) {
  auto sum = std::make_unique<DefaultFrameScheduler::UpdateManager>();

  // Pre-declare the Session IDs used in this test.
  constexpr SessionId kSession1 = 1;
  constexpr SessionId kSession2 = 2;
  constexpr SessionId kSession3 = 3;
  constexpr SessionId kSession4 = 3;
  constexpr SessionId kSession5 = 3;
  constexpr SessionId kSession6 = 3;

  PresentationInfo info;
  info.presentation_interval = 1;

  // Frame 1: Too early for any to run.  Even though the updater is deleted, there is still a
  // reschedule because it was too early to try to apply the updates so the manager's  Unlike the
  // "MultiUpdaterMultiSession" test above, there is no reschedule because the updater is deleted
  // before updates are applied.
  {
    auto updater1 = std::make_unique<MockSessionUpdater>();
    sum->AddSessionUpdater(updater1->GetWeakPtr());

    auto status =
        ScheduleUpdateAndCallback(sum, updater1.get(), kSession1, zx::time(2), zx::time(3));
    updater1.reset();

    uint64_t frame_number = info.presentation_time = 1;
    auto [render, reschedule] = sum->ApplyUpdates(
        zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
    EXPECT_FALSE(render);
    EXPECT_TRUE(reschedule);

    sum->RatchetPresentCallbacks(zx::time(info.presentation_time), frame_number);
    sum->SignalPresentCallbacks(info);
    EXPECT_FALSE(status->callback_passed);
  }

  // Frame 2: Schedule another update, early enough to be applied this time.  Thus, when we destroy
  // the updater before applying updates, there is no reschedule nor render.
  {
    auto updater2 = std::make_unique<MockSessionUpdater>();
    sum->AddSessionUpdater(updater2->GetWeakPtr());

    auto status =
        ScheduleUpdateAndCallback(sum, updater2.get(), kSession2, zx::time(2), zx::time(2));
    updater2.reset();

    uint64_t frame_number = info.presentation_time = 2;
    auto [render, reschedule] = sum->ApplyUpdates(
        zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
    EXPECT_FALSE(render);
    EXPECT_FALSE(reschedule);

    sum->RatchetPresentCallbacks(zx::time(info.presentation_time), frame_number);
    sum->SignalPresentCallbacks(info);
    EXPECT_FALSE(status->callback_passed);
  }

  // Frame 3: Schedule another update, again early enough to be applied.  This time we destroy it
  // after updates but before signaling present callbacks; the callback should therefore be invoked
  // (and, the scene should be rendered).
  {
    auto updater3 = std::make_unique<MockSessionUpdater>();
    sum->AddSessionUpdater(updater3->GetWeakPtr());

    auto status =
        ScheduleUpdateAndCallback(sum, updater3.get(), kSession3, zx::time(3), zx::time(3));

    uint64_t frame_number = info.presentation_time = 3;
    auto [render, reschedule] = sum->ApplyUpdates(
        zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
    EXPECT_TRUE(render);
    EXPECT_FALSE(reschedule);

    updater3.reset();
    sum->RatchetPresentCallbacks(zx::time(info.presentation_time), frame_number);
    sum->SignalPresentCallbacks(info);
    EXPECT_TRUE(status->callback_passed);
    EXPECT_TRUE(status->callback_invoked);
    EXPECT_TRUE(status->updater_disappeared);
  }

  // For the next few frames, we have multiple updaters at the same time.
  auto updater4 = std::make_unique<MockSessionUpdater>();
  auto updater5 = std::make_unique<MockSessionUpdater>();
  sum->AddSessionUpdater(updater4->GetWeakPtr());
  sum->AddSessionUpdater(updater5->GetWeakPtr());
  updater4->BeRelaxedAboutUnexpectedSessionUpdates();
  updater5->BeRelaxedAboutUnexpectedSessionUpdates();

  auto status4 =
      ScheduleUpdateAndCallback(sum, updater4.get(), kSession4, zx::time(4), zx::time(4));
  auto status5 =
      ScheduleUpdateAndCallback(sum, updater5.get(), kSession5, zx::time(4), zx::time(5));

  // Frame 4: The update for |status4| will be applied, and |status5| will be blocked on its fence
  // and rescheduled.
  {
    uint64_t frame_number = info.presentation_time = 4;
    auto [render, reschedule] = sum->ApplyUpdates(
        zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
    EXPECT_TRUE(render);
    EXPECT_TRUE(reschedule);

    sum->RatchetPresentCallbacks(zx::time(info.presentation_time), frame_number);
    sum->SignalPresentCallbacks(info);
    EXPECT_TRUE(status4->callback_passed);
    EXPECT_TRUE(status4->callback_invoked);
    EXPECT_FALSE(status4->updater_disappeared);
    EXPECT_FALSE(status5->callback_passed);
  }

  auto updater6 = std::make_unique<MockSessionUpdater>();
  sum->AddSessionUpdater(updater6->GetWeakPtr());
  updater6->BeRelaxedAboutUnexpectedSessionUpdates();
  auto status6 =
      ScheduleUpdateAndCallback(sum, updater6.get(), kSession5, zx::time(5), zx::time(5));

  // Frame 5: The updates for both |status5| and |status6| will be applied, so there will be a
  // render and no reschedule.  Destroy |updater6| before the callbacks are signaled.
  {
    uint64_t frame_number = info.presentation_time = 5;
    auto [render, reschedule] = sum->ApplyUpdates(
        zx::time(info.presentation_time), zx::duration(info.presentation_interval), frame_number);
    EXPECT_TRUE(render);
    EXPECT_FALSE(reschedule);

    sum->RatchetPresentCallbacks(zx::time(info.presentation_time), frame_number);
    // Unlike where we deleted |updater3| above, we reset after RatcherPresentCallbacks().  This one
    // is the more common case, but UpdateManager doesn't care.
    updater6.reset();
    sum->SignalPresentCallbacks(info);
    EXPECT_TRUE(status5->callback_passed);
    EXPECT_TRUE(status5->callback_invoked);
    EXPECT_TRUE(status6->callback_passed);
    EXPECT_TRUE(status6->callback_invoked);
    // As expected, we see that |updater6| was killed while |updater5| remains.
    EXPECT_FALSE(status5->updater_disappeared);
    EXPECT_TRUE(status6->updater_disappeared);
  }
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
