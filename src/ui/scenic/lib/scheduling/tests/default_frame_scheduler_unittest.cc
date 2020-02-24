// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>

#include "gmock/gmock.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"
#include "src/ui/scenic/lib/scheduling/tests/frame_scheduler_test.h"

using scheduling::Present2Info;

namespace fuchsia {
namespace images {
inline bool operator==(const fuchsia::images::PresentationInfo& a,
                       const fuchsia::images::PresentationInfo& b) {
  return fidl::Equals(a, b);
}
}  // namespace images
}  // namespace fuchsia

namespace scheduling {
namespace test {

namespace {

// A MockSessionUpdater class which executes the provided function on every
// UpdateSessions call.
//
class MockSessionUpdaterWithFunctionOnUpdate : public MockSessionUpdater {
 public:
  MockSessionUpdaterWithFunctionOnUpdate(fit::function<void()> function)
      : function_(std::move(function)) {}

  // |SessionUpdater|
  SessionUpdater::UpdateResults UpdateSessions(
      const std::unordered_map<SessionId, PresentId>& sessions_to_update,
      uint64_t trace_id) override {
    if (function_) {
      function_();
    }
    return MockSessionUpdater::UpdateSessions(std::move(sessions_to_update), trace_id);
  }

 private:
  fit::function<void()> function_;
};

}  // namespace

// Schedule an update on the scheduler, and also add a callback in the mock updater which will be
// invoked when the frame is finished "rendering".
static void ScheduleUpdateAndCallback(
    const std::unique_ptr<DefaultFrameScheduler>& scheduler, SessionId session_id,
    zx::time presentation_time, OnPresentedCallback callback = [](auto...) {},
    std::vector<zx::event> release_fences = {}) {
  scheduling::PresentId present_id =
      scheduler->RegisterPresent(session_id, std::move(callback), std::move(release_fences));
  scheduler->ScheduleUpdateForSession(presentation_time,
                                      {.session_id = session_id, .present_id = present_id});
}

// Schedule an update on the scheduler, and also add a callback in the mock updater which will be
// invoked when the frame is finished "rendering".
static void SchedulePresent2Update(const std::unique_ptr<DefaultFrameScheduler>& scheduler,
                                   SessionId session_id, zx::time presentation_time,
                                   zx::time present_received_time = zx::time(0),
                                   std::vector<zx::event> release_fences = {}) {
  std::variant<OnPresentedCallback, Present2Info> variant;
  auto& info = variant.emplace<Present2Info>(session_id);
  info.SetPresentReceivedTime(present_received_time);
  scheduling::PresentId present_id =
      scheduler->RegisterPresent(session_id, std::move(variant), std::move(release_fences));

  scheduler->ScheduleUpdateForSession(presentation_time,
                                      {.session_id = session_id, .present_id = present_id});
}

static void SetSessionUpdateFailedNotExpected(FrameScheduler* scheduler,
                                              const SessionId session_id) {
  scheduler->SetOnUpdateFailedCallbackForSession(session_id, []() {
    // Tests using this helper do not expect their session
    // update to fail. Fail the test case.
    EXPECT_FALSE(true);
  });
}

TEST_F(FrameSchedulerTest, PresentTimeZero_ShouldBeScheduledBeforeNextVsync) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule an update for as soon as possible.
  ScheduleUpdateAndCallback(scheduler, kSessionId, /* presentation */ zx::time(0));

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());

  // Should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest, Present2WithTimeZero_ShouldBeScheduledBeforeNextVsync) {
  auto scheduler = CreateDefaultFrameScheduler();
  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);
  scheduler->SetOnFramePresentedCallbackForSession(kSessionId, [](auto) {});

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule an update for as soon as possible.
  SchedulePresent2Update(scheduler, kSessionId, /* presentation */ zx::time(0));

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());

  // Should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest, PresentBiggerThanNextVsync_ShouldBeScheduledAfterNextVsync) {
  auto scheduler = CreateDefaultFrameScheduler();
  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  EXPECT_EQ(Now(), vsync_timing_->last_vsync_time());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule an update for in between the next two vsyncs.
  const auto vsync_interval = vsync_timing_->vsync_interval();
  zx::time time_after_vsync =
      vsync_timing_->last_vsync_time() + (vsync_interval + vsync_interval / 2);

  ScheduleUpdateAndCallback(scheduler, kSessionId,
                            /* presentation time*/ time_after_vsync);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Nothing should have been scheduled yet.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one more vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());

  // Should have been scheduled and handled now.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest, Present2BiggerThanNextVsync_ShouldBeScheduledAfterNextVsync) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);
  scheduler->SetOnFramePresentedCallbackForSession(kSessionId, [](auto) {});

  EXPECT_EQ(Now(), vsync_timing_->last_vsync_time());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule an update for in between the next two vsyncs.
  const auto vsync_interval = vsync_timing_->vsync_interval();
  zx::time time_after_vsync =
      vsync_timing_->last_vsync_time() + (vsync_interval + vsync_interval / 2);

  SchedulePresent2Update(scheduler, kSessionId,
                         /* presentation time*/ time_after_vsync);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Nothing should have been scheduled yet.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one more vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());

  // Should have been scheduled and handled now.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest, SinglePresent_ShouldGetSingleRenderCall) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  ScheduleUpdateAndCallback(scheduler, kSessionId, Now());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // End the pending frame.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);

  // Wait for a very long time.
  RunLoopFor(zx::sec(10));

  // No further render calls should have been made.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest, SinglePresent2_ShouldGetSingleRenderCall) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  uint64_t present_count = 0;
  scheduler->SetOnFramePresentedCallbackForSession(
      kSessionId, [&present_count](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        present_count += info.presentation_infos.size();
      });

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  SchedulePresent2Update(scheduler, kSessionId,
                         /* presentation time*/ Now());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  EXPECT_EQ(present_count, 0u);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(present_count, 0u);

  // End the pending frame.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);
  EXPECT_EQ(present_count, 1u);

  // Wait for a very long time.
  RunLoopFor(zx::sec(10));

  // No further render calls should have been made.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(present_count, 1u);
}

TEST_F(FrameSchedulerTest, SinglePresent_ShouldGetSingleRenderCallExactlyOnTime) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  // Set the LastVsyncTime arbitrarily in the future.
  //
  // We want to test our ability to schedule a frame "next time" given an arbitrary start,
  // vs in a certain duration from Now() = 0, so this makes that distinction clear.
  zx::time future_vsync_time =
      zx::time(vsync_timing_->last_vsync_time() + vsync_timing_->vsync_interval() * 6);

  vsync_timing_->set_last_vsync_time(future_vsync_time);

  EXPECT_GT(vsync_timing_->last_vsync_time(), Now());

  // Start the test.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  ScheduleUpdateAndCallback(scheduler, kSessionId,
                            future_vsync_time + vsync_timing_->vsync_interval());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopUntil(zx::time(future_vsync_time + vsync_timing_->vsync_interval()));

  // Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // End the pending frame.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);

  // Wait for a very long time.
  RunLoopFor(zx::sec(10));

  // No further render calls should have been made.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest, SinglePresent2_ShouldGetSingleRenderCallExactlyOnTime) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);
  uint64_t present_count = 0;
  scheduler->SetOnFramePresentedCallbackForSession(
      kSessionId, [&present_count](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        present_count += info.presentation_infos.size();
      });

  // Set the LastVsyncTime arbitrarily in the future.
  //
  // We want to test our ability to schedule a frame "next time" given an arbitrary start,
  // vs in a certain duration from Now() = 0, so this makes that distinction clear.
  zx::time future_vsync_time =
      zx::time(vsync_timing_->last_vsync_time() + vsync_timing_->vsync_interval() * 6);

  vsync_timing_->set_last_vsync_time(future_vsync_time);

  EXPECT_GT(vsync_timing_->last_vsync_time(), Now());

  // Start the test.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  EXPECT_EQ(present_count, 0u);

  SchedulePresent2Update(scheduler, kSessionId,
                         future_vsync_time + vsync_timing_->vsync_interval());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopUntil(zx::time(future_vsync_time + vsync_timing_->vsync_interval()));

  // Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(present_count, 0u);

  // End the pending frame.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);
  EXPECT_EQ(present_count, 1u);

  // Wait for a very long time.
  RunLoopFor(zx::sec(10));

  // No further render calls should have been made.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(present_count, 1u);
}

TEST_F(FrameSchedulerTest, SinglePresent2_ShouldGetPresentCallbackWhenNoContentToRender) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);
  uint64_t present_count = 0;
  scheduler->SetOnFramePresentedCallbackForSession(
      kSessionId, [&present_count](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        present_count += info.presentation_infos.size();
      });

  // Set the LastVsyncTime arbitrarily in the future.
  //
  // We want to test our ability to schedule a frame "next time" given an arbitrary start,
  // vs in a certain duration from Now() = 0, so this makes that distinction clear.
  zx::time future_vsync_time =
      zx::time(vsync_timing_->last_vsync_time() + vsync_timing_->vsync_interval() * 6);

  vsync_timing_->set_last_vsync_time(future_vsync_time);

  EXPECT_GT(vsync_timing_->last_vsync_time(), Now());

  mock_renderer_->SetRenderFrameReturnValue(scheduling::RenderFrameResult::kNoContentToRender);

  // Start the test.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  EXPECT_EQ(present_count, 0u);

  SchedulePresent2Update(scheduler, kSessionId,
                         future_vsync_time + vsync_timing_->vsync_interval());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopUntil(zx::time(future_vsync_time + vsync_timing_->vsync_interval()));

  // Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);

  EXPECT_EQ(present_count, 1u);
}

TEST_F(FrameSchedulerTest, PresentsForTheSameFrame_ShouldGetSingleRenderCall) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId1 = 1;
  constexpr SessionId kSessionId2 = 2;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId1);
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId2);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule two updates for now.
  zx::time now = Now();
  ScheduleUpdateAndCallback(scheduler, kSessionId1, now);
  ScheduleUpdateAndCallback(scheduler, kSessionId2, now);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Both Presents should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // End the pending frame.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);

  // Wait for a very long time.
  RunLoopFor(zx::sec(10));

  // No further render calls should have been made.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
}

TEST_F(FrameSchedulerTest, Present2sForTheSameFrame_ShouldGetSingleRenderCall) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId1 = 1;
  constexpr SessionId kSessionId2 = 2;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId1);
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId2);
  scheduler->SetOnFramePresentedCallbackForSession(kSessionId1, [](auto) {});
  scheduler->SetOnFramePresentedCallbackForSession(kSessionId2, [](auto) {});

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule two updates for now.
  zx::time now = Now();
  SchedulePresent2Update(scheduler, kSessionId1, now);
  SchedulePresent2Update(scheduler, kSessionId2, now);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Both Presents should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // End the pending frame.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
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
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  EXPECT_EQ(Now(), vsync_timing_->last_vsync_time());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule an update for now.
  zx::time now = Now();
  ScheduleUpdateAndCallback(scheduler, kSessionId, now);

  // Schedule an update for in between the next two vsyncs.
  const auto vsync_interval = vsync_timing_->vsync_interval();
  zx::time time_after_vsync =
      vsync_timing_->last_vsync_time() + vsync_interval + vsync_interval / 2;
  ScheduleUpdateAndCallback(scheduler, kSessionId, time_after_vsync);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // First Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());

  // Wait for one more vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Second Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
}

TEST_F(FrameSchedulerTest, Present2sForDifferentFrames_ShouldGetSeparateRenderCalls) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);
  scheduler->SetOnFramePresentedCallbackForSession(kSessionId, [](auto) {});

  EXPECT_EQ(Now(), vsync_timing_->last_vsync_time());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule an update for now.
  zx::time now = Now();
  SchedulePresent2Update(scheduler, kSessionId, now);

  // Schedule an update for in between the next two vsyncs.
  const auto vsync_interval = vsync_timing_->vsync_interval();
  zx::time time_after_vsync = now + vsync_interval + vsync_interval / 2;
  SchedulePresent2Update(scheduler, kSessionId, time_after_vsync);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // First Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());

  // Wait for one more vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Second Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
}

TEST_F(FrameSchedulerTest, SecondPresentDuringRender_ShouldApplyUpdatesAndReschedule) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule an update for now.
  zx::time now = Now();
  ScheduleUpdateAndCallback(scheduler, kSessionId, now);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);

  // Schedule another update for now.
  ScheduleUpdateAndCallback(scheduler, kSessionId, now);
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Updates should be applied, but not rendered.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // End previous frame.
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Second render should have occurred.
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
  mock_renderer_->EndFrame(/* frame number */ 1, Now());
}

TEST_F(FrameSchedulerTest, SecondPresent2DuringRender_ShouldApplyUpdatesAndReschedule) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);
  uint64_t present_count = 0;
  scheduler->SetOnFramePresentedCallbackForSession(
      kSessionId, [&present_count](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        present_count += info.presentation_infos.size();
      });

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  EXPECT_EQ(present_count, 0u);

  // Schedule an update for now.
  zx::time now = Now();
  SchedulePresent2Update(scheduler, kSessionId, now);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  EXPECT_EQ(present_count, 0u);

  // Schedule another update for now.
  SchedulePresent2Update(scheduler, kSessionId, now);
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Updates should be applied, but not rendered.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(present_count, 0u);

  // End previous frame.
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
  EXPECT_EQ(present_count, 1u);

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  // Second render should have occurred.
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
  mock_renderer_->EndFrame(/* frame number */ 1, Now());
  EXPECT_EQ(present_count, 2u);
}

