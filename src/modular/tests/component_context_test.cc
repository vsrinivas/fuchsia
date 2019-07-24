// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular_test_harness/cpp/fake_agent.h>
#include <lib/modular_test_harness/cpp/fake_module.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <src/lib/fxl/logging.h>

namespace {

class ComponentContextTest : public modular::testing::TestHarnessFixture {};

// Tests that an agent is able to start another agent through
// fuchsia::modular::ComponentContext.ConnectToAgent(). Asserts that closing
// fuchsia::modular::AgentController triggers the agent to stop.
TEST_F(ComponentContextTest, AgentStartsSecondAgent) {
  modular_testing::TestHarnessBuilder builder;

  modular::testing::FakeModule fake_module;
  const auto fake_module_url = modular_testing::TestHarnessBuilder::GenerateFakeUrl();
  builder.InterceptComponent(
      fake_module.GetOnCreateHandler(),
      {.url = fake_module_url,
       .sandbox_services = modular::testing::FakeModule::GetSandboxServices()});

  modular::testing::FakeAgent first_fake_agent;
  const auto first_fake_agent_url = modular_testing::TestHarnessBuilder::GenerateFakeUrl();
  builder.InterceptComponent(
      first_fake_agent.GetOnCreateHandler(),
      {.url = first_fake_agent_url,
       .sandbox_services = modular::testing::FakeAgent::GetSandboxServices()});

  modular::testing::FakeAgent second_fake_agent;
  const auto second_fake_agent_url = modular_testing::TestHarnessBuilder::GenerateFakeUrl();
  builder.InterceptComponent(
      second_fake_agent.GetOnCreateHandler(),
      {.url = second_fake_agent_url,
       .sandbox_services = modular::testing::FakeAgent::GetSandboxServices()});

  builder.BuildAndRun(test_harness());

  // Add the test mod.
  fuchsia::modular::Intent intent;
  intent.handler = fake_module_url;
  modular::testing::AddModToStory(test_harness(), "story_name", "mod_name", std::move(intent));
  RunLoopUntil([&] { return fake_module.is_running(); });

  // Connect to the first fake agent from the fake mod.
  fuchsia::sys::ServiceProviderPtr first_agent_services;
  fuchsia::modular::AgentControllerPtr first_agent_controller;
  fake_module.modular_component_context()->ConnectToAgent(
      first_fake_agent_url, first_agent_services.NewRequest(), first_agent_controller.NewRequest());
  RunLoopUntil([&] { return first_fake_agent.is_running(); });

  // Connect to the second fake agent from the first fake agent.
  fuchsia::sys::ServiceProviderPtr second_agent_services;
  fuchsia::modular::AgentControllerPtr second_agent_controller;
  first_fake_agent.modular_component_context()->ConnectToAgent(
      second_fake_agent_url, second_agent_services.NewRequest(),
      second_agent_controller.NewRequest());
  RunLoopUntil([&] { return second_fake_agent.is_running(); });

  // Killing the agent controller should stop the agent.
  second_agent_controller.Unbind();
  RunLoopUntil([&] { return !second_fake_agent.is_running(); });

  first_agent_controller.Unbind();
  RunLoopUntil([&] { return !first_fake_agent.is_running(); });
}

}  // namespace
