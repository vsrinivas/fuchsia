// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"

#include "src/ui/scenic/lib/scenic/command_dispatcher.h"

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

SessionManagerForTest::SessionManagerForTest(std::shared_ptr<EventReporter> event_reporter,
                                             std::shared_ptr<ErrorReporter> error_reporter)
    : event_reporter_(event_reporter), error_reporter_(error_reporter) {}

std::unique_ptr<SessionHandler> SessionManagerForTest::CreateSessionHandler(
    CommandDispatcherContext dispatcher_context, SessionContext session_context,
    SessionId session_id, std::shared_ptr<EventReporter> event_reporter,
    std::shared_ptr<ErrorReporter> error_reporter) {
  return std::make_unique<SessionHandler>(std::move(dispatcher_context), std::move(session_context),
                                          event_reporter_ ? event_reporter_ : event_reporter,
                                          error_reporter_ ? error_reporter_ : error_reporter);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