TEST_F(FrameSchedulerTest, RenderCalls_ShouldNotExceed_MaxOutstandingFrames) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  auto maximum_allowed_render_calls = scheduler->kMaxOutstandingFrames;
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule more updates than the maximum, and signal them rendered but not
  // presented.
  zx::time now = Now();
  for (size_t i = 0; i < maximum_allowed_render_calls + 1; ++i) {
    ScheduleUpdateAndCallback(scheduler, kSessionId, now);
    // Wait for a long time
    zx::duration schedule_frame_wait(5 * vsync_timing_->vsync_interval().get());
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

TEST_F(FrameSchedulerTest, Present2RenderCalls_ShouldNotExceed_MaxOutstandingFrames) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  auto maximum_allowed_render_calls = scheduler->kMaxOutstandingFrames;
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule more updates than the maximum, and signal them rendered but not
  // presented.
  zx::time now = Now();
  for (size_t i = 0; i < maximum_allowed_render_calls + 1; ++i) {
    SchedulePresent2Update(scheduler, kSessionId, now);
    // Wait for a long time
    zx::duration schedule_frame_wait(5 * vsync_timing_->vsync_interval().get());
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
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  SessionId session_id = 1;

  // Schedule an update for now.
  zx::time now = Now();
  ScheduleUpdateAndCallback(scheduler, kSessionId, now);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // Schedule another update.
  ScheduleUpdateAndCallback(scheduler, kSessionId, now);
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  // Next render doesn't trigger until the previous render is finished.
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);

  // Drop frame #0. This should not trigger a frame presented signal.
  mock_renderer_->SignalFrameDropped(/* frame number */ 0);
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);

  // Frame #0 should still have rendered on the GPU; simulate this.
  mock_renderer_->SignalFrameCpuRendered(/* frame number */ 0, Now());
  mock_renderer_->SignalFrameRendered(/* frame number */ 0, Now());
  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  // Presenting frame #1 should trigger frame presented signal.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->SignalFrameCpuRendered(/* frame number */ 1, Now());
  mock_renderer_->SignalFrameRendered(/* frame number */ 1, Now());
  mock_renderer_->SignalFramePresented(/* frame number */ 1, Now());
  // Both callbacks are signaled (the failed frame #0, and the successful
  // frame #1).
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
}

TEST_F(FrameSchedulerTest, SignalSuccessfulPresent2CallbackOnlyWhenFramePresented) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);
  uint64_t present_count = 0;
  scheduler->SetOnFramePresentedCallbackForSession(
      kSessionId, [&present_count](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        present_count += info.presentation_infos.size();
      });

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(present_count, 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  SessionId session_id = 1;

  // Schedule an update for now.
  zx::time now = Now();
  SchedulePresent2Update(scheduler, kSessionId, now);

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // Schedule another update.
  SchedulePresent2Update(scheduler, kSessionId, now);
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  // Next render doesn't trigger until the previous render is finished.
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);

  // Drop frame #0. This should not trigger a frame presented signal.
  mock_renderer_->SignalFrameDropped(/* frame number */ 0);
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(present_count, 0u);
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);

  // Frame #0 should still have rendered on the GPU; simulate this.
  mock_renderer_->SignalFrameCpuRendered(/* frame number */ 0, Now());
  mock_renderer_->SignalFrameRendered(/* frame number */ 0, Now());
  EXPECT_EQ(present_count, 0u);
  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  // Presenting frame #1 should trigger frame presented signal.
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->SignalFrameCpuRendered(/* frame number */ 1, Now());
  mock_renderer_->SignalFrameRendered(/* frame number */ 1, Now());
  mock_renderer_->SignalFramePresented(/* frame number */ 1, Now());
  // Both callbacks are signaled (the failed frame #0, and the successful
  // frame #1).
  EXPECT_EQ(present_count, 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
}

TEST_F(FrameSchedulerTest, FailedUpdateWithRender_ShouldNotCrash) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId1 = 1;
  bool update_failed1 = false;
  scheduler->SetOnUpdateFailedCallbackForSession(
      kSessionId1, [&update_failed1, id = kSessionId1, frame_scheduler = scheduler.get()]() {
        update_failed1 = true;
        // Remove session from FrameScheduler when the update fails.
        frame_scheduler->RemoveSession(id);
      });
  mock_updater_->SetUpdateSessionsReturnValue({.sessions_with_failed_updates = {kSessionId1}});

  constexpr SessionId kSessionId2 = 2;
  bool update_failed2 = false;
  scheduler->SetOnUpdateFailedCallbackForSession(kSessionId2,
                                                 [&update_failed2]() { update_failed2 = true; });

  ScheduleUpdateAndCallback(scheduler, kSessionId1, Now());
  ScheduleUpdateAndCallback(scheduler, kSessionId2, Now());

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_TRUE(update_failed1);
  EXPECT_FALSE(update_failed2);
}

TEST_F(FrameSchedulerTest, FailedPresent2UpdateWithRender_ShouldNotCrash) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId1 = 1;
  bool update_failed1 = false;
  scheduler->SetOnUpdateFailedCallbackForSession(
      kSessionId1, [&update_failed1, id = kSessionId1, frame_scheduler = scheduler.get()]() {
        update_failed1 = true;
        // Remove session from FrameScheduler when the update fails.
        frame_scheduler->RemoveSession(id);
      });
  mock_updater_->SetUpdateSessionsReturnValue({.sessions_with_failed_updates = {kSessionId1}});

  constexpr SessionId kSessionId2 = 2;
  bool update_failed2 = false;
  scheduler->SetOnUpdateFailedCallbackForSession(kSessionId2,
                                                 [&update_failed2]() { update_failed2 = true; });
  scheduler->SetOnFramePresentedCallbackForSession(kSessionId2, [](auto...) {});

  SchedulePresent2Update(scheduler, kSessionId1, Now());
  SchedulePresent2Update(scheduler, kSessionId2, Now());

  EXPECT_NO_FATAL_FAILURE(RunLoopFor(zx::duration(vsync_timing_->vsync_interval())));
  EXPECT_TRUE(update_failed1);
  EXPECT_FALSE(update_failed2);
  EXPECT_NO_FATAL_FAILURE(mock_renderer_->EndFrame(/* frame number */ 0, Now()));
  EXPECT_NO_FATAL_FAILURE(RunLoopFor(zx::duration(vsync_timing_->vsync_interval())));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_TRUE(update_failed1);
  EXPECT_FALSE(update_failed2);
}

TEST_F(FrameSchedulerTest, NoOpUpdateWithSecondPendingUpdate_ShouldBeRescheduled) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  ScheduleUpdateAndCallback(scheduler, kSessionId, Now() + vsync_timing_->vsync_interval());
  ScheduleUpdateAndCallback(scheduler, kSessionId,
                            Now() + (vsync_timing_->vsync_interval() + zx::duration(1)));

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
}

TEST_F(FrameSchedulerTest, NoOpPresent2UpdateWithSecondPendingUpdate_ShouldBeRescheduled) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);
  scheduler->SetOnFramePresentedCallbackForSession(kSessionId, [](auto) {});

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  SchedulePresent2Update(scheduler, kSessionId, Now() + vsync_timing_->vsync_interval());
  SchedulePresent2Update(scheduler, kSessionId,
                         Now() + (vsync_timing_->vsync_interval() + zx::duration(1)));

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
}

