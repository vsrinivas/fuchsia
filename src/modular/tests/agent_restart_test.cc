// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <fuchsia/testing/modular/cpp/fidl.h>

#include <gmock/gmock.h>
#include <sdk/lib/modular/testing/cpp/fake_component.h>

#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {

class TestAgent : public modular_testing::FakeComponent {
 public:
  fit::closure on_create;

  static std::unique_ptr<TestAgent> CreateWithDefaultOptions() {
    return std::make_unique<TestAgent>(modular_testing::FakeComponent::Args{
        .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()});
  }

  TestAgent(FakeComponent::Args args) : FakeComponent(std::move(args)) {}

  // |FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override {
    FakeComponent::OnCreate(std::move(startup_info));
    if (on_create)
      on_create();
  }
};

class AgentRestartTest : public modular_testing::TestHarnessFixture {
 protected:
  std::unique_ptr<TestAgent> agent_;
  AgentRestartTest() : agent_(TestAgent::CreateWithDefaultOptions()) {}
};

// Test that a session agent is restarted if it crashes.
TEST_F(AgentRestartTest, SessionAgentsAreRestartedOnCrash) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_sessionmgr_config()->mutable_session_agents()->push_back(agent_->url());
  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptComponent(agent_->BuildInterceptOptions());

  bool was_started = false;
  agent_->on_create = [&] { was_started = true; };

  builder.BuildAndRun(test_harness());
  RunLoopUntil([&] { return was_started; });

  was_started = false;
  agent_->Exit(1, fuchsia::sys::TerminationReason::EXITED);
  RunLoopUntil([&] { return was_started; });
}

// Test that a session agent is not restarted if it crashes when the config field
// disable_agent_restart_on_crash is set to true.
TEST_F(AgentRestartTest, SessionAgentsAreNotRestartedOnCrashWhenDisabled) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_sessionmgr_config()->mutable_session_agents()->push_back(agent_->url());
  spec.mutable_sessionmgr_config()->set_disable_agent_restart_on_crash(true);
  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptComponent(agent_->BuildInterceptOptions());

  bool was_started = false;
  agent_->on_create = [&] { was_started = true; };

  builder.BuildAndRun(test_harness());
  RunLoopUntil([&] { return was_started; });

  was_started = false;
  agent_->Exit(1, fuchsia::sys::TerminationReason::EXITED);
  sleep(3);
  test_harness_launcher()->StopTestHarness();
  RunLoopUntil([&]() { return !test_harness_launcher()->is_test_harness_running(); });
  EXPECT_FALSE(was_started);
}

}  // namespace
