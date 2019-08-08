// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_MOCKS_H_
#define GARNET_LIB_UI_GFX_TESTS_MOCKS_H_

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "garnet/lib/ui/gfx/gfx_system.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "src/ui/lib/escher/flib/release_fence_signaller.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class ReleaseFenceSignallerForTest : public escher::ReleaseFenceSignaller {
 public:
  ReleaseFenceSignallerForTest(escher::impl::CommandBufferSequencer* command_buffer_sequencer);

  void AddCPUReleaseFence(zx::event fence) override;
};

class SessionManagerForTest : public SessionManager {
 public:
  // |event_reporter| and |error_reporter| default to nullptr because of the way that
  // CreateSessionHandler() works: if either of these is non-null then it will override the
  // corresponding argument passed to CreateSessionHandler().
  SessionManagerForTest(std::shared_ptr<EventReporter> event_reporter = nullptr,
                        std::shared_ptr<ErrorReporter> error_reporter = nullptr);
  ~SessionManagerForTest() override = default;

  // Publicly accessible for tests.
  void InsertSessionHandler(SessionId session_id, SessionHandler* session_handler);

 protected:
  // Override CreateSessionHandler so that calling CreateCommandDispatcher
  // creates the test version of SessionHandler.
  std::unique_ptr<SessionHandler> CreateSessionHandler(
      CommandDispatcherContext dispatcher_context, SessionContext session_context,
      SessionId session_id,
      // If tests instances of reporters were provided at SessionManager
      // creation, those are used instead of the ones provided here
      std::shared_ptr<EventReporter> event_reporter,
      std::shared_ptr<ErrorReporter> error_reporter) override;

 private:
  std::shared_ptr<EventReporter> event_reporter_;
  std::shared_ptr<ErrorReporter> error_reporter_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_MOCKS_H_