TEST_F(FrameSchedulerTest, LowGpuRenderTime_ShouldNotMatter) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  // Guarantee the vsync interval here is what we expect.
  zx::duration interval = zx::msec(100);
  vsync_timing_->set_vsync_interval(interval);
  EXPECT_EQ(0, Now().get());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule a frame where the GPU render work finished before the CPU work.
  ScheduleUpdateAndCallback(scheduler, kSessionId, Now());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Latch an early time here for the GPU rendering to finish at.
  RunLoopFor(zx::msec(91));
  auto gpu_render_time_finish = Now();

  // Go to vsync.
  RunLoopUntil(zx::time(vsync_timing_->last_vsync_time() + vsync_timing_->vsync_interval()));
  vsync_timing_->set_last_vsync_time(Now());

  // Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);

  // End the frame, at different render times.
  mock_renderer_->SignalFrameCpuRendered(/* frame number */ 0, Now());
  mock_renderer_->SignalFrameRendered(/* frame number */ 0, gpu_render_time_finish);
  mock_renderer_->SignalFramePresented(/* frame number */ 0, Now());

  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);

  // Now we assert that we predict reasonably, given that we had 0 GPU rendering time.
  // Specifically, we should assume we will miss the upcoming frame and aim for the next
  // one, because the large render duration pushes our prediction up.
  RunLoopFor(zx::msec(91));

  // Schedule the frame just a tad too late, given the CPU render duration.
  ScheduleUpdateAndCallback(scheduler, kSessionId, zx::time(0));

  // Go to vsync.
  RunLoopUntil(zx::time(vsync_timing_->last_vsync_time() + vsync_timing_->vsync_interval()));
  vsync_timing_->set_last_vsync_time(Now());

  // Nothing should have been scheduled yet.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // Wait for one more vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 1, Now());

  // Should have been scheduled and handled now.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
}

TEST_F(FrameSchedulerTest, LowPresent2GpuRenderTime_ShouldNotMatter) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);
  uint64_t present_count = 0;
  scheduler->SetOnFramePresentedCallbackForSession(
      kSessionId, [&present_count](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        present_count += info.presentation_infos.size();
      });

  // Guarantee the vsync interval here is what we expect.
  zx::duration interval = zx::msec(100);
  vsync_timing_->set_vsync_interval(interval);
  EXPECT_EQ(0, Now().get());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);
  EXPECT_EQ(present_count, 0u);

  // Schedule a frame where the GPU render work finished before the CPU work.
  SchedulePresent2Update(scheduler, kSessionId, Now());

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Latch an early time here for the GPU rendering to finish at.
  RunLoopFor(zx::msec(91));
  auto gpu_render_time_finish = Now();

  // Go to vsync.
  RunLoopUntil(zx::time(vsync_timing_->last_vsync_time() + vsync_timing_->vsync_interval()));
  vsync_timing_->set_last_vsync_time(Now());

  // Present should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_updater_->prepare_frame_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(present_count, 0u);

  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);

  // End the frame, at different render times.
  mock_renderer_->SignalFrameCpuRendered(/* frame number */ 0, Now());
  mock_renderer_->SignalFrameRendered(/* frame number */ 0, gpu_render_time_finish);
  mock_renderer_->SignalFramePresented(/* frame number */ 0, Now());

  EXPECT_EQ(mock_renderer_->pending_frames(), 0u);
  EXPECT_EQ(present_count, 1u);

  // Now we assert that we predict reasonably, given that we had 0 GPU rendering time.
  // Specifically, we should assume we will miss the upcoming frame and aim for the next
  // one, because the large render duration pushes our prediction up.
  RunLoopFor(zx::msec(91));

  // Schedule the frame just a tad too late, given the CPU render duration.
  SchedulePresent2Update(scheduler, kSessionId, zx::time(0));

  // Go to vsync.
  RunLoopUntil(zx::time(vsync_timing_->last_vsync_time() + vsync_timing_->vsync_interval()));
  vsync_timing_->set_last_vsync_time(Now());

  // Nothing should have been scheduled yet.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);

  // Wait for one more vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(mock_renderer_->pending_frames(), 1u);
  mock_renderer_->EndFrame(/* frame number */ 1, Now());

  // Should have been scheduled and handled now.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 2u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 2u);
}

TEST_F(FrameSchedulerTest, PresentAndPresent2Clients_CanCoexist) {
  auto scheduler = CreateDefaultFrameScheduler();

  // Present client.
  constexpr SessionId kSessionId1 = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId1);

  // Present2 client.
  constexpr SessionId kSessionId2 = 2;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId2);
  uint64_t present_count = 0;
  scheduler->SetOnFramePresentedCallbackForSession(
      kSessionId2, [&present_count](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        present_count += info.presentation_infos.size();
      });

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule updates on both clients.
  ScheduleUpdateAndCallback(scheduler, kSessionId1, zx::time(0));
  SchedulePresent2Update(scheduler, kSessionId2, zx::time(0));

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());

  // Both Present and Present2 should have been scheduled and handled.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(present_count, 1u);
}

TEST_F(FrameSchedulerTest, MultiplePresent2Clients) {
  auto scheduler = CreateDefaultFrameScheduler();

  // All three clients will call Present2 four times, one after the other.
  constexpr uint64_t kNumPresents = 4;
  constexpr uint64_t kNumClients = 3;

  constexpr SessionId kSessionId1 = 0;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId1);
  uint64_t present_count1 = 0;
  scheduler->SetOnFramePresentedCallbackForSession(
      kSessionId1,
      [&present_count1, kNumPresents](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        EXPECT_EQ(info.presentation_infos.size(), kNumPresents);
        present_count1 += info.presentation_infos.size();

        for (uint64_t i = 0; i < info.presentation_infos.size(); ++i)
          EXPECT_EQ(info.presentation_infos[i].present_received_time(),
                    static_cast<zx_time_t>(kSessionId1));
      });

  constexpr SessionId kSessionId2 = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId2);
  uint64_t present_count2 = 0;
  scheduler->SetOnFramePresentedCallbackForSession(
      kSessionId2,
      [&present_count2, kNumPresents](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        EXPECT_EQ(info.presentation_infos.size(), kNumPresents);
        present_count2 += info.presentation_infos.size();

        for (uint64_t i = 0; i < info.presentation_infos.size(); ++i)
          EXPECT_EQ(info.presentation_infos[i].present_received_time(),
                    static_cast<zx_time_t>(kSessionId2));
      });

  constexpr SessionId kSessionId3 = 2;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId3);
  uint64_t present_count3 = 0;
  scheduler->SetOnFramePresentedCallbackForSession(
      kSessionId3,
      [&present_count3, kNumPresents](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        EXPECT_EQ(info.presentation_infos.size(), kNumPresents);
        present_count3 += info.presentation_infos.size();

        for (uint64_t i = 0; i < info.presentation_infos.size(); ++i)
          EXPECT_EQ(info.presentation_infos[i].present_received_time(),
                    static_cast<zx_time_t>(kSessionId3));
      });

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule interspersed updates on all clients, with the latched_time being the session_id, so we
  // can differentiate between them in the OnFramePresented callbacks.
  for (uint64_t i = 0; i < kNumPresents; ++i) {
    for (SessionId session_id = 0; session_id < kNumClients; ++session_id) {
      SchedulePresent2Update(scheduler, session_id, /*presentation_time=*/zx::time(0),
                             /*present_received_time=*/zx::time(session_id));
    }
  }

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());

  // All Present2s should have been scheduled and handled in one go.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(present_count1, kNumPresents);
  EXPECT_EQ(present_count2, kNumPresents);
  EXPECT_EQ(present_count3, kNumPresents);
}

TEST_F(FrameSchedulerTest, CoalescedPresent2s_CauseASingleOnFramePresentedEvent) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId2 = 1;
  uint64_t present_count = 0;
  constexpr uint64_t kNumPresents = 4;

  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId2);

  scheduler->SetOnFramePresentedCallbackForSession(
      kSessionId2,
      [&present_count, kNumPresents](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        EXPECT_EQ(info.presentation_infos.size(), kNumPresents);
        present_count += info.presentation_infos.size();
      });

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule updates on both clients.
  for (uint64_t i = 0; i < kNumPresents; ++i)
    SchedulePresent2Update(scheduler, kSessionId2, zx::time(0));

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());

  // All Present2s should have been scheduled and handled in one go.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(present_count, kNumPresents);
}

TEST_F(FrameSchedulerTest, OnFramePresentedEvent_HasPresent2sInOrder) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId2 = 1;
  uint64_t present_count = 0;
  constexpr uint64_t kNumPresents = 4;

  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId2);

  // Present in reverse order. This is to ensure that the Presents are ordered by submission, not
  // necessarily latch point or present received values.
  constexpr zx::time original_present_received_time = zx::time(4);
  constexpr zx::duration present_delta = zx::duration(-1);

  scheduler->SetOnFramePresentedCallbackForSession(
      kSessionId2, [&present_count, kNumPresents, original_present_received_time,
                    present_delta](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        EXPECT_EQ(info.presentation_infos.size(), kNumPresents);
        present_count += info.presentation_infos.size();

        zx::time present_received_time = original_present_received_time;
        for (uint64_t i = 0; i < info.presentation_infos.size(); ++i) {
          EXPECT_EQ(info.presentation_infos[i].present_received_time(),
                    present_received_time.get());

          present_received_time += present_delta;
        }
      });

  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 0u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 0u);

  // Schedule updates, changing the present received time for each one.
  zx::time present_received_time = original_present_received_time;
  for (uint64_t i = 0; i < kNumPresents; ++i) {
    SchedulePresent2Update(scheduler, kSessionId2, /*presentation_time=*/zx::time(0),
                           present_received_time);

    present_received_time += present_delta;
  }

  // Wait for one vsync period.
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());

  // All Present2s should have been scheduled and handled in one go.
  EXPECT_EQ(mock_updater_->update_sessions_call_count(), 1u);
  EXPECT_EQ(mock_renderer_->render_frame_call_count(), 1u);
  EXPECT_EQ(present_count, kNumPresents);
}

TEST_F(FrameSchedulerTest, SinglePredictedPresentation_ShouldBeReasonable) {
  auto scheduler = CreateDefaultFrameScheduler();

  zx::time next_vsync = vsync_timing_->last_vsync_time() + vsync_timing_->vsync_interval();

  // Ask for a prediction for one frame into the future.
  std::vector<fuchsia::scenic::scheduling::PresentationInfo> predicted_presents;
  scheduler->GetFuturePresentationInfos(zx::duration(0), [&](auto future_presents) {
    predicted_presents = std::move(future_presents);
  });

  EXPECT_GE(predicted_presents.size(), 1u);
  EXPECT_EQ(predicted_presents[0].presentation_time(), next_vsync.get());

  for (size_t i = 0; i < predicted_presents.size(); i++) {
    auto current = std::move(predicted_presents[i]);
    EXPECT_LT(current.latch_point(), current.presentation_time());
    EXPECT_GE(current.latch_point(), Now().get());
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
  zx::time vsync2 = vsync1 + vsync_timing_->vsync_interval();

  EXPECT_GT(vsync_timing_->vsync_interval(), zx::duration(0));
  EXPECT_EQ(vsync0, Now());

  RunLoopUntil(vsync1);

  // Ask for a prediction.
  std::vector<fuchsia::scenic::scheduling::PresentationInfo> predicted_presents;
  scheduler->GetFuturePresentationInfos(zx::duration(0), [&](auto future_presents) {
    predicted_presents = std::move(future_presents);
  });

  EXPECT_GE(predicted_presents.size(), 1u);
  EXPECT_EQ(predicted_presents[0].presentation_time(), vsync2.get());

  for (size_t i = 0; i < predicted_presents.size(); i++) {
    auto current = std::move(predicted_presents[i]);
    EXPECT_LT(current.latch_point(), current.presentation_time());
    EXPECT_GE(current.latch_point(), Now().get());
  }
}

TEST_F(FrameSchedulerTest, MultiplePredictedPresentations_ShouldBeReasonable) {
  auto scheduler = CreateDefaultFrameScheduler();

  zx::time vsync0 = vsync_timing_->last_vsync_time();
  zx::time vsync1 = vsync0 + vsync_timing_->vsync_interval();
  zx::time vsync2 = vsync1 + vsync_timing_->vsync_interval();
  zx::time vsync3 = vsync2 + vsync_timing_->vsync_interval();
  zx::time vsync4 = vsync3 + vsync_timing_->vsync_interval();

  // What we really want is a positive difference between each vsync.
  EXPECT_GT(vsync_timing_->vsync_interval(), zx::duration(0));

  // Ask for a prediction a few frames into the future.
  std::vector<fuchsia::scenic::scheduling::PresentationInfo> predicted_presents;
  scheduler->GetFuturePresentationInfos(
      zx::duration((vsync4 - vsync0).get()),
      [&](auto future_presents) { predicted_presents = std::move(future_presents); });

  // Expect at least one frame of prediction.
  EXPECT_GE(predicted_presents.size(), 1u);

  auto past_prediction = std::move(predicted_presents[0]);

  for (size_t i = 0; i < predicted_presents.size(); i++) {
    auto current = std::move(predicted_presents[i]);
    EXPECT_LT(current.latch_point(), current.presentation_time());
    EXPECT_GE(current.latch_point(), Now().get());

    if (i > 0)
      EXPECT_LT(past_prediction.presentation_time(), current.presentation_time());

    past_prediction = std::move(current);
  }
}

TEST_F(FrameSchedulerTest, InfinitelyLargePredictionRequest_ShouldBeTruncated) {
  auto scheduler = CreateDefaultFrameScheduler();

  zx::time next_vsync = vsync_timing_->last_vsync_time() + vsync_timing_->vsync_interval();

  // Ask for an extremely large prediction duration.
  std::vector<fuchsia::scenic::scheduling::PresentationInfo> predicted_presents;
  scheduler->GetFuturePresentationInfos(zx::duration(INTMAX_MAX), [&](auto future_presents) {
    predicted_presents = std::move(future_presents);
  });

  constexpr static const uint64_t kOverlyLargeRequestCount = 100u;

  EXPECT_LE(predicted_presents.size(), kOverlyLargeRequestCount);
  EXPECT_EQ(predicted_presents[0].presentation_time(), next_vsync.get());

  for (size_t i = 0; i < predicted_presents.size(); i++) {
    auto current = std::move(predicted_presents[i]);
    EXPECT_LT(current.latch_point(), current.presentation_time());
    EXPECT_GE(current.latch_point(), Now().get());
  }
}

// Verify that we properly observe 4 updates for all session updaters.
TEST_F(FrameSchedulerTest, MultiUpdaterMultiSession) {
  auto scheduler = CreateDefaultFrameScheduler();

  // Pre-declare the Session IDs used in this test.
  constexpr SessionId kSession1 = 1;
  constexpr SessionId kSession2 = 2;
  constexpr SessionId kSession3 = 3;
  constexpr SessionId kSession4 = 4;

  MockSessionUpdater updater1;
  MockSessionUpdater updater2;
  scheduler->AddSessionUpdater(updater1.GetWeakPtr());
  scheduler->AddSessionUpdater(updater2.GetWeakPtr());
  // Update is not expected to fail.
  scheduler->SetOnUpdateFailedCallbackForSession(kSession1, [] { EXPECT_FALSE(true); });
  scheduler->SetOnUpdateFailedCallbackForSession(kSession2, [] { EXPECT_FALSE(true); });
  scheduler->SetOnUpdateFailedCallbackForSession(kSession3, [] { EXPECT_FALSE(true); });
  scheduler->SetOnUpdateFailedCallbackForSession(kSession4, [] { EXPECT_FALSE(true); });

  ScheduleUpdateAndCallback(scheduler, kSession1, zx::time(2));
  ScheduleUpdateAndCallback(scheduler, kSession2, zx::time(3));
  ScheduleUpdateAndCallback(scheduler, kSession3, zx::time(4));
  ScheduleUpdateAndCallback(scheduler, kSession4, zx::time(5));
  // Should still only get one combined update for each session.
  ScheduleUpdateAndCallback(scheduler, kSession4, zx::time(6));

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));

  EXPECT_EQ(updater1.last_sessions_to_update().size(), 4u);
  EXPECT_EQ(updater2.last_sessions_to_update().size(), 4u);
  EXPECT_TRUE(updater1.last_sessions_to_update().find(kSession1) !=
              updater1.last_sessions_to_update().end());
  EXPECT_TRUE(updater1.last_sessions_to_update().find(kSession2) !=
              updater1.last_sessions_to_update().end());
  EXPECT_TRUE(updater1.last_sessions_to_update().find(kSession3) !=
              updater1.last_sessions_to_update().end());
  EXPECT_TRUE(updater1.last_sessions_to_update().find(kSession4) !=
              updater1.last_sessions_to_update().end());
  EXPECT_TRUE(updater2.last_sessions_to_update().find(kSession1) !=
              updater2.last_sessions_to_update().end());
  EXPECT_TRUE(updater2.last_sessions_to_update().find(kSession2) !=
              updater2.last_sessions_to_update().end());
  EXPECT_TRUE(updater2.last_sessions_to_update().find(kSession3) !=
              updater2.last_sessions_to_update().end());
  EXPECT_TRUE(updater2.last_sessions_to_update().find(kSession4) !=
              updater2.last_sessions_to_update().end());
}

