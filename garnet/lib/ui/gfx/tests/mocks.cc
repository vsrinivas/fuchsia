// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/mocks.h"

#include "garnet/lib/ui/gfx/tests/session_test.h"
#include "garnet/lib/ui/scenic/command_dispatcher.h"

namespace scenic_impl {
namespace gfx {
namespace test {

SessionForTest::SessionForTest(SessionId id, SessionContext context,
                               EventReporter* event_reporter,
                               ErrorReporter* error_reporter)
    : Session(id, std::move(context), event_reporter, error_reporter) {}

SessionHandlerForTest::SessionHandlerForTest(SessionManager* session_manager,
                                             SessionContext session_context,
                                             SessionId session_id,
                                             Scenic* scenic,
                                             EventReporter* event_reporter,
                                             ErrorReporter* error_reporter)
    : SessionHandler(CommandDispatcherContext(scenic, nullptr, session_id),
                     session_manager, std::move(session_context), session_id,
                     event_reporter, error_reporter),
      command_count_(0),
      present_count_(0) {}

void SessionHandlerForTest::DispatchCommand(
    fuchsia::ui::scenic::Command command) {
  SessionHandler::DispatchCommand(std::move(command));
  ++command_count_;
}

void SessionHandlerForTest::Present(
    uint64_t presentation_time, ::std::vector<zx::event> acquire_fences,
    ::std::vector<zx::event> release_fences,
    fuchsia::ui::scenic::Session::PresentCallback callback) {
  SessionHandler::Present(presentation_time, std::move(acquire_fences),
                          std::move(release_fences), std::move(callback));
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

SessionManagerForTest::SessionManagerForTest() : SessionManager() {}

void SessionManagerForTest::InsertSessionHandler(
    SessionId session_id, SessionHandler* session_handler) {
  SessionManager::InsertSessionHandler(session_id, session_handler);
}

EngineForTest::EngineForTest(DisplayManager* display_manager,
                             std::unique_ptr<escher::ReleaseFenceSignaller> r,
                             escher::EscherWeakPtr escher)
    : Engine(display_manager, std::move(r),
             std::make_unique<SessionManagerForTest>(), std::move(escher)) {}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
