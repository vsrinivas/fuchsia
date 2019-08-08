// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_SESSION_HANDLER_TEST_H_
#define GARNET_LIB_UI_GFX_TESTS_SESSION_HANDLER_TEST_H_

#include <lib/fit/function.h>

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/tests/error_reporting_test.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/sys/cpp/component_context.h"

namespace scenic_impl {
namespace gfx {
namespace test {

// For testing SessionHandler without having to manually provide all the state
// necessary for SessionHandler to run
class SessionHandlerTest : public ErrorReportingTest, public SessionUpdater {
 public:
  SessionHandlerTest();

 protected:
  // | ::testing::Test |
  void SetUp() override;
  void TearDown() override;

  void InitializeScenic();
  void InitializeDisplayManager();
  void InitializeEngine();
  void InitializeSessionHandler();
  void InitializeScenicSession(SessionId session_id);

  SessionHandler* session_handler() {
    FXL_DCHECK(command_dispatcher_);
    return static_cast<SessionHandler*>(command_dispatcher_.get());
  }

  Scenic* scenic() { return scenic_.get(); }

  Engine* engine() { return engine_.get(); }

  Session* session() { return session_handler()->session(); }

  // |SessionUpdater|
  UpdateResults UpdateSessions(std::unordered_set<SessionId> sessions_to_update,
                               zx::time presentation_time, uint64_t trace_id) override;
  // |SessionUpdater|
  void PrepareFrame(zx::time presentation_time, uint64_t trace_id) override;

  std::unique_ptr<sys::ComponentContext> app_context_;
  std::unique_ptr<Scenic> scenic_;
  std::unique_ptr<escher::impl::CommandBufferSequencer> command_buffer_sequencer_;
  std::unique_ptr<Engine> engine_;
  std::shared_ptr<FrameScheduler> frame_scheduler_;
  std::unique_ptr<DisplayManager> display_manager_;
  std::unique_ptr<scenic_impl::Session> scenic_session_;
  CommandDispatcherUniquePtr command_dispatcher_;
  std::unique_ptr<SessionManager> session_manager_;

  fxl::WeakPtrFactory<SessionHandlerTest> weak_factory_;  // must be last
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_SESSION_HANDLER_TEST_H_
