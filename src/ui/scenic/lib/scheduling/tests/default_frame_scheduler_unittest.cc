// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <lib/gtest/test_loop_fixture.h>

#include <gmock/gmock.h>

#include "src/ui/scenic/lib/scenic/scenic.h"
#include "src/ui/scenic/lib/scheduling/constant_frame_predictor.h"
#include "src/ui/scenic/lib/scheduling/tests/frame_scheduler_test.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace scheduling::test {

namespace {

zx::time Now() { return async::Now(async_get_default_dispatcher()); }

// A MockSessionUpdater class which executes the provided functions on every
// UpdateSessions(), OnCpuWorkDone() and OnFramePresented() call respectively.
class MockSessionUpdaterWithFunctions : public MockSessionUpdater {
 public:
  MockSessionUpdaterWithFunctions(fit::function<void()> update_sessions,
                                  fit::function<void()> on_cpu_work_done,
                                  fit::function<void()> on_frame_presented)
      : update_sessions_(std::move(update_sessions)),
        on_cpu_work_done_(std::move(on_cpu_work_done)),
        on_frame_presented_(std::move(on_frame_presented)) {}

  // |SessionUpdater|
  SessionUpdater::UpdateResults UpdateSessions(
      const std::unordered_map<SessionId, PresentId>& sessions_to_update,
      uint64_t trace_id) override {
    update_sessions_();
    return MockSessionUpdater::UpdateSessions(std::move(sessions_to_update), trace_id);
  }
  // |SessionUpdater|
  void OnCpuWorkDone() override {
    on_cpu_work_done_();
    MockSessionUpdater::OnCpuWorkDone();
  }
  // |SessionUpdater|
  void OnFramePresented(
      const std::unordered_map<SessionId, std::map<PresentId, /*latched_time*/ zx::time>>&
          latched_times,
      PresentTimestamps present_times) override {
    on_frame_presented_();
    MockSessionUpdater::OnFramePresented(std::move(latched_times), std::move(present_times));
  }

 private:
  const fit::function<void()> update_sessions_;
  const fit::function<void()> on_cpu_work_done_;
  const fit::function<void()> on_frame_presented_;
};

}  // namespace

// Schedule an update on the frame scheduler.
static void ScheduleUpdate(const std::unique_ptr<DefaultFrameScheduler>& scheduler,
                           SessionId session_id, zx::time presentation_time,
                           std::vector<zx::event> release_fences = {}, bool squashable = true) {
  scheduling::PresentId present_id =
      scheduler->RegisterPresent(session_id, std::move(release_fences));
  scheduler->ScheduleUpdateForSession(
      presentation_time, {.session_id = session_id, .present_id = present_id}, squashable);
}

// This function runs a single frame through the scheduler, updater, and renderer. It performs a
// positive test for timing behavior, confirming that the requested update (triggered at
// |presentation_time|) is not triggered before |early_time|, but has been triggered after
// |update_time|.
static void SingleRenderTest(const std::unique_ptr<DefaultFrameScheduler>& scheduler,
                             const std::shared_ptr<MockSessionUpdater>& updater,
                             const std::shared_ptr<MockFrameRenderer>& renderer,
                             async::TestLoop& loop, zx::time presentation_time, zx::time early_time,
                             zx::time update_time) {
  constexpr SessionId kSessionId = 1;

  EXPECT_EQ(updater->update_sessions_call_count(), 0u);
  EXPECT_EQ(renderer->GetNumPendingFrames(), 0u);
  EXPECT_EQ(updater->cpu_work_done_count(), 0u);

  ScheduleUpdate(scheduler, kSessionId, presentation_time);

  EXPECT_GE(early_time, Now());
  loop.RunUntil(early_time);

  EXPECT_EQ(updater->update_sessions_call_count(), 0u);
  EXPECT_EQ(renderer->GetNumPendingFrames(), 0u);
  EXPECT_EQ(updater->cpu_work_done_count(), 0u);

  EXPECT_GE(update_time, Now());
  loop.RunUntil(update_time);

  // Present should have been scheduled and handled.
  EXPECT_EQ(updater->update_sessions_call_count(), 1u);
  EXPECT_EQ(renderer->GetNumPendingFrames(), 1u);
  EXPECT_EQ(updater->cpu_work_done_count(), 1u);

  // Wait for a very long time.
  loop.RunFor(zx::sec(10));

  // No further render calls should have been made.
  EXPECT_EQ(updater->update_sessions_call_count(), 1u);
  EXPECT_EQ(renderer->GetNumPendingFrames(), 1u);
  EXPECT_EQ(updater->cpu_work_done_count(), 1u);

  // End the pending frame.
  EXPECT_EQ(updater->on_frame_presented_call_count(), 0u);
  renderer->EndFrame();
  EXPECT_EQ(renderer->GetNumPendingFrames(), 0u);
  EXPECT_EQ(updater->on_frame_presented_call_count(), 1u);
  ASSERT_EQ(updater->last_latched_times().count(kSessionId), 1u);
  EXPECT_EQ(updater->last_latched_times().at(kSessionId).size(), 1u);
  EXPECT_EQ(updater->cpu_work_done_count(), 1u);

  // Wait for a very long time.
  loop.RunFor(zx::sec(10));

  // No further render calls should have been made.
  EXPECT_EQ(updater->update_sessions_call_count(), 1u);
  EXPECT_EQ(renderer->GetNumPendingFrames(), 0u);
  EXPECT_EQ(updater->cpu_work_done_count(), 1u);
  EXPECT_EQ(updater->on_frame_presented_call_count(), 1u);
}