TEST_F(FrameSchedulerTest, AddSessionUpdatersInSessionUpdater) {
  auto scheduler = CreateDefaultFrameScheduler();

  // Pre-declare the Session IDs used in this test.
  constexpr SessionId kSession1 = 1;
  // Updates are not expected to fail.
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSession1);

  // Creates a mock SessionUpdater that creates 10 SessionUpdaters on every
  // UpdateSessions call.
  constexpr size_t kUpdatersToAddOnEveryUpdate = 10U;
  std::vector<std::unique_ptr<MockSessionUpdater>> session_updaters_created;
  auto updater1 = std::make_unique<MockSessionUpdaterWithFunctionOnUpdate>(
      [scheduler = scheduler.get(), &session_updaters_created]() {
        for (size_t i = 0; i < kUpdatersToAddOnEveryUpdate; i++) {
          auto updater = std::make_unique<MockSessionUpdater>();
          scheduler->AddSessionUpdater(updater->GetWeakPtr());
          session_updaters_created.push_back(std::move(updater));
        }
      });
  scheduler->AddSessionUpdater(updater1->GetWeakPtr());

  // Frame 1: Updater1 creates 10 new SessionUpdaters this frame, but only
  // updater1 will be called to update sessions.
  bool callback_called1 = false;
  {
    ScheduleUpdateAndCallback(scheduler, kSession1, zx::time(0),
                              [&callback_called1](auto...) { callback_called1 = true; });
    EXPECT_EQ(updater1->update_sessions_call_count(), 0U);
    RunLoopFor(zx::sec(2));
    mock_renderer_->EndFrame(/*frame number=*/0, /*time_done=*/Now() + zx::sec(1));
    RunLoopFor(zx::sec(2));

    EXPECT_EQ(updater1->update_sessions_call_count(), 1U);
    EXPECT_TRUE(callback_called1);
    EXPECT_EQ(session_updaters_created.size(), kUpdatersToAddOnEveryUpdate);
    EXPECT_TRUE(std::all_of(session_updaters_created.begin(), session_updaters_created.end(),
                            [](const auto& session_updater) {
                              return session_updater->update_sessions_call_count() == 0;
                            }));
  }

  // Frame 2: updater1 will create another 10 SessionUpdaters this frame, which
  //          will not be updated, while the SessionUpdaters created on previous
  //          frame will be updated now.
  bool callback_called2 = false;
  {
    ScheduleUpdateAndCallback(scheduler, kSession1, zx::time(0),
                              [&callback_called2](auto...) { callback_called2 = true; });
    EXPECT_EQ(updater1->update_sessions_call_count(), 1U);
    RunLoopFor(zx::sec(2));
    mock_renderer_->EndFrame(/*frame number=*/1, /*time_done=*/Now());
    RunLoopFor(zx::sec(2));
    RunLoopUntilIdle();

    EXPECT_TRUE(callback_called2);
    EXPECT_EQ(updater1->update_sessions_call_count(), 2U);
    EXPECT_EQ(session_updaters_created.size(), 2 * kUpdatersToAddOnEveryUpdate);
    EXPECT_TRUE(std::count_if(session_updaters_created.begin(), session_updaters_created.end(),
                              [](const auto& session_updater) {
                                return session_updater->update_sessions_call_count() == 1;
                              }) == kUpdatersToAddOnEveryUpdate);
  }
}

// Checks that SessionUpdater being deleted by another SessionUpdater doesn't crash the frame
// scheduler.
// NOTE: This test relies on the frame scheduler at least initially maintaining insertion order of
// SessionUpdaters. If this changes the test needs to be reworked.
TEST_F(FrameSchedulerTest, KillingFollowingSessionUpdaterInPreviousSessionUpdater_ShouldNotCrash) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSession1 = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSession1);

  auto updater1 = std::make_unique<MockSessionUpdaterWithFunctionOnUpdate>([]() {
    EXPECT_FALSE(true) << "Should never be called.";
  });
  auto updater2 = std::make_unique<MockSessionUpdaterWithFunctionOnUpdate>([&updater1]() {
    // No call should have been made to UpdateSessions of |updater1|. If this ever fails then the
    // SessionUpdaters are probably out of expected order in the frame scheduler and the test needs
    // to be reworked.
    EXPECT_EQ(updater1->update_sessions_call_count(), 0U);
    updater1.reset();
  });

  // Add updaters in opposite order to ensure updater1 will be called after updater2.
  scheduler->AddSessionUpdater(updater2->GetWeakPtr());
  scheduler->AddSessionUpdater(updater1->GetWeakPtr());

  // Schedule an update.
  ScheduleUpdateAndCallback(scheduler, kSession1, zx::time(0), [](auto...) {});

  // We should now only be calling the update on |updater2|. If the deletion wasn't handled
  // properly, we should see a crash in RunLoop.
  EXPECT_NO_FATAL_FAILURE(RunLoopFor(zx::sec(2)));
  EXPECT_FALSE(updater1);
  EXPECT_EQ(updater2->update_sessions_call_count(), 1U);
}

