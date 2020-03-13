// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular/testing/cpp/fake_agent.h>

#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {

class ComponentContextTest : public modular_testing::TestHarnessFixture {};

// Tests that an agent is able to start another agent through
// fuchsia::modular::ComponentContext.ConnectToAgent(). Asserts that closing
// fuchsia::modular::AgentController triggers the agent to stop.
TEST_F(ComponentContextTest, AgentStartsSecondAgent) {
  modular_testing::TestHarnessBuilder builder;

  auto fake_module = modular_testing::FakeModule::CreateWithDefaultOptions();
  builder.InterceptComponent(fake_module->BuildInterceptOptions());

  auto fake_agent = modular_testing::FakeAgent::CreateWithDefaultOptions();
  builder.InterceptComponent(fake_agent->BuildInterceptOptions());

  auto second_fake_agent = modular_testing::FakeAgent::CreateWithDefaultOptions();
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
  fake_module->modular_component_context()->ConnectToAgent(
      fake_agent->url(), first_agent_services.NewRequest(), first_agent_controller.NewRequest());
  RunLoopUntil([&] { return fake_agent->is_running(); });

  // Connect to the second fake agent from the first fake agent.
  fuchsia::sys::ServiceProviderPtr second_agent_services;
  fuchsia::modular::AgentControllerPtr second_agent_controller;
  fake_agent->modular_component_context()->ConnectToAgent(second_fake_agent->url(),
                                                          second_agent_services.NewRequest(),
                                                          second_agent_controller.NewRequest());
  RunLoopUntil([&] { return second_fake_agent->is_running(); });

  // Killing the agent controller should stop the agent.
  second_agent_controller.Unbind();
  RunLoopUntil([&] { return !second_fake_agent->is_running(); });

  first_agent_controller.Unbind();
  RunLoopUntil([&] { return !fake_agent->is_running(); });
}

}  // namespace