TEST_F(FrameSchedulerTest, PresentTimeZero_ShouldBeScheduledBeforeNextVsync) {
  auto scheduler = CreateDefaultFrameScheduler();
  SingleRenderTest(scheduler, mock_updater_, mock_renderer_, test_loop(), zx::time(0), zx::time(0),
                   zx::time(0) + vsync_timing_->vsync_interval());
}

TEST_F(FrameSchedulerTest, PresentBiggerThanNextVsync_ShouldBeScheduledAfterNextVsync) {
  auto scheduler = CreateDefaultFrameScheduler();

  // Schedule an update for in between the next two vsyncs.
  const auto vsync_interval = vsync_timing_->vsync_interval();
  const zx::time early_time = vsync_timing_->last_vsync_time() + vsync_interval;
  const zx::time update_time = vsync_timing_->last_vsync_time() + vsync_interval * 2;
  const zx::time presentation_time = early_time + (update_time - early_time) / 2;

  SingleRenderTest(scheduler, mock_updater_, mock_renderer_, test_loop(), presentation_time,
                   early_time, update_time);
}

TEST_F(FrameSchedulerTest, SinglePresent_ShouldGetSingleRenderCallExactlyOnTime) {
  auto scheduler = CreateDefaultFrameScheduler();
  // Set the LastVsyncTime arbitrarily in the future.
  //
  // We want to test our ability to schedule a frame "next time" given an arbitrary start,
  // vs in a certain duration from Now() = 0, so this makes that distinction clear.
  const auto vsync_interval = vsync_timing_->vsync_interval();
  const zx::time early_time = vsync_timing_->last_vsync_time() + vsync_interval * 6;
  const zx::time update_time = vsync_timing_->last_vsync_time() + vsync_interval * 7;
  const zx::time presentation_time = update_time;
  vsync_timing_->set_last_vsync_time(early_time);

  SingleRenderTest(scheduler, mock_updater_, mock_renderer_, test_loop(), presentation_time,
                   early_time, update_time);
}

TEST_F(FrameSchedulerTest, PresentsForTheSameFrame_ShouldGetSquashedAndSingleRenderCall) {
  auto scheduler = CreateDefaultFrameScheduler();

  // Schedule an extra update for now.
  constexpr SessionId kSessionId = 2;
  const zx::time now = Now();
  ScheduleUpdate(scheduler, kSessionId, now);
  ScheduleUpdate(scheduler, kSessionId, now);

  test_loop().RunUntil(now + vsync_timing_->vsync_interval());

  // Present should have been scheduled and applied.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 0u);

  // Present the frame.
  mock_renderer_->EndFrame();

  // The two updates should be squashed and presented together.
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 1u);
  EXPECT_EQ(mock_updater_->last_latched_times().size(), 1u);
  ASSERT_EQ(mock_updater_->last_latched_times().count(kSessionId), 1u);
  EXPECT_EQ(mock_updater_->last_latched_times().at(kSessionId).size(), 2u);
}

TEST_F(FrameSchedulerTest, SquashedPresents_ShouldScheduleForInitialPresent) {
  auto scheduler = CreateDefaultFrameScheduler();

  // Schedule two updates. The first with a later requested_presentation_time than the second. They
  // should be squashed.
  constexpr SessionId kSessionId = 1;
  ScheduleUpdate(scheduler, kSessionId, zx::time(1.5f * vsync_timing_->vsync_interval().get()));
  ScheduleUpdate(scheduler, kSessionId, zx::time(0));

  // Run loop past when a frame would have been scheduled in case update #2 was used.
  // Observe no attempt to apply changes.
  const zx::time now = Now();
  test_loop().RunUntil(now + vsync_timing_->vsync_interval());
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);

  // Wait for the requested time for update 1 to pass. Should now see an attempted update.
  test_loop().RunUntil(now + zx::duration(2 * vsync_timing_->vsync_interval().get()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);

  // Both updates should have been applied.
  mock_renderer_->EndFrame();
  EXPECT_EQ(mock_updater_->last_latched_times().size(), 1u);
  ASSERT_EQ(mock_updater_->last_latched_times().count(kSessionId), 1u);
  EXPECT_EQ(mock_updater_->last_latched_times().at(kSessionId).size(), 2u);
}