TEST_F(FrameSchedulerTest, SquashedPresents_ShouldSignalAllCallbacksInOrder) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  std::vector<int64_t> callback_order;
  // Schedule three callbacks, which should be squashed.
  ScheduleUpdateAndCallback(scheduler, kSessionId, zx::time(0),
                            [&callback_order](auto...) { callback_order.push_back(1); });
  ScheduleUpdateAndCallback(scheduler, kSessionId, zx::time(0),
                            [&callback_order](auto...) { callback_order.push_back(2); });
  ScheduleUpdateAndCallback(scheduler, kSessionId, zx::time(0),
                            [&callback_order](auto...) { callback_order.push_back(3); });

  // Schedule a callback for later, which should not be squashed.
  ScheduleUpdateAndCallback(scheduler, kSessionId, Now() + zx::sec(1),
                            [&callback_order](auto...) { callback_order.push_back(4); });

  RunLoopFor(zx::sec(2));
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
  RunLoopUntilIdle();
  EXPECT_THAT(callback_order, ::testing::ElementsAreArray({1, 2, 3}));
}

TEST_F(FrameSchedulerTest, SquashedPresent2s_ShouldHaveCorrectNumberOfHandledPresentsInCallback) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  uint64_t present_count = 0;
  uint64_t num_callbacks = 0;
  scheduler->SetOnFramePresentedCallbackForSession(
      kSessionId,
      [&present_count, &num_callbacks](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        present_count += info.presentation_infos.size();
        ++num_callbacks;
      });

  // Schedule three callbacks, which should be squashed.
  SchedulePresent2Update(scheduler, kSessionId, zx::time(0));
  SchedulePresent2Update(scheduler, kSessionId, zx::time(0));
  SchedulePresent2Update(scheduler, kSessionId, zx::time(0));

  // Schedule a callback for later, which should not be squashed.
  SchedulePresent2Update(scheduler, kSessionId, Now() + zx::sec(1));

  RunLoopFor(zx::sec(2));
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
  RunLoopUntilIdle();
  EXPECT_EQ(present_count, 3u);
  EXPECT_EQ(num_callbacks, 1u);
}

TEST_F(FrameSchedulerTest, SkippedPresent_ShouldSignalAllCallbacksInOrder) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  std::vector<int64_t> callback_order;
  // These will never get scheduled.
  scheduler->RegisterPresent(kSessionId,
                             [&callback_order](auto...) { callback_order.push_back(1); },
                             /*release_fences=*/{});
  scheduler->RegisterPresent(kSessionId,
                             [&callback_order](auto...) { callback_order.push_back(2); },
                             /*release_fences=*/{});

  // Next one should be scheduled and presented.
  ScheduleUpdateAndCallback(scheduler, kSessionId, zx::time(0),
                            [&callback_order](auto...) { callback_order.push_back(3); });

  // This should never get scheduled, and it's callback should never be triggered.
  scheduler->RegisterPresent(kSessionId,
                             [&callback_order](auto...) { callback_order.push_back(4); },
                             /*release_fences=*/{});

  RunLoopFor(zx::sec(1));
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
  RunLoopUntilIdle();
  EXPECT_THAT(callback_order, ::testing::ElementsAreArray({1, 2, 3}));
}

// Verify that updaters can be removed after updates have been queued without crashing.
TEST_F(FrameSchedulerTest, CanRemoveUpdaterWithQueuedUpdates) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSession1 = 1;
  scheduler->SetOnUpdateFailedCallbackForSession(kSession1, [] { EXPECT_FALSE(true); });

  auto updater = std::make_unique<MockSessionUpdater>();
  scheduler->AddSessionUpdater(updater->GetWeakPtr());

  ScheduleUpdateAndCallback(scheduler, kSession1, zx::time(0));
  updater.reset();

  EXPECT_NO_FATAL_FAILURE(RunLoopFor(zx::duration(vsync_timing_->vsync_interval())));
}

// Verify that updaters added after updates have been queued still get all updates.
TEST_F(FrameSchedulerTest, CanAddUpdaterWithQueuedUpdates) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSession1 = 1;
  scheduler->SetOnUpdateFailedCallbackForSession(kSession1, [] { EXPECT_FALSE(true); });
  ScheduleUpdateAndCallback(scheduler, kSession1, zx::time(0));

  auto updater = std::make_unique<MockSessionUpdater>();
  scheduler->AddSessionUpdater(updater->GetWeakPtr());

  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  EXPECT_EQ(updater->update_sessions_call_count(), 1u);
}

// Tests creating a session and calling Present several times with release fences. Fences should
// fire as the subsequent Present call is presented to the display.
TEST_F(FrameSchedulerTest, ReleaseFences_ShouldBeFiredAfterSubsequentFramePresented) {
  auto scheduler = CreateDefaultFrameScheduler();
  constexpr SessionId kSession = 1;

  // Create release fences
  std::vector<zx::event> release_fences1 = scenic_impl::gfx::test::CreateEventArray(2);
  zx::event release_fence1 = scenic_impl::gfx::test::CopyEvent(release_fences1.at(0));
  zx::event release_fence2 = scenic_impl::gfx::test::CopyEvent(release_fences1.at(1));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence1, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));

  std::vector<zx::event> release_fences2 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event release_fence3 = scenic_impl::gfx::test::CopyEvent(release_fences2.at(0));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));

  bool callback1_fired = false;
  ScheduleUpdateAndCallback(
      scheduler, kSession, zx::time(0), [&callback1_fired](auto...) { callback1_fired = true; },
      std::move(release_fences1));
  EXPECT_FALSE(callback1_fired);
  RunLoopFor(zx::duration(vsync_timing_->vsync_interval()));
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
  EXPECT_TRUE(callback1_fired);
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence1, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));

  bool callback2_fired = false;
  ScheduleUpdateAndCallback(
      scheduler, kSession, Now() + (vsync_timing_->vsync_interval() + zx::duration(1)),
      [&callback2_fired](auto...) { callback2_fired = true; }, std::move(release_fences2));

  EXPECT_FALSE(callback2_fired);
  RunLoopFor(zx::sec(1));
  mock_renderer_->EndFrame(/* frame number */ 1, Now());
  EXPECT_TRUE(callback2_fired);
  EXPECT_TRUE(scenic_impl::gfx::test::IsEventSignalled(release_fence1, ZX_EVENT_SIGNALED));
  EXPECT_TRUE(scenic_impl::gfx::test::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));
}

