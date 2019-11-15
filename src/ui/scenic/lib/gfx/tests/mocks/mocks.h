// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_MOCKS_MOCKS_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_MOCKS_MOCKS_H_

#include "src/ui/lib/escher/flib/release_fence_signaller.h"
#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/engine/session_handler.h"
#include "src/ui/scenic/lib/gfx/gfx_system.h"
#include "src/ui/scenic/lib/scenic/scenic.h"

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

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_MOCKS_MOCKS_H_