TEST_F(FrameSchedulerTest, UnsquashablePresents_ShouldNeverBeSquashed) {
  auto scheduler = CreateDefaultFrameScheduler();

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);

  // Schedule four updates with the same presentation time, but different squashability.
  constexpr SessionId kSessionId = 1;
  ScheduleUpdate(scheduler, kSessionId, zx::time(0), /*release_fences*/ {}, /*squashable=*/false);
  ScheduleUpdate(scheduler, kSessionId, zx::time(0), /*release_fences*/ {}, /*squashable=*/false);
  ScheduleUpdate(scheduler, kSessionId, zx::time(0), /*release_fences*/ {}, /*squashable=*/true);
  ScheduleUpdate(scheduler, kSessionId, zx::time(0), /*release_fences*/ {}, /*squashable=*/false);

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Present should have been scheduled and applied.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 0u);

  // Present the frame.
  mock_renderer_->EndFrame();

  // Only one update should have been applied.
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 1u);
  EXPECT_EQ(mock_updater_->last_latched_times().size(), 1u);
  ASSERT_EQ(mock_updater_->last_latched_times().count(kSessionId), 1u);
  EXPECT_EQ(mock_updater_->last_latched_times().at(kSessionId).size(), 1u);

  // Next frame should also apply a single one.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  mock_renderer_->EndFrame();
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 2u);
  EXPECT_EQ(mock_updater_->last_latched_times().size(), 1u);
  ASSERT_EQ(mock_updater_->last_latched_times().count(kSessionId), 1u);
  EXPECT_EQ(mock_updater_->last_latched_times().at(kSessionId).size(), 1u);

  // Third update is squashable, so next frame should contain update 3+4.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 3u);
  mock_renderer_->EndFrame();
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 3u);
  EXPECT_EQ(mock_updater_->last_latched_times().size(), 1u);
  ASSERT_EQ(mock_updater_->last_latched_times().count(kSessionId), 1u);
  EXPECT_EQ(mock_updater_->last_latched_times().at(kSessionId).size(), 2u);

  // All updates should have been completed.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 3u);
}

TEST_F(FrameSchedulerTest, PresentsForDifferentFrames_ShouldGetSeparateRenderCalls) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  const zx::time now = Now();
  EXPECT_EQ(now, vsync_timing_->last_vsync_time());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);

  // Schedule an update for now.
  ScheduleUpdate(scheduler, kSessionId, now);

  // Schedule an update for in between the next two vsyncs.
  const auto vsync_interval = vsync_timing_->vsync_interval();
  const zx::time early_time = vsync_timing_->last_vsync_time() + vsync_interval;
  const zx::time update_time = vsync_timing_->last_vsync_time() + vsync_interval * 2;
  const zx::time presentation_time = early_time + (update_time - early_time) / 2;

  ScheduleUpdate(scheduler, kSessionId, presentation_time);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);

  // Wait for one vsync period.
  RunLoopUntil(early_time);

  // First Present should have been scheduled and applied.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 0u);

  mock_renderer_->EndFrame();
  // First Present should have been completed.
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 1u);
  EXPECT_EQ(mock_updater_->last_latched_times().size(), 1u);
  ASSERT_EQ(mock_updater_->last_latched_times().count(kSessionId), 1u);
  EXPECT_EQ(mock_updater_->last_latched_times().at(kSessionId).size(), 1u);

  // Wait for one more vsync period.
  RunLoopUntil(update_time);

  // Second Present should have been scheduled and applied.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 1u);

  mock_renderer_->EndFrame();
  // Second Present should have been completed.
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 2u);
  EXPECT_EQ(mock_updater_->last_latched_times().size(), 1u);
  ASSERT_EQ(mock_updater_->last_latched_times().count(kSessionId), 1u);
  EXPECT_EQ(mock_updater_->last_latched_times().at(kSessionId).size(), 1u);
}

TEST_F(FrameSchedulerTest, SecondPresentDuringRender_ShouldApplyUpdatesAndReschedule) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);

  // Schedule an update for now.
  zx::time now = Now();
  ScheduleUpdate(scheduler, kSessionId, now);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);

  // Schedule another update for now.
  ScheduleUpdate(scheduler, kSessionId, now);
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Updates should be applied, but not rendered.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);

  // End previous frame.
  mock_renderer_->EndFrame();
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Second render should have occurred.
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);
}

TEST_F(FrameSchedulerTest, SignalSuccessfulPresentCallbackOnlyWhenFramePresented) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);

  // Schedule an update for now.
  zx::time now = Now();
  ScheduleUpdate(scheduler, kSessionId, now);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);

  // Schedule another update.
  ScheduleUpdate(scheduler, kSessionId, now);
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  // Next render doesn't trigger until the previous render is finished.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);

  // Drop frame #0. This should not trigger a frame presented signal.
  mock_renderer_->DropFrame();
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 0u);

  // Presenting frame #1 should trigger frame presented signal for both updates.
  mock_renderer_->EndFrame();
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 1u);
  EXPECT_EQ(mock_updater_->last_latched_times().size(), 1u);
  ASSERT_EQ(mock_updater_->last_latched_times().count(kSessionId), 1u);
  EXPECT_EQ(mock_updater_->last_latched_times().at(kSessionId).size(), 2u);
}

