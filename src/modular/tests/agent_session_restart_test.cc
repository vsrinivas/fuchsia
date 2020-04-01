// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <fuchsia/testing/modular/cpp/fidl.h>

#include <sdk/lib/modular/testing/cpp/fake_agent.h>

#include "gmock/gmock.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {

const std::string kTestAgentUrl("fuchsia-pkg://fuchsia.com/fake_agent#meta/fake_agent.cmx");
const std::string kTestServiceName(fuchsia::testing::modular::TestProtocol::Name_);

class AgentSessionRestartTest : public modular_testing::TestHarnessFixture {
 protected:
  AgentSessionRestartTest() {}

  modular_testing::TestHarnessBuilder::InterceptOptions AddSandboxServices(
      std::vector<std::string> service_names,
      modular_testing::TestHarnessBuilder::InterceptOptions options) {
    for (auto service_name : service_names) {
      options.sandbox_services.push_back(service_name);
    }
    return options;
  }
};

// Test that an Agent can use the SessionRestartController protocol to restart the session.
TEST_F(AgentSessionRestartTest, AgentCanRestartSession) {
  auto agent = modular_testing::FakeAgent::CreateWithDefaultOptions();
  auto session_shell = modular_testing::FakeSessionShell::CreateWithDefaultOptions();

  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_sessionmgr_config()->mutable_session_agents()->push_back(agent->url());

  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptSessionShell(session_shell->BuildInterceptOptions());
  builder.InterceptComponent(AddSandboxServices({fuchsia::modular::SessionRestartController::Name_},
                                                agent->BuildInterceptOptions()));
  builder.BuildAndRun(test_harness());

  // Use the session shell's startup to indicate that the runtime is up.
  RunLoopUntil([&] { return session_shell->is_running() && agent->is_running(); });

  // Issue a restart command from the Agent.
  auto session_restart_controller =
      agent->component_context()->svc()->Connect<fuchsia::modular::SessionRestartController>();
  session_restart_controller->Restart();

  // Wait for the session shell to die (indicating a restart), then wait for it to come back.
  RunLoopUntil([&] { return !session_shell->is_running(); });
  RunLoopUntil([&] { return session_shell->is_running(); });
}

}  // namespace
