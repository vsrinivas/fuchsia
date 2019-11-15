// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/session_handler_test.h"

#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/ui/scenic/lib/gfx/engine/constant_frame_predictor.h"
#include "src/ui/scenic/lib/gfx/engine/default_frame_scheduler.h"

namespace scenic_impl {
namespace gfx {
namespace test {

SessionHandlerTest::SessionHandlerTest() : weak_factory_(this) {}

void SessionHandlerTest::SetUp() {
  ErrorReportingTest::SetUp();

  InitializeScenic();
  InitializeDisplayManager();
  InitializeEngine();

  InitializeSessionHandler();

  RunLoopUntilIdle();  // Reset loop state; some tests are sensitive to dirty loop state.
}

void SessionHandlerTest::TearDown() {
  command_dispatcher_.reset();
  engine_.reset();
  frame_scheduler_.reset();
  command_buffer_sequencer_.reset();
  display_manager_.reset();
  scenic_.reset();
  session_manager_.reset();

  ErrorReportingTest::TearDown();
}

void SessionHandlerTest::InitializeScenic() {
  sys::testing::ComponentContextProvider context_provider;
  scenic_ = std::make_unique<Scenic>(app_context_.context(), inspect_deprecated::Node(), [] {});
}

void SessionHandlerTest::InitializeSessionHandler() {
  auto session_context = engine_->session_context();
  auto session_id = SessionId(1);

  InitializeScenicSession(session_id);

  session_manager_ = std::make_unique<SessionManagerForTest>(this->shared_event_reporter(),
                                                             this->shared_error_reporter()),
  command_dispatcher_ = session_manager_->CreateCommandDispatcher(
      CommandDispatcherContext(scenic_.get(), scenic_session_.get()), std::move(session_context));
}

void SessionHandlerTest::InitializeDisplayManager() {
  display_manager_ = std::make_unique<DisplayManager>();
  display_manager_->SetDefaultDisplayForTests(std::make_unique<Display>(
      /*id*/ 0, /*px-width*/ 0, /*px-height*/ 0));
}

void SessionHandlerTest::InitializeEngine() {
  command_buffer_sequencer_ = std::make_unique<escher::impl::CommandBufferSequencer>();

  auto mock_release_fence_signaller =
      std::make_unique<ReleaseFenceSignallerForTest>(command_buffer_sequencer_.get());

  frame_scheduler_ = std::make_shared<DefaultFrameScheduler>(
      display_manager_->default_display_shared(),
      std::make_unique<ConstantFramePredictor>(/* static_vsync_offset */ zx::msec(5)));
  engine_ = std::make_unique<Engine>(app_context_.context(), frame_scheduler_,
                                     std::move(mock_release_fence_signaller), GetEscherWeakPtr());
  frame_scheduler_->SetFrameRenderer(engine_->GetWeakPtr());
  frame_scheduler_->AddSessionUpdater(weak_factory_.GetWeakPtr());
}

void SessionHandlerTest::InitializeScenicSession(SessionId session_id) {
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener;
  scenic_session_ = std::make_unique<scenic_impl::Session>(session_id, /*session_request=*/nullptr,
                                                           std::move(listener));
}

escher::EscherWeakPtr SessionHandlerTest::GetEscherWeakPtr() { return escher::EscherWeakPtr(); }

SessionUpdater::UpdateResults SessionHandlerTest::UpdateSessions(
    std::unordered_set<SessionId> sessions_to_update, zx::time target_presentation_time,
    zx::time latched_time, uint64_t trace_id) {
  UpdateResults update_results;
  CommandContext command_context(/*uploader*/ nullptr, /*sysmem*/ nullptr,
                                 /*display_manager*/ nullptr, engine_->scene_graph()->GetWeakPtr());

  for (auto session_id : sessions_to_update) {
    auto session_handler = session_manager_->FindSessionHandler(session_id);
    if (!session_handler) {
      // This means the session that requested the update died after the
      // request. Requiring the scene to be re-rendered to reflect the session's
      // disappearance is probably desirable. ImagePipe also relies on this to
      // be true, since it calls ScheduleUpdate() in its destructor.
      update_results.needs_render = true;
      continue;
    }

    auto session = session_handler->session();

    auto apply_results =
        session->ApplyScheduledUpdates(&command_context, target_presentation_time, latched_time);
  }

  // Flush work to the GPU.
  command_context.Flush();

  return update_results;
}

void SessionHandlerTest::PrepareFrame(zx::time target_presentation_time, uint64_t trace_id) {}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