TEST_F(FrameSchedulerTest, FailedUpdateWithRender_ShouldNotCrash) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId1 = 1;
  mock_updater_->SetUpdateSessionsReturnValue({.sessions_with_failed_updates = {kSessionId1}});

  constexpr SessionId kSessionId2 = 2;

  uint64_t present_counts[2] = {0, 0};
  ScheduleUpdate(scheduler, kSessionId1, Now());
  ScheduleUpdate(scheduler, kSessionId2, Now());

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 0u);
  EXPECT_NO_FATAL_FAILURE(mock_renderer_->EndFrame());
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 1u);
  // TODO(): The session with the failed update should not receive an OnFramePresented call.
  EXPECT_EQ(mock_updater_->last_latched_times().size(), 2u);
  EXPECT_TRUE(mock_updater_->last_latched_times().count(kSessionId1));
  EXPECT_TRUE(mock_updater_->last_latched_times().count(kSessionId2));
}

TEST_F(FrameSchedulerTest, NoOpUpdateWithSecondPendingUpdate_ShouldBeRescheduled) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);

  ScheduleUpdate(scheduler, kSessionId, Now() + vsync_timing_->vsync_interval());
  ScheduleUpdate(scheduler, kSessionId,
                 Now() + (vsync_timing_->vsync_interval() + zx::duration(1)));

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
}

TEST_F(FrameSchedulerTest, LongRenderTime_ShouldTriggerAReschedule_WithALatePresent) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  // Guarantee the vsync interval here is what we expect.
  zx::duration interval = zx::msec(100);
  vsync_timing_->set_vsync_interval(interval);
  EXPECT_EQ(0, Now().get());

  // Schedule a frame
  ScheduleUpdate(scheduler, kSessionId, zx::time(0));

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);

  // Trigger an update
  auto update_time = zx::time(vsync_timing_->last_vsync_time() + vsync_timing_->vsync_interval());

  // Go to vsync.
  RunLoopUntil(update_time);
  vsync_timing_->set_last_vsync_time(Now());

  // Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);

  // End the frame, more than halfway through the vsync, so that the next update cannot complete in
  // time, given prediction.
  RunLoopFor(zx::msec(91));
  FrameRenderer::Timestamps timestamps;
  timestamps.render_done_time = Now();
  timestamps.actual_presentation_time = Now();
  mock_renderer_->EndFrame(timestamps);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);

  ScheduleUpdate(scheduler, kSessionId, zx::time(0));

  // Go to vsync.
  RunLoopUntil(zx::time(vsync_timing_->last_vsync_time() + vsync_timing_->vsync_interval()));
  vsync_timing_->set_last_vsync_time(Now());

  // Nothing should have been scheduled yet.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);

  // Wait for one more vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);
}

TEST_F(FrameSchedulerTest, SinglePredictedPresentation_ShouldBeReasonable) {
  auto scheduler = CreateDefaultFrameScheduler();

  zx::time next_vsync = vsync_timing_->last_vsync_time() + vsync_timing_->vsync_interval();

  // Ask for a prediction for one frame into the future.
  std::vector<scheduling::FuturePresentationInfo> predicted_presents;
  scheduler->GetFuturePresentationInfos(zx::duration(0), [&](auto future_presents) {
    predicted_presents = std::move(future_presents);
  });

  EXPECT_GE(predicted_presents.size(), 1u);
  EXPECT_EQ(predicted_presents[0].presentation_time, next_vsync);

  for (size_t i = 0; i < predicted_presents.size(); i++) {
    auto current = std::move(predicted_presents[i]);
    EXPECT_LT(current.latch_point, current.presentation_time);
    EXPECT_GE(current.latch_point, Now());
  }
}

TEST_F(FrameSchedulerTest, ArbitraryPredictedPresentation_ShouldBeReasonable) {
  // The main and only difference between this test and
  // "SinglePredictedPresentation_ShouldBeReasonable" above is that we advance the clock before
  // asking for a prediction, to ensure that GetPredictions() works in a more general sense.

  auto scheduler = CreateDefaultFrameScheduler();

  // Advance the clock to vsync1.
  zx::time vsync0 = vsync_timing_->last_vsync_time();
  zx::time vsync1 = vsync0 + vsync_timing_->vsync_interval();
  zx::time vsync = vsync1 + vsync_timing_->vsync_interval();

  EXPECT_GT(vsync_timing_->vsync_interval(), zx::duration(0));
  EXPECT_EQ(vsync0, Now());

  RunLoopUntil(vsync1);

  // Ask for a prediction.
  std::vector<scheduling::FuturePresentationInfo> predicted_presents;
  scheduler->GetFuturePresentationInfos(zx::duration(0), [&](auto future_presents) {
    predicted_presents = std::move(future_presents);
  });

  EXPECT_GE(predicted_presents.size(), 1u);
  EXPECT_EQ(predicted_presents[0].presentation_time, vsync);

  for (size_t i = 0; i < predicted_presents.size(); i++) {
    auto current = std::move(predicted_presents[i]);
    EXPECT_LT(current.latch_point, current.presentation_time);
    EXPECT_GE(current.latch_point, Now());
  }
}

