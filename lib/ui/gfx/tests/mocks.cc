// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/mocks.h"

#include "garnet/lib/ui/scenic/command_dispatcher.h"

namespace scenic {
namespace gfx {
namespace test {

SessionForTest::SessionForTest(SessionId id,
                               Engine* engine,
                               EventReporter* event_reporter,
                               ErrorReporter* error_reporter)
    : Session(id, engine, event_reporter, error_reporter) {}

void SessionForTest::TearDown() {
  Session::TearDown();
}

SessionHandlerForTest::SessionHandlerForTest(CommandDispatcherContext context,
                                             Engine* engine,
                                             SessionId session_id,
                                             EventReporter* event_reporter,
                                             ErrorReporter* error_reporter)
    : SessionHandler(std::move(context),
                     engine,
                     session_id,
                     event_reporter,
                     error_reporter),
      enqueue_count_(0),
      present_count_(0) {}

void SessionHandlerForTest::Enqueue(::f1dl::VectorPtr<ui::CommandPtr> commands) {
  SessionHandler::Enqueue(std::move(commands));
  ++enqueue_count_;
}

void SessionHandlerForTest::Present(
    uint64_t presentation_time,
    ::f1dl::VectorPtr<zx::event> acquire_fences,
    ::f1dl::VectorPtr<zx::event> release_fences,
    const ui::Session::PresentCallback& callback) {
  SessionHandler::Present(presentation_time, std::move(acquire_fences),
                          std::move(release_fences), callback);
  ++present_count_;
}

ReleaseFenceSignallerForTest::ReleaseFenceSignallerForTest(
    escher::impl::CommandBufferSequencer* command_buffer_sequencer)
    : ReleaseFenceSignaller(command_buffer_sequencer) {}

void ReleaseFenceSignallerForTest::AddCPUReleaseFence(zx::event fence) {
  num_calls_to_add_cpu_release_fence_++;
  // Signal immediately for testing purposes.
  fence.signal(0u, escher::kFenceSignalled);
}

SessionManagerForTest::SessionManagerForTest(UpdateScheduler* update_scheduler)
    : SessionManager(update_scheduler) {}

std::unique_ptr<SessionHandler> SessionManagerForTest::CreateSessionHandler(
    CommandDispatcherContext context,
    Engine* engine,
    SessionId session_id,
    EventReporter* event_reporter,
    ErrorReporter* error_reporter) const {
  return std::make_unique<SessionHandlerForTest>(
      std::move(context), engine, session_id, event_reporter, error_reporter);
}

EngineForTest::EngineForTest(DisplayManager* display_manager,
                             std::unique_ptr<escher::ReleaseFenceSignaller> r,
                             escher::Escher* escher)
    : Engine(display_manager, std::move(r), escher) {}

std::unique_ptr<SessionManager> EngineForTest::InitializeSessionManager() {
  return std::make_unique<SessionManagerForTest>(this);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic
