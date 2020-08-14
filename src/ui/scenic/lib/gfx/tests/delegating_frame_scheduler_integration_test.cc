// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ui/scenic/cpp/commands.h>

#include <gtest/gtest.h>

#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe_handler.h"
#include "src/ui/scenic/lib/gfx/tests/error_reporting_test.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"
#include "src/ui/scenic/lib/scheduling/delegating_frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/tests/mocks/frame_scheduler_mocks.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using DelegatingFrameSchedulerIntegrationTest = ErrorReportingTest;

struct TestSession {
  std::shared_ptr<scheduling::DelegatingFrameScheduler> delegating_frame_scheduler;
  std::unique_ptr<scenic_impl::Session> session;
};

constexpr scenic_impl::SessionId kSessionId = 1;

TestSession CreateTestSession(std::shared_ptr<EventReporter> event_reporter,
                              std::shared_ptr<ErrorReporter> error_reporter) {
  TestSession session;
  session.delegating_frame_scheduler =
      std::make_shared<scheduling::DelegatingFrameScheduler>(nullptr);
  session.session = std::make_unique<scenic_impl::Session>(kSessionId, nullptr, nullptr, []() {});
  session.session->SetFrameScheduler(session.delegating_frame_scheduler);
  return session;
}

// Test that if FrameScheduler is set _after_ a call to Session::ScheduleUpdateForPresent,
// that the FrameScheduler will still get a call to FrameScheduler::ScheduleUpdateForSession.
//
// TODO(fxbug.dev/42536): Revamp this test when FrameScheduler is moved into Scenic::Session.
TEST_F(DelegatingFrameSchedulerIntegrationTest, SessionIntegration1) {
  TestSession session = CreateTestSession(shared_event_reporter(), shared_error_reporter());

  std::vector<zx::event> acquire_fences;
  zx::event fence = CreateEvent();
  acquire_fences.push_back(CopyEvent(fence));

  std::vector<fuchsia::ui::gfx::Command> commands;
  commands.push_back(scenic::NewCreateCompositorCmd(/*id=*/1));

  constexpr zx::time kPresentationTime = zx::time(5);
  session.session->Present(kPresentationTime.get(), /*acquire_fences=*/{}, /*release_fences=*/{},
                           /*present_callback=*/[](auto) {});
  fence.signal(0u, escher::kFenceSignalled);
  RunLoopUntilIdle();

  auto frame_scheduler = std::make_shared<scheduling::test::MockFrameScheduler>();

  bool scheduled_update = false;
  // Mock method callback for test.
  frame_scheduler->set_schedule_update_for_session_callback(
      [&](zx::time requested_presentation_time, scheduling::SchedulingIdPair id_pair) {
        scheduled_update = true;
        EXPECT_EQ(kPresentationTime, requested_presentation_time);
        EXPECT_EQ(kSessionId, id_pair.session_id);
      });

  // Once |frame_scheduler| is set, expect it to get a call to ScheduleUpdateForSession.
  EXPECT_FALSE(scheduled_update);
  session.delegating_frame_scheduler->SetFrameScheduler(frame_scheduler);
  EXPECT_TRUE(scheduled_update);
}

// Test that if FrameScheduler is set _after_ a call to GetFuturePresentationTimes,
// that we still get a return value callback.
TEST_F(DelegatingFrameSchedulerIntegrationTest, SessionIntegration2) {
  TestSession session = CreateTestSession(shared_event_reporter(), shared_error_reporter());

  bool got_return_value = false;
  session.delegating_frame_scheduler->GetFuturePresentationInfos(
      zx::duration(5),
      /*return_callback=*/[&](auto infos) { got_return_value = true; });

  auto frame_scheduler = std::make_shared<scheduling::test::MockFrameScheduler>();
  // Once |frame_scheduler| is set, expect it to get a return value from
  // GetFuturePresentationTimes.
  EXPECT_FALSE(got_return_value);
  session.delegating_frame_scheduler->SetFrameScheduler(frame_scheduler);
  EXPECT_TRUE(got_return_value);
}

// Test that if FrameScheduler is set _after_ a call to ImagePipeUpdater::ScheduleImagePipeUpdate,
// that the FrameScheduler will still get a call to FrameScheduler::ScheduleUpdateForSession.
TEST_F(DelegatingFrameSchedulerIntegrationTest, ImagePipeUpdaterIntegration) {
  TestSession session = CreateTestSession(shared_event_reporter(), shared_error_reporter());
  // This ImagePipeUpdater is using the same delegating frame scheduler in as |session|.
  auto image_pipe_updater = std::make_shared<ImagePipeUpdater>(session.delegating_frame_scheduler);
  session.delegating_frame_scheduler->AddSessionUpdater(image_pipe_updater);

  constexpr zx::time kPresentationTime = zx::time(5);
  image_pipe_updater->ScheduleImagePipeUpdate(kPresentationTime, /*image_pipe=*/nullptr,
                                              /*acquire_fences=*/{}, /*release_fences=*/{},
                                              /*callback=*/[](auto...) {});

  auto frame_scheduler = std::make_shared<scheduling::test::MockFrameScheduler>();

  bool scheduled_update = false;
  // Mock method callback for test.
  frame_scheduler->set_schedule_update_for_session_callback(
      [&](zx::time presentation_time, scheduling::SchedulingIdPair id_pair) {
        scheduled_update = true;
        EXPECT_EQ(kPresentationTime, presentation_time);
        EXPECT_EQ(image_pipe_updater->GetSchedulingId(), id_pair.session_id);
      });

  // Once |frame_scheduler| is set, expect it to get a call to ScheduleUpdateForSession.
  EXPECT_FALSE(scheduled_update);
  session.delegating_frame_scheduler->SetFrameScheduler(frame_scheduler);
  EXPECT_TRUE(scheduled_update);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