TEST_F(FrameSchedulerTest, MultiplePredictedPresentations_ShouldBeReasonable) {
  auto scheduler = CreateDefaultFrameScheduler();

  zx::time vsync0 = vsync_timing_->last_vsync_time();
  zx::time vsync1 = vsync0 + vsync_timing_->vsync_interval();
  zx::time vsync = vsync1 + vsync_timing_->vsync_interval();
  zx::time vsync3 = vsync + vsync_timing_->vsync_interval();
  zx::time vsync4 = vsync3 + vsync_timing_->vsync_interval();

  // What we really want is a positive difference between each vsync.
  EXPECT_GT(vsync_timing_->vsync_interval(), zx::duration(0));

  // Ask for a prediction a few frames into the future.
  std::vector<scheduling::FuturePresentationInfo> predicted_presents;
  scheduler->GetFuturePresentationInfos(
      zx::duration((vsync4 - vsync0).get()),
      [&](auto future_presents) { predicted_presents = std::move(future_presents); });

  // Expect at least one frame of prediction.
  EXPECT_GE(predicted_presents.size(), 1u);

  auto past_prediction = std::move(predicted_presents[0]);

  for (size_t i = 0; i < predicted_presents.size(); i++) {
    auto current = std::move(predicted_presents[i]);
    EXPECT_LT(current.latch_point, current.presentation_time);
    EXPECT_GE(current.latch_point, Now());

    if (i > 0)
      EXPECT_LT(past_prediction.presentation_time, current.presentation_time);

    past_prediction = std::move(current);
  }
}

TEST_F(FrameSchedulerTest, InfinitelyLargePredictionRequest_ShouldBeTruncated) {
  auto scheduler = CreateDefaultFrameScheduler();

  zx::time next_vsync = vsync_timing_->last_vsync_time() + vsync_timing_->vsync_interval();

  // Ask for an extremely large prediction duration.
  std::vector<scheduling::FuturePresentationInfo> predicted_presents;
  scheduler->GetFuturePresentationInfos(zx::duration(INTMAX_MAX), [&](auto future_presents) {
    predicted_presents = std::move(future_presents);
  });

  constexpr static const uint64_t kOverlyLargeRequestCount = 100u;

  EXPECT_LE(predicted_presents.size(), kOverlyLargeRequestCount);
  EXPECT_EQ(predicted_presents[0].presentation_time, next_vsync);

  for (size_t i = 0; i < predicted_presents.size(); i++) {
    auto current = std::move(predicted_presents[i]);
    EXPECT_LT(current.latch_point, current.presentation_time);
    EXPECT_GE(current.latch_point, Now());
  }
}

TEST_F(FrameSchedulerTest, SessionUpdaters_ShouldBeCalledInOrder) {
  auto scheduler = std::make_unique<DefaultFrameScheduler>(
      vsync_timing_,
      std::make_unique<ConstantFramePredictor>(/* static_vsync_offset */ zx::msec(5)));

  int updater1_counter = 1;
  int updater2_counter = 4;
  std::vector<int32_t> update_call_order;
  std::vector<int32_t> cpu_work_done_order;
  std::vector<int32_t> presented_call_order;
  auto updater1 = std::make_shared<MockSessionUpdaterWithFunctions>(
      [&update_call_order, &updater1_counter] { update_call_order.push_back(updater1_counter++); },
      [&cpu_work_done_order, &updater1_counter] {
        cpu_work_done_order.push_back(updater1_counter++);
      },
      [&presented_call_order, &updater1_counter] {
        presented_call_order.push_back(updater1_counter++);
      });
  auto updater2 = std::make_shared<MockSessionUpdaterWithFunctions>(
      [&update_call_order, &updater2_counter] { update_call_order.push_back(updater2_counter++); },
      [&cpu_work_done_order, &updater2_counter] {
        cpu_work_done_order.push_back(updater2_counter++);
      },
      [&presented_call_order, &updater2_counter] {
        presented_call_order.push_back(updater2_counter++);
      });

  // Initialization order is call order, so |updater1| should always be called before |updater2|.
  scheduler->Initialize(mock_renderer_, {updater1, updater2});

  ScheduleUpdate(scheduler, /*session_id*/ 1, zx::time(0));
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  mock_renderer_->EndFrame();
  RunLoopUntilIdle();

  // The updaters should have been called in initialization order, and the functions should have
  // been called in this order.
  EXPECT_THAT(update_call_order, testing::ElementsAre(1, 4));
  EXPECT_THAT(cpu_work_done_order, testing::ElementsAre(2, 5));
  EXPECT_THAT(presented_call_order, testing::ElementsAre(3, 6));
}

