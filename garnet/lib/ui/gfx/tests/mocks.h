// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_MOCKS_H_
#define GARNET_LIB_UI_GFX_TESTS_MOCKS_H_

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "lib/escher/flib/release_fence_signaller.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class SessionForTest : public Session {
 public:
  SessionForTest(SessionId id, SessionContext context,
                 EventReporter* event_reporter = EventReporter::Default(),
                 ErrorReporter* error_reporter = ErrorReporter::Default());
};

class SessionHandlerForTest : public SessionHandler {
 public:
  SessionHandlerForTest(
      SessionContext session_context, SessionId session_id, Scenic* scenic,
      EventReporter* event_reporter = EventReporter::Default(),
      ErrorReporter* error_reporter = ErrorReporter::Default());

  SessionHandlerForTest(CommandDispatcherContext command_dispatcher_context,
                        SessionContext session_context,
                        EventReporter* event_reporter,
                        ErrorReporter* error_reporter);

  // |scenic::CommandDispatcher|
  void DispatchCommand(fuchsia::ui::scenic::Command command) override;

  // |fuchsia::ui::scenic::Session / scenic::TempSessionDelegate|
  void Present(uint64_t presentation_time,
               ::std::vector<zx::event> acquire_fences,
               ::std::vector<zx::event> release_fences,
               fuchsia::ui::scenic::Session::PresentCallback callback) override;

  // Return the number of commands that have been enqueued.
  uint32_t command_count() const { return command_count_; }

  // Return the number of times that Present() has been called.
  uint32_t present_count() const { return present_count_; }

 private:
  std::atomic<uint32_t> command_count_;
  std::atomic<uint32_t> present_count_;
};

class ReleaseFenceSignallerForTest : public escher::ReleaseFenceSignaller {
 public:
  ReleaseFenceSignallerForTest(
      escher::impl::CommandBufferSequencer* command_buffer_sequencer);

  void AddCPUReleaseFence(zx::event fence) override;

  uint32_t num_calls_to_add_cpu_release_fence() {
    return num_calls_to_add_cpu_release_fence_;
  }

 private:
  uint32_t num_calls_to_add_cpu_release_fence_ = 0;
};

class SessionManagerForTest : public SessionManager {
 public:
  SessionManagerForTest(EventReporter* event_reporter = nullptr,
                        ErrorReporter* error_reporter = nullptr);
  ~SessionManagerForTest() override = default;

  // Publicly accessible for tests.
  void InsertSessionHandler(SessionId session_id,
                            SessionHandler* session_handler);

 protected:
  // Override CreateSessionHandler so that calling CreateCommandDispatcher
  // creates the test version of SessionHandler.
  std::unique_ptr<SessionHandler> CreateSessionHandler(
      CommandDispatcherContext dispatcher_context,
      SessionContext session_context, SessionId session_id,
      // If tests instances of reporters were provided at SessionManager
      // creation, those are used instead of the ones provided here
      EventReporter* error_reporter,
      ErrorReporter* event_reporter) const override;

 private:
  EventReporter* event_reporter_;
  ErrorReporter* error_reporter_;
};

class EngineForTest : public Engine {
 public:
  EngineForTest(sys::ComponentContext* component_context,
                DisplayManager* display_manager,
                std::unique_ptr<escher::ReleaseFenceSignaller> r,
                EventReporter* event_reporter = nullptr,
                ErrorReporter* error_reporter = nullptr);
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_MOCKS_H_
