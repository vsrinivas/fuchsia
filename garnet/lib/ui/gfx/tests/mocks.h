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

class SessionHandlerForTest : public SessionHandler {
 public:
  SessionHandlerForTest(SessionContext session_context, SessionId session_id, Scenic* scenic,
                        EventReporter* event_reporter = EventReporter::Default(),
                        ErrorReporter* error_reporter = ErrorReporter::Default());

  SessionHandlerForTest(CommandDispatcherContext command_dispatcher_context,
                        SessionContext session_context, EventReporter* event_reporter,
                        ErrorReporter* error_reporter);

  // |scenic::CommandDispatcher|
  void DispatchCommand(fuchsia::ui::scenic::Command command) override;

  // |fuchsia::ui::scenic::Session / scenic::TempSessionDelegate|
  void Present(uint64_t presentation_time, ::std::vector<zx::event> acquire_fences,
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
  ReleaseFenceSignallerForTest(escher::impl::CommandBufferSequencer* command_buffer_sequencer);

  void AddCPUReleaseFence(zx::event fence) override;

  uint32_t num_calls_to_add_cpu_release_fence() { return num_calls_to_add_cpu_release_fence_; }

 private:
  uint32_t num_calls_to_add_cpu_release_fence_ = 0;
};

class SessionManagerForTest : public SessionManager {
 public:
  SessionManagerForTest(EventReporter* event_reporter = nullptr,
                        ErrorReporter* error_reporter = nullptr);
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
      EventReporter* error_reporter, ErrorReporter* event_reporter) override;

 private:
  EventReporter* event_reporter_;
  ErrorReporter* error_reporter_;
};

class GfxSystemForTest : public GfxSystem {
 public:
  static constexpr TypeId kTypeId = GfxSystem::kTypeId;

  explicit GfxSystemForTest(SystemContext context, std::unique_ptr<DisplayManager> display_manager,
                            escher::impl::CommandBufferSequencer* command_buffer_sequencer)
      : GfxSystem(std::move(context), std::move(display_manager)),
        command_buffer_sequencer_(command_buffer_sequencer) {}

  Engine* engine() { return engine_.get(); }

 private:
  std::unique_ptr<SessionManager> InitializeSessionManager() override {
    return std::make_unique<SessionManagerForTest>();
  }

  std::unique_ptr<gfx::Engine> InitializeEngine() override {
    return std::make_unique<Engine>(
        context()->app_context(), frame_scheduler_, display_manager_.get(),
        std::make_unique<ReleaseFenceSignallerForTest>(command_buffer_sequencer_),
        escher_ ? escher_->GetWeakPtr() : escher::EscherWeakPtr());
  }

  std::unique_ptr<escher::Escher> InitializeEscher() override { return nullptr; }

  escher::impl::CommandBufferSequencer* command_buffer_sequencer_;
};

// Device-independent "display"; for testing only. Needed to ensure GfxSystem
// doesn't wait for a device-driven "display ready" signal.
class TestDisplay : public Display {
 public:
  TestDisplay(uint64_t id, uint32_t width_px, uint32_t height_px)
      : Display(id, width_px, height_px) {}
  ~TestDisplay() = default;
  bool is_test_display() const override { return true; }
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_MOCKS_H_
