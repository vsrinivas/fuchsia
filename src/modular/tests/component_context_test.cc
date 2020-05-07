// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular/testing/cpp/fake_agent.h>

#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {

/// A FakeAgent that sets a flag when OnCreate was called.
class WasCreatedFakeAgent : public modular_testing::FakeAgent {
 public:
  explicit WasCreatedFakeAgent(FakeComponent::Args args) : modular_testing::FakeAgent(args) {}
  static std::unique_ptr<WasCreatedFakeAgent> CreateWithDefaultOptions();

  /// Returns true if OnCreate has been called.
  bool was_created() { return was_created_; }

 protected:
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override { was_created_ = true; }

  /// Whether or not the agent has been instantiated.
  bool was_created_ = false;
};

std::unique_ptr<WasCreatedFakeAgent> WasCreatedFakeAgent::CreateWithDefaultOptions() {
  return std::make_unique<WasCreatedFakeAgent>(modular_testing::FakeComponent::Args{
      .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
      .sandbox_services = FakeAgent::GetDefaultSandboxServices()});
}

class ComponentContextTest : public modular_testing::TestHarnessFixture {};

// Tests that an agent is able to start another agent through
// fuchsia::modular::ComponentContext.DeprecatedConnectToAgent(). Asserts that closing
// fuchsia::modular::AgentController triggers the agent to stop.
TEST_F(ComponentContextTest, AgentStartsSecondAgent) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  auto fake_module = modular_testing::FakeModule::CreateWithDefaultOptions();
  auto fake_agent = modular_testing::FakeAgent::CreateWithDefaultOptions();
  auto second_fake_agent = modular_testing::FakeAgent::CreateWithDefaultOptions();

  spec.mutable_sessionmgr_config()->mutable_session_agents()->push_back(fake_agent->url());
  spec.mutable_sessionmgr_config()->mutable_session_agents()->push_back(second_fake_agent->url());

  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptComponent(fake_module->BuildInterceptOptions());
  builder.InterceptComponent(fake_agent->BuildInterceptOptions());
  builder.InterceptComponent(second_fake_agent->BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  // Add the test mod.
  fuchsia::modular::Intent intent;
  intent.handler = fake_module->url();
  modular_testing::AddModToStory(test_harness(), "story_name", "mod_name", std::move(intent));
  RunLoopUntil([&] { return fake_module->is_running(); });

  // Connect to the first fake agent from the fake mod.
  fuchsia::sys::ServiceProviderPtr first_agent_services;
  fuchsia::modular::AgentControllerPtr first_agent_controller;
  fake_module->modular_component_context()->DeprecatedConnectToAgent(
      fake_agent->url(), first_agent_services.NewRequest(), first_agent_controller.NewRequest());
  RunLoopUntil([&] { return fake_agent->is_running(); });

  // Connect to the second fake agent from the first fake agent.
  fuchsia::sys::ServiceProviderPtr second_agent_services;
  fuchsia::modular::AgentControllerPtr second_agent_controller;
  fake_agent->modular_component_context()->DeprecatedConnectToAgent(
      second_fake_agent->url(), second_agent_services.NewRequest(),
      second_agent_controller.NewRequest());
  RunLoopUntil([&] { return second_fake_agent->is_running(); });
}

/// Tests that an attempt to connect to an agent that is a session agent succeeds.
TEST_F(ComponentContextTest, AttemptToConnectToSessionAgent) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  auto fake_module = modular_testing::FakeModule::CreateWithDefaultOptions();
  auto fake_agent = WasCreatedFakeAgent::CreateWithDefaultOptions();
  spec.mutable_sessionmgr_config()->mutable_session_agents()->push_back(fake_agent->url());

  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptComponent(fake_module->BuildInterceptOptions());
  builder.InterceptComponent(fake_agent->BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  // Add the test mod.
  fuchsia::modular::Intent intent;
  intent.handler = fake_module->url();
  modular_testing::AddModToStory(test_harness(), "story_name", "mod_name", std::move(intent));
  RunLoopUntil([&] { return fake_module->is_running(); });

  // Connect to the first fake agent from the fake mod.
  fuchsia::sys::ServiceProviderPtr first_agent_services;
  fuchsia::modular::AgentControllerPtr first_agent_controller;
  fake_module->modular_component_context()->DeprecatedConnectToAgent(
      fake_agent->url(), first_agent_services.NewRequest(), first_agent_controller.NewRequest());

  RunLoopUntil([&] { return fake_agent->is_running(); });
}

/// Tests that an attempt to connect to an agent that is not a session agent fails.
TEST_F(ComponentContextTest, AttemptToConnectToNonSessionAgent) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  auto fake_module = modular_testing::FakeModule::CreateWithDefaultOptions();
  auto fake_agent = WasCreatedFakeAgent::CreateWithDefaultOptions();

  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptComponent(fake_module->BuildInterceptOptions());
  builder.InterceptComponent(fake_agent->BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  // Add the test mod.
  fuchsia::modular::Intent intent;
  intent.handler = fake_module->url();
  modular_testing::AddModToStory(test_harness(), "story_name", "mod_name", std::move(intent));
  RunLoopUntil([&] { return fake_module->is_running(); });

  // Connect to the first fake agent from the fake mod.
  fuchsia::sys::ServiceProviderPtr first_agent_services;
  fuchsia::modular::AgentControllerPtr first_agent_controller;
  fake_module->modular_component_context()->DeprecatedConnectToAgent(
      fake_agent->url(), first_agent_services.NewRequest(), first_agent_controller.NewRequest());

  bool agent_controller_dropped = false;
  first_agent_controller.set_error_handler(
      [&](zx_status_t status) { agent_controller_dropped = true; });

  RunLoopUntil([&] { return agent_controller_dropped; });

  ASSERT_FALSE(fake_agent->was_created());
}

}  // namespace
