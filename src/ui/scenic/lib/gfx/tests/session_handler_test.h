// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_SESSION_HANDLER_TEST_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_SESSION_HANDLER_TEST_H_

#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "lib/gtest/test_loop_fixture.h"
#include "lib/sys/cpp/component_context.h"
#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/sysmem.h"
#include "src/ui/scenic/lib/gfx/tests/error_reporting_test.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"

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

  virtual escher::EscherWeakPtr GetEscherWeakPtr();

  SessionHandler* session_handler() {
    FXL_DCHECK(command_dispatcher_);
    return static_cast<SessionHandler*>(command_dispatcher_.get());
  }

  Scenic* scenic() { return scenic_.get(); }

  Engine* engine() { return engine_.get(); }

  Session* session() { return session_handler()->session(); }

  // |SessionUpdater|
  UpdateResults UpdateSessions(std::unordered_set<SessionId> sessions_to_update,
                               zx::time target_presentation_time, zx::time latched_time,
                               uint64_t trace_id) override;
  // |SessionUpdater|
  void PrepareFrame(zx::time target_presentation_time, uint64_t trace_id) override;

  sys::testing::ComponentContextProvider app_context_;
  std::unique_ptr<Scenic> scenic_;
  std::unique_ptr<escher::impl::CommandBufferSequencer> command_buffer_sequencer_;
  std::unique_ptr<Engine> engine_;
  std::shared_ptr<FrameScheduler> frame_scheduler_;
  std::unique_ptr<Sysmem> sysmem_;
  std::unique_ptr<DisplayManager> display_manager_;
  std::unique_ptr<scenic_impl::Session> scenic_session_;
  CommandDispatcherUniquePtr command_dispatcher_;
  std::unique_ptr<SessionManager> session_manager_;

  fxl::WeakPtrFactory<SessionHandlerTest> weak_factory_;  // must be last
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_SESSION_HANDLER_TEST_H_
