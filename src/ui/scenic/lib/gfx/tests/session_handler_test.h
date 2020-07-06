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
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/sysmem.h"
#include "src/ui/scenic/lib/gfx/tests/error_reporting_test.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/vsync_timing.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class SessionHandlerTest : public ErrorReportingTest {
 public:
  SessionHandlerTest();

 protected:
  // | ::testing::Test |
  void SetUp() override;
  void TearDown() override;

  void InitializeScenic();
  void InitializeEngine();
  void InitializeCommandDispatcher();
  void InitializeScenicSession(SessionId session_id);

  virtual escher::EscherWeakPtr GetEscherWeakPtr();

  Session* session() {
    FX_DCHECK(command_dispatcher_);
    return static_cast<Session*>(command_dispatcher_.get());
  }

  std::shared_ptr<scheduling::FrameScheduler> frame_scheduler() { return frame_scheduler_; }

  Scenic* scenic() { return scenic_.get(); }

  Engine* engine() { return engine_.get(); }

  class TestSessionUpdater : public scheduling::SessionUpdater {
   public:
    TestSessionUpdater(Engine* engine, SessionManager* session_manager)
        : engine_(engine), session_manager_(session_manager) {}

    // |scheduling::SessionUpdater|
    UpdateResults UpdateSessions(
        const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
        uint64_t trace_id) override;

   private:
    Engine* engine_;
    SessionManager* session_manager_;
  };

  sys::testing::ComponentContextProvider app_context_;
  std::unique_ptr<Scenic> scenic_;
  std::shared_ptr<Engine> engine_;
  std::shared_ptr<scheduling::FrameScheduler> frame_scheduler_;
  std::unique_ptr<scenic_impl::Session> scenic_session_;
  CommandDispatcherUniquePtr command_dispatcher_;
  std::unique_ptr<SessionManager> session_manager_;
  std::shared_ptr<TestSessionUpdater> session_updater_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_SESSION_HANDLER_TEST_H_
