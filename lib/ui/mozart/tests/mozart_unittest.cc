// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "garnet/lib/ui/mozart/clock.h"
#include "garnet/lib/ui/mozart/mozart.h"
#include "lib/fsl/tasks/message_loop.h"

extern std::unique_ptr<app::ApplicationContext> g_application_context;

namespace mz {
namespace test {

using MozartTest = testing::Test;

class MockSystemWithDelayedInitialization : public System {
 public:
  static constexpr TypeId kTypeId = kDummySystem;

  explicit MockSystemWithDelayedInitialization(SystemContext context)
      : System(std::move(context), false) {}

  std::unique_ptr<CommandDispatcher> CreateCommandDispatcher(
      CommandDispatcherContext context) override {
    return nullptr;
  }

  void Initialize() {
    // This is a mock, so don't do anything except mark self as initialized.
    SetToInitialized();
  }
};

TEST_F(MozartTest, SessionCreatedAfterAllSystemsInitialized) {
  app::ApplicationContext* app_context = g_application_context.get();
  fxl::TaskRunner* task_runner =
      fsl::MessageLoop::GetCurrent()->task_runner().get();
  Clock clock_;
  Mozart mozart(app_context, task_runner, &clock_);

  auto mock_system =
      mozart.RegisterSystem<MockSystemWithDelayedInitialization>();
  EXPECT_EQ(0U, mozart.num_sessions());

  ui_mozart::SessionPtr session;
  mozart.CreateSession(session.NewRequest(), nullptr);
  EXPECT_EQ(0U, mozart.num_sessions());

  mock_system->Initialize();
  EXPECT_EQ(1U, mozart.num_sessions());
}

}  // namespace test
}  // namespace mz