// Verify that we properly observe 4 updates for all session updaters.
TEST_F(FrameSchedulerTest, MultiUpdaterMultiSession) {
  auto scheduler = std::make_unique<DefaultFrameScheduler>(
      vsync_timing_,
      std::make_unique<ConstantFramePredictor>(/* static_vsync_offset */ zx::msec(5)));

  // Pre-declare the Session IDs used in this test.
  constexpr SessionId kSession1 = 1;
  constexpr SessionId kSession2 = 2;
  constexpr SessionId kSession3 = 3;
  constexpr SessionId kSession4 = 4;
  auto updater1 = std::make_shared<MockSessionUpdater>();
  auto updater2 = std::make_shared<MockSessionUpdater>();
  scheduler->Initialize(mock_renderer_, {updater1, updater2});

  ScheduleUpdate(scheduler, kSession1, zx::time(2));
  ScheduleUpdate(scheduler, kSession2, zx::time(3));
  ScheduleUpdate(scheduler, kSession3, zx::time(4));
  ScheduleUpdate(scheduler, kSession4, zx::time(5));
  // Should still only get one combined update for each session.
  ScheduleUpdate(scheduler, kSession4, zx::time(6));

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  EXPECT_EQ(updater1->last_sessions_to_update().size(), 4u);
  EXPECT_EQ(updater2->last_sessions_to_update().size(), 4u);
  EXPECT_TRUE(updater1->last_sessions_to_update().find(kSession1) !=
              updater1->last_sessions_to_update().end());
  EXPECT_TRUE(updater1->last_sessions_to_update().find(kSession2) !=
              updater1->last_sessions_to_update().end());
  EXPECT_TRUE(updater1->last_sessions_to_update().find(kSession3) !=
              updater1->last_sessions_to_update().end());
  EXPECT_TRUE(updater1->last_sessions_to_update().find(kSession4) !=
              updater1->last_sessions_to_update().end());
  EXPECT_TRUE(updater2->last_sessions_to_update().find(kSession1) !=
              updater2->last_sessions_to_update().end());
  EXPECT_TRUE(updater2->last_sessions_to_update().find(kSession2) !=
              updater2->last_sessions_to_update().end());
  EXPECT_TRUE(updater2->last_sessions_to_update().find(kSession3) !=
              updater2->last_sessions_to_update().end());
  EXPECT_TRUE(updater2->last_sessions_to_update().find(kSession4) !=
              updater2->last_sessions_to_update().end());
}

// Tests whether the SessionUpdater::OnPresented is called at the correct times with the correct
// data.
TEST_F(FrameSchedulerTest, SessionUpdater_OnPresented_Test) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId1 = 1;
  constexpr SessionId kSessionId2 = 2;

  // Schedule a couple of updates, all of which should be handled this frame.
  ScheduleUpdate(scheduler, kSessionId1, zx::time(0));
  ScheduleUpdate(scheduler, kSessionId1, zx::time(0));
  ScheduleUpdate(scheduler, kSessionId1, zx::time(0));
  ScheduleUpdate(scheduler, kSessionId2, zx::time(0));

  // Schedule updates for next frame.
  ScheduleUpdate(scheduler, kSessionId1,
                 zx::time(0) + zx::duration(2 * vsync_timing_->vsync_interval().get()));
  ScheduleUpdate(scheduler, kSessionId2,
                 zx::time(0) + zx::duration(2 * vsync_timing_->vsync_interval().get()));

  EXPECT_TRUE(mock_updater_->last_latched_times().empty());

  RunLoopFor(vsync_timing_->vsync_interval());
  const zx::time kPresentationTime1 = Now();
  mock_renderer_->EndFrame();
  RunLoopUntilIdle();
  {
    // The first batch of updates should have been presented.
    auto result_map = mock_updater_->last_latched_times();
    auto last_presented_time = mock_updater_->last_presented_time();
    EXPECT_EQ(last_presented_time, kPresentationTime1);
    EXPECT_EQ(result_map.size(), 2u);  // Both sessions sould have updates.
    EXPECT_EQ(result_map.at(kSessionId1).size(), 3u);
    EXPECT_EQ(result_map.at(kSessionId2).size(), 1u);
    for (auto& [session_id, present_map] : result_map) {
      for (auto& [present_id, latched_time] : present_map) {
        // We don't know latched time, but it should have been set.
        EXPECT_NE(latched_time, zx::time(0));
      }
    }
  }

  // End next frame.
  RunLoopFor(zx::sec(2));
  const zx::time kPresentationTime2 = Now();
  mock_renderer_->EndFrame();
  RunLoopUntilIdle();
  {
    // The second batch of updates should have been presented.
    auto result_map = mock_updater_->last_latched_times();
    auto last_presented_time = mock_updater_->last_presented_time();
    EXPECT_EQ(last_presented_time, kPresentationTime2);
    EXPECT_EQ(result_map.size(), 2u);
    EXPECT_EQ(result_map.at(kSessionId1).size(), 1u);
    EXPECT_EQ(result_map.at(kSessionId2).size(), 1u);
    for (auto& [session_id, present_map] : result_map) {
      for (auto& [present_id, latched_time] : present_map) {
        EXPECT_NE(latched_time, zx::time(0));
      }
    }
  }
}

