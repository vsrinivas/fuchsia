// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ui/scenic/cpp/commands.h>

#include "gtest/gtest.h"
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

struct TestGfxSession {
  std::shared_ptr<scheduling::DelegatingFrameScheduler> delegating_frame_scheduler;
  SessionContext session_context;
  std::unique_ptr<Session> session;
};

constexpr scenic_impl::SessionId kSessionId = 1;

TestGfxSession CreateTestGfxSession(std::shared_ptr<EventReporter> event_reporter,
                                    std::shared_ptr<ErrorReporter> error_reporter) {
  TestGfxSession gfx_session;
  gfx_session.delegating_frame_scheduler =
      std::make_shared<scheduling::DelegatingFrameScheduler>(nullptr);
  gfx_session.session_context = {.vk_device = vk::Device(),
                                 .escher = nullptr,
                                 .escher_resource_recycler = nullptr,
                                 .escher_image_factory = nullptr,
                                 .escher_rounded_rect_factory = nullptr,
                                 .release_fence_signaller = nullptr,
                                 .frame_scheduler = gfx_session.delegating_frame_scheduler,
                                 .scene_graph = SceneGraphWeakPtr(),
                                 .view_linker = nullptr};
  gfx_session.session = std::make_unique<Session>(kSessionId, gfx_session.session_context,
                                                  event_reporter, error_reporter);
  return gfx_session;
}

// Test that if FrameScheduler is set _after_ a call to Session::ScheduleUpdateForPresent,
// that the FrameScheduler will still get a call to FrameScheduler::ScheduleUpdateForSession.
//
// TODO(fxb/42536): Revamp this test when FrameScheduler is moved into Scenic::Session.
TEST_F(DelegatingFrameSchedulerIntegrationTest, SessionIntegration1) {
  TestGfxSession gfx_session =
      CreateTestGfxSession(shared_event_reporter(), shared_error_reporter());

  std::vector<zx::event> acquire_fences;
  zx::event fence = CreateEvent();
  acquire_fences.push_back(CopyEvent(fence));

  std::vector<fuchsia::ui::gfx::Command> commands;
  commands.push_back(scenic::NewCreateCompositorCmd(/*id=*/1));

  constexpr zx::time kPresentationTime = zx::time(5);
  EXPECT_TRUE(gfx_session.session->ScheduleUpdateForPresent(
      kPresentationTime, /*commands=*/std::move(commands), std::move(acquire_fences),
      /*release_fences=*/{}, /*present_callback=*/[](auto) {}));
  fence.signal(0u, escher::kFenceSignalled);
  RunLoopUntilIdle();

  auto frame_scheduler = std::make_shared<scheduling::test::MockFrameScheduler>();

  bool scheduled_update = false;
  // Mock method callback for test.
  frame_scheduler->set_schedule_update_for_session_callback(
      [&](zx::time presentation_time, scenic_impl::SessionId session_id) {
        scheduled_update = true;
        EXPECT_EQ(kPresentationTime, presentation_time);
        EXPECT_EQ(kSessionId, session_id);
      });

  // Once |frame_scheduler| is set, expect it to get a call to ScheduleUpdateForSession.
  EXPECT_FALSE(scheduled_update);
  gfx_session.delegating_frame_scheduler->SetFrameScheduler(frame_scheduler);
  EXPECT_TRUE(scheduled_update);
}

// Test that if FrameScheduler is set _after_ a call to Session::GetFuturePresentationTimes,
// that we still get a return value callback.
//
// TODO(fxb/42536): Revamp this test when FrameScheduler is moved into Scenic::Session.
TEST_F(DelegatingFrameSchedulerIntegrationTest, SessionIntegration2) {
  TestGfxSession gfx_session =
      CreateTestGfxSession(shared_event_reporter(), shared_error_reporter());

  bool got_return_value = false;
  gfx_session.session->GetFuturePresentationInfos(
      zx::duration(5),
      /*return_callback=*/[&](auto infos) { got_return_value = true; });

  auto frame_scheduler = std::make_shared<scheduling::test::MockFrameScheduler>();
  // Once |frame_scheduler| is set, expect it to get a return value from
  // GetFuturePresentationTimes.
  EXPECT_FALSE(got_return_value);
  gfx_session.delegating_frame_scheduler->SetFrameScheduler(frame_scheduler);
  EXPECT_TRUE(got_return_value);
}

// Test that if FrameScheduler is set _after_ applying a SetRendererParamCmd to
// change render frequency, it still gets the render frequency callback.
//
// TODO(42510): Remove the SetRenderContinuously command.
TEST_F(DelegatingFrameSchedulerIntegrationTest, GfxCommandApplierIntegration) {
  TestGfxSession gfx_session =
      CreateTestGfxSession(shared_event_reporter(), shared_error_reporter());
  CommandContext empty_command_context;
  constexpr uint32_t kRendererId = 1;
  EXPECT_TRUE(GfxCommandApplier::ApplyCommand(gfx_session.session.get(), &empty_command_context,
                                              scenic::NewCreateRendererCmd(kRendererId)));

  auto param = fuchsia::ui::gfx::RendererParam();
  param.set_render_frequency(fuchsia::ui::gfx::RenderFrequency::CONTINUOUSLY);

  EXPECT_TRUE(GfxCommandApplier::ApplyCommand(
      gfx_session.session.get(), &empty_command_context,
      scenic::NewSetRendererParamCmd(kRendererId, std::move(param))));

  auto frame_scheduler = std::make_shared<scheduling::test::MockFrameScheduler>();

  bool render_frequency_was_set = false;
  // Mock method callback for test.
  frame_scheduler->set_set_render_continuously_callback([&](bool render_continuosly) {
    render_frequency_was_set = true;
    EXPECT_EQ(true, render_continuosly);
  });

  EXPECT_FALSE(render_frequency_was_set);
  gfx_session.delegating_frame_scheduler->SetFrameScheduler(frame_scheduler);
  EXPECT_TRUE(render_frequency_was_set);
}

// Test that if FrameScheduler is set _after_ a call to ImagePipeUpdater::ScheduleImagePipeUpdate,
// that the FrameScheduler will still get a call to FrameScheduler::ScheduleUpdateForSession.
TEST_F(DelegatingFrameSchedulerIntegrationTest, ImagePipeUpdaterIntegration) {
  TestGfxSession gfx_session =
      CreateTestGfxSession(shared_event_reporter(), shared_error_reporter());
  // This ImagePipeUpdater is using the delegating frame scheduler in the session context.
  auto image_pipe_updater =
      std::make_unique<ImagePipeUpdater>(gfx_session.session_context.frame_scheduler,
                                         gfx_session.session_context.release_fence_signaller);

  constexpr zx::time kPresentationTime = zx::time(5);
  image_pipe_updater->ScheduleImagePipeUpdate(kPresentationTime, /*image_pipe=*/nullptr);

  auto frame_scheduler = std::make_shared<scheduling::test::MockFrameScheduler>();

  bool scheduled_update = false;
  // Mock method callback for test.
  frame_scheduler->set_schedule_update_for_session_callback(
      [&](zx::time presentation_time, scenic_impl::SessionId session_id) {
        scheduled_update = true;
        EXPECT_EQ(kPresentationTime, presentation_time);
        EXPECT_EQ(image_pipe_updater->GetSchedulingId(), session_id);
      });

  // Once |frame_scheduler| is set, expect it to get a call to ScheduleUpdateForSession.
  EXPECT_FALSE(scheduled_update);
  gfx_session.delegating_frame_scheduler->SetFrameScheduler(frame_scheduler);
  EXPECT_TRUE(scheduled_update);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
