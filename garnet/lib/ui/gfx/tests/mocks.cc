// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/mocks.h"

#include "garnet/lib/ui/gfx/engine/default_frame_scheduler.h"
#include "garnet/lib/ui/gfx/engine/frame_predictor.h"
#include "garnet/lib/ui/scenic/command_dispatcher.h"

namespace scenic_impl {
namespace gfx {
namespace test {

ReleaseFenceSignallerForTest::ReleaseFenceSignallerForTest(
    escher::impl::CommandBufferSequencer* command_buffer_sequencer)
    : ReleaseFenceSignaller(command_buffer_sequencer) {}

void ReleaseFenceSignallerForTest::AddCPUReleaseFence(zx::event fence) {
  // Signal immediately for testing purposes.
  fence.signal(0u, escher::kFenceSignalled);
}

SessionManagerForTest::SessionManagerForTest(EventReporter* event_reporter,
                                             ErrorReporter* error_reporter)
    : event_reporter_(event_reporter), error_reporter_(error_reporter) {}

std::unique_ptr<SessionHandler> SessionManagerForTest::CreateSessionHandler(
    CommandDispatcherContext dispatcher_context, SessionContext session_context,
    SessionId session_id, EventReporter* event_reporter, ErrorReporter* error_reporter) {
  return std::make_unique<SessionHandler>(std::move(dispatcher_context), std::move(session_context),
                                          event_reporter_ ? event_reporter_ : event_reporter,
                                          error_reporter_ ? error_reporter_ : error_reporter);
}

GfxSystemForTest::GfxSystemForTest(SystemContext context,
                                   std::unique_ptr<DisplayManager> display_manager,
                                   escher::impl::CommandBufferSequencer* command_buffer_sequencer)
    : GfxSystem(std::move(context), std::move(display_manager), escher::EscherWeakPtr()),
      command_buffer_sequencer_(command_buffer_sequencer) {}

std::unique_ptr<SessionManager> GfxSystemForTest::InitializeSessionManager() {
  return std::make_unique<SessionManagerForTest>();
}

std::unique_ptr<gfx::Engine> GfxSystemForTest::InitializeEngine() {
  return std::make_unique<Engine>(frame_scheduler_, display_manager_.get(),
      std::make_unique<ReleaseFenceSignallerForTest>(command_buffer_sequencer_),
      escher::EscherWeakPtr());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