// Tests creating a session and calling Present several times with release fences. Fences should
// fire as the subsequent Present call is presented to the display.
// TODO(58037): Refactor these tests to use the new fence interface in a frame renderer mock.
// They're currently testing the mock's (fake) implementation (as opposed to testing inputs and
// outputs).
TEST_F(FrameSchedulerTest, ReleaseFences_ShouldBeFiredAfterSubsequentFramePresented) {
  auto scheduler = CreateDefaultFrameScheduler();
  constexpr SessionId kSession = 1;

  // Create release fences
  std::vector<zx::event> release_fences1 = utils::CreateEventArray(2);
  zx::event release_fence1 = utils::CopyEvent(release_fences1.at(0));
  zx::event release_fence2 = utils::CopyEvent(release_fences1.at(1));
  EXPECT_FALSE(utils::IsEventSignalled(release_fence1, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(utils::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));

  std::vector<zx::event> release_fences2 = utils::CreateEventArray(1);
  zx::event release_fence3 = utils::CopyEvent(release_fences2.at(0));
  EXPECT_FALSE(utils::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));

  ScheduleUpdate(scheduler, kSession, zx::time(0), std::move(release_fences1));
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  mock_renderer_->EndFrame();
  EXPECT_FALSE(utils::IsEventSignalled(release_fence1, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(utils::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(utils::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));

  ScheduleUpdate(scheduler, kSession, Now() + (vsync_timing_->vsync_interval() + zx::duration(1)),
                 std::move(release_fences2));

  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 1U);
  RunLoopFor(zx::sec(1));
  mock_renderer_->EndFrame();
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 2U);
  EXPECT_TRUE(utils::IsEventSignalled(release_fence1, ZX_EVENT_SIGNALED));
  EXPECT_TRUE(utils::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(utils::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));
}

TEST_F(FrameSchedulerTest, SquashedPresents_ShouldHaveAllPreviousFencesSignaled) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  // Create release fences
  std::vector<zx::event> release_fences1 = utils::CreateEventArray(1);
  zx::event release_fence1 = utils::CopyEvent(release_fences1.at(0));
  std::vector<zx::event> release_fences2 = utils::CreateEventArray(1);
  zx::event release_fence2 = utils::CopyEvent(release_fences2.at(0));
  std::vector<zx::event> release_fences3 = utils::CreateEventArray(1);
  zx::event release_fence3 = utils::CopyEvent(release_fences3.at(0));

  // Schedule two presents, which should be squashed. First fence should be signaled.
  ScheduleUpdate(scheduler, kSessionId, zx::time(0), std::move(release_fences1));
  ScheduleUpdate(scheduler, kSessionId, zx::time(0), std::move(release_fences2));

  // Schedule a present for later, which should not be part of the squashed presents.
  ScheduleUpdate(scheduler, kSessionId, Now() + zx::sec(2), std::move(release_fences3));

  // No fences are signalled yet.
  EXPECT_FALSE(utils::IsEventSignalled(release_fence1, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(utils::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(utils::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));

  // After 1 second, we've latched on the first two updates. The resources for the first update are
  // therefore released.
  RunLoopFor(zx::sec(1));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1U);
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 0U);
  EXPECT_TRUE(utils::IsEventSignalled(release_fence1, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(utils::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(utils::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));

  // After rendering the first frame (update 1 and 2), no new fences have been signalled.
  mock_renderer_->EndFrame();
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1U);
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 1U);
  EXPECT_EQ(mock_updater_->last_latched_times().at(kSessionId).size(), 2U);
  EXPECT_FALSE(utils::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(utils::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));

  // After two more seconds, the third update has been latched. Even though it hasn't been rendered,
  // we know we will never use the resources from the second update, so it is safe to release them.
  RunLoopFor(zx::sec(2));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2U);
  EXPECT_TRUE(utils::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(utils::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));

  // Rendering the second frame does not signal any new fences.
  mock_renderer_->EndFrame();
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 2U);
  EXPECT_EQ(mock_updater_->last_latched_times().at(kSessionId).size(), 1U);
  EXPECT_FALSE(utils::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));
}

TEST_F(FrameSchedulerTest, SkippedPresents_ShouldHaveAllPreviousFencesSignaled) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  // Create release fences
  std::vector<zx::event> release_fences1 = utils::CreateEventArray(1);
  zx::event release_fence1 = utils::CopyEvent(release_fences1.at(0));
  std::vector<zx::event> release_fences2 = utils::CreateEventArray(1);
  zx::event release_fence2 = utils::CopyEvent(release_fences2.at(0));
  std::vector<zx::event> release_fences3 = utils::CreateEventArray(1);
  zx::event release_fence3 = utils::CopyEvent(release_fences3.at(0));
  std::vector<zx::event> release_fences4 = utils::CreateEventArray(1);
  zx::event release_fence4 = utils::CopyEvent(release_fences4.at(0));

  // These will never get scheduled, but will be skipped and fences should be signaled.
  scheduler->RegisterPresent(kSessionId, std::move(release_fences1));
  scheduler->RegisterPresent(kSessionId, std::move(release_fences2));

  // Next one should be scheduled and presented. Fences should not be signaled.
  ScheduleUpdate(scheduler, kSessionId, zx::time(0), std::move(release_fences3));

  // This should never get scheduled and fences should never be signaled.
  scheduler->RegisterPresent(kSessionId, std::move(release_fences4));

  RunLoopFor(zx::sec(1));
  mock_renderer_->EndFrame();
  RunLoopUntilIdle();
  EXPECT_TRUE(utils::IsEventSignalled(release_fence1, ZX_EVENT_SIGNALED));
  EXPECT_TRUE(utils::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(utils::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(utils::IsEventSignalled(release_fence4, ZX_EVENT_SIGNALED));
}

TEST_F(FrameSchedulerTest, ReleaseFences_ShouldFireInOrder) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;

  std::vector<int> fence_order;

  // Create release fences
  std::vector<zx::event> release_fences1 = utils::CreateEventArray(1);
  zx::event release_fence1 = utils::CopyEvent(release_fences1.at(0));
  async::Wait waiter1(release_fence1.get(), ZX_EVENT_SIGNALED, 0,
                      [&fence_order](auto...) { fence_order.push_back(1); });
  waiter1.Begin(dispatcher());

  std::vector<zx::event> release_fences2 = utils::CreateEventArray(1);
  zx::event release_fence2 = utils::CopyEvent(release_fences2.at(0));
  async::Wait waiter2(release_fence2.get(), ZX_EVENT_SIGNALED, 0,
                      [&fence_order](auto...) { fence_order.push_back(2); });
  waiter2.Begin(dispatcher());

  std::vector<zx::event> release_fences3 = utils::CreateEventArray(1);
  zx::event release_fence3 = utils::CopyEvent(release_fences3.at(0));
  async::Wait waiter3(release_fence3.get(), ZX_EVENT_SIGNALED, 0,
                      [&fence_order](auto...) { fence_order.push_back(3); });
  waiter3.Begin(dispatcher());

  // These will never get scheduled, but will be skipped and fences should be signaled.
  scheduler->RegisterPresent(kSessionId, std::move(release_fences1));
  scheduler->RegisterPresent(kSessionId, std::move(release_fences2));
  scheduler->RegisterPresent(kSessionId, std::move(release_fences3));

  // Next one should be scheduled and presented, triggering signalling of previous fences.
  ScheduleUpdate(scheduler, kSessionId, zx::time(0));

  EXPECT_TRUE(fence_order.empty());
  RunLoopFor(zx::sec(1));
  EXPECT_THAT(fence_order, ::testing::ElementsAreArray({1, 2, 3}));
}

TEST_F(FrameSchedulerTest, DelayedRendering_ShouldProduceLatchedTimes) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);

  // Schedule an update for now.
  zx::time now = Now();
  ScheduleUpdate(scheduler, kSessionId, now);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);

  // Schedule 2 other updates for now, while Scenic is still rendering.
  ScheduleUpdate(scheduler, kSessionId, now);
  ScheduleUpdate(scheduler, kSessionId, now);
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Updates should be applied, but not rendered.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);

  // Schedule 2 other updates for now, again while Scenic is still rendering.
  ScheduleUpdate(scheduler, kSessionId, now);
  ScheduleUpdate(scheduler, kSessionId, now);
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Updates should be applied, but not rendered.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 3u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);

  // End previous frame.
  mock_renderer_->EndFrame();
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // We expect 1 latched time submitted in the first frame.
  EXPECT_EQ(mock_updater_->last_latched_times().at(kSessionId).size(), 1u);

  // Second render should have occurred.
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);

  // End second frame.
  mock_renderer_->EndFrame();
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // We expect 4 latched times submitted in the second frame.
  EXPECT_EQ(mock_updater_->last_latched_times().at(kSessionId).size(), 4u);
}

TEST_F(FrameSchedulerTest, RenderContinuously_ShouldCauseRenders_WithoutScheduledUpdates) {
  auto scheduler = CreateDefaultFrameScheduler();

  // No scheduled update. Run a vsync interval and observe no attempted renders.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);

  scheduler->SetRenderContinuously(true);

  // Still no scheduled updates. Run a vsync interval and observe an attempted render.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);

  // With a frame pending we should see no more attempted renders until it is completed.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 0u);

  mock_renderer_->EndFrame();
  EXPECT_EQ(mock_updater_->on_frame_presented_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);

  // With the previous frame complete, we should now see another attempted render in the next vsync
  // interval.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 1u);

  // After disabling continuous rendering we should no longer see attempted renders.
  scheduler->SetRenderContinuously(false);
  mock_renderer_->EndFrame();
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_renderer_->GetNumPendingFrames(), 0u);
}

}  // namespace scheduling::test