// Tests creating a session and calling Present2 several times with release fences. Fences should
// fire as the subsequent Present call is presented to the display.
TEST_F(FrameSchedulerTest, ReleaseFences_WithPresent2_ShouldBeFiredAfterSubsequentFramePresented) {
  auto scheduler = CreateDefaultFrameScheduler();
  constexpr SessionId kSessionId = 1;

  scheduler->SetOnFramePresentedCallbackForSession(kSessionId, [](auto) {});

  // Create release fences
  std::vector<zx::event> release_fences1 = scenic_impl::gfx::test::CreateEventArray(2);
  zx::event release_fence1 = scenic_impl::gfx::test::CopyEvent(release_fences1.at(0));
  zx::event release_fence2 = scenic_impl::gfx::test::CopyEvent(release_fences1.at(1));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence1, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));

  std::vector<zx::event> release_fences2 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event release_fence3 = scenic_impl::gfx::test::CopyEvent(release_fences2.at(0));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));

  bool callback1_fired = false;

  SchedulePresent2Update(scheduler, kSessionId, zx::time(0), zx::time(1),
                         std::move(release_fences1));
  EXPECT_FALSE(callback1_fired);
  RunLoopFor(zx::sec(3));
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence1, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));

  SchedulePresent2Update(scheduler, kSessionId,
                         Now() + (vsync_timing_->vsync_interval() + zx::duration(1)), zx::time(1),
                         std::move(release_fences2));

  RunLoopFor(zx::sec(1));
  mock_renderer_->EndFrame(/* frame number */ 1, Now());
  EXPECT_TRUE(scenic_impl::gfx::test::IsEventSignalled(release_fence1, ZX_EVENT_SIGNALED));
  EXPECT_TRUE(scenic_impl::gfx::test::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));
}

TEST_F(FrameSchedulerTest, SquashedPresents_ShouldHaveAllPreviousFencesSignaled) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  // Create release fences
  std::vector<zx::event> release_fences1 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event release_fence1 = scenic_impl::gfx::test::CopyEvent(release_fences1.at(0));
  std::vector<zx::event> release_fences2 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event release_fence2 = scenic_impl::gfx::test::CopyEvent(release_fences2.at(0));
  std::vector<zx::event> release_fences3 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event release_fence3 = scenic_impl::gfx::test::CopyEvent(release_fences3.at(0));

  int64_t present_count = 0;
  scheduler->SetOnFramePresentedCallbackForSession(
      kSessionId, [&present_count](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        present_count += info.presentation_infos.size();
      });

  // Schedule three presents, which should be squashed. First fence should be signaled.
  SchedulePresent2Update(scheduler, kSessionId, zx::time(0), zx::time(0),
                         std::move(release_fences1));
  SchedulePresent2Update(scheduler, kSessionId, zx::time(0), zx::time(0),
                         std::move(release_fences2));

  // Schedule a present for later, which should not be part of the squashed presents.
  SchedulePresent2Update(scheduler, kSessionId, Now() + zx::sec(1), zx::time(0),
                         std::move(release_fences3));

  RunLoopFor(zx::sec(2));
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
  RunLoopUntilIdle();
  EXPECT_EQ(present_count, 2);
  EXPECT_TRUE(scenic_impl::gfx::test::IsEventSignalled(release_fence1, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));
}

TEST_F(FrameSchedulerTest, SkippedPresents_ShouldHaveAllPreviousFencesSignaled) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  // Create release fences
  std::vector<zx::event> release_fences1 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event release_fence1 = scenic_impl::gfx::test::CopyEvent(release_fences1.at(0));
  std::vector<zx::event> release_fences2 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event release_fence2 = scenic_impl::gfx::test::CopyEvent(release_fences2.at(0));
  std::vector<zx::event> release_fences3 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event release_fence3 = scenic_impl::gfx::test::CopyEvent(release_fences3.at(0));
  std::vector<zx::event> release_fences4 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event release_fence4 = scenic_impl::gfx::test::CopyEvent(release_fences4.at(0));

  int64_t callback_count = 0;
  // These will never get scheduled, but will be skipped and fences should be signaled.
  scheduler->RegisterPresent(
      kSessionId, [&callback_count](auto...) { ++callback_count; }, std::move(release_fences1));
  scheduler->RegisterPresent(
      kSessionId, [&callback_count](auto...) { ++callback_count; }, std::move(release_fences2));

  // Next one should be scheduled and presented. Fences should not be signaled.
  ScheduleUpdateAndCallback(
      scheduler, kSessionId, zx::time(0), [&callback_count](auto...) { ++callback_count; },
      std::move(release_fences3));

  // This should never get scheduled, and it's callback should never be triggered.
  scheduler->RegisterPresent(
      kSessionId, [&callback_count](auto...) { ++callback_count; }, std::move(release_fences3));

  RunLoopFor(zx::sec(1));
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());
  RunLoopUntilIdle();
  EXPECT_EQ(callback_count, 3);
  EXPECT_TRUE(scenic_impl::gfx::test::IsEventSignalled(release_fence1, ZX_EVENT_SIGNALED));
  EXPECT_TRUE(scenic_impl::gfx::test::IsEventSignalled(release_fence2, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence3, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(scenic_impl::gfx::test::IsEventSignalled(release_fence4, ZX_EVENT_SIGNALED));
}

TEST_F(FrameSchedulerTest, ReleaseFences_ShouldFireInOrder) {
  auto scheduler = CreateDefaultFrameScheduler();

  constexpr SessionId kSessionId = 1;
  SetSessionUpdateFailedNotExpected(scheduler.get(), kSessionId);

  std::vector<int> fence_order;

  // Create release fences
  std::vector<zx::event> release_fences1 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event release_fence1 = scenic_impl::gfx::test::CopyEvent(release_fences1.at(0));
  async::Wait waiter1(release_fence1.get(), ZX_EVENT_SIGNALED, 0,
                      [&fence_order](auto...) { fence_order.push_back(1); });
  waiter1.Begin(dispatcher());

  std::vector<zx::event> release_fences2 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event release_fence2 = scenic_impl::gfx::test::CopyEvent(release_fences2.at(0));
  async::Wait waiter2(release_fence2.get(), ZX_EVENT_SIGNALED, 0,
                      [&fence_order](auto...) { fence_order.push_back(2); });
  waiter2.Begin(dispatcher());

  std::vector<zx::event> release_fences3 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event release_fence3 = scenic_impl::gfx::test::CopyEvent(release_fences3.at(0));
  async::Wait waiter3(release_fence3.get(), ZX_EVENT_SIGNALED, 0,
                      [&fence_order](auto...) { fence_order.push_back(3); });
  waiter3.Begin(dispatcher());

  // These will never get scheduled, but will be skipped and fences should be signaled.
  scheduler->RegisterPresent(
      kSessionId, /*callback=*/[](auto...) {}, std::move(release_fences1));
  scheduler->RegisterPresent(
      kSessionId, /*callback=*/[](auto...) {}, std::move(release_fences2));
  scheduler->RegisterPresent(
      kSessionId, /*callback=*/[](auto...) {}, std::move(release_fences3));

  // Next one should be scheduled and presented, triggering signalling of previous fences.
  ScheduleUpdateAndCallback(scheduler, kSessionId, zx::time(0), /*callback=*/[](auto...) {},
                            /*release_fence=*/{});

  RunLoopFor(zx::sec(1));
  mock_renderer_->EndFrame(/* frame number */ 0, /*time_done*/ Now());

  EXPECT_TRUE(fence_order.empty());
  RunLoopUntilIdle();
  EXPECT_THAT(fence_order, ::testing::ElementsAreArray({1, 2, 3}));
}

}  // namespace test
}  // namespace scheduling
