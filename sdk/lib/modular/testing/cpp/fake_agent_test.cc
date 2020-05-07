// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/test/harness/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular/testing/cpp/fake_agent.h>

#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

class FakeAgentTest : public modular_testing::TestHarnessFixture,
                      public fuchsia::modular::test::harness::Pinger {
 protected:
  bool pinged() const { return pinged_; }

 private:
  // |fuchsia::modular::test::harness::Pinger|
  void Ping() override { pinged_ = true; }

  bool pinged_ = false;
};

// Test that we can call AddAgentService() even before the test harness is run, and that the service
// can be acquired once the agent is launched.
TEST_F(FakeAgentTest, AddAgentService) {
  fidl::BindingSet<fuchsia::modular::test::harness::Pinger> pinger_bindings;
  auto fake_agent = modular_testing::FakeAgent::CreateWithDefaultOptions();
  fake_agent->AddAgentService<fuchsia::modular::test::harness::Pinger>(
      pinger_bindings.GetHandler(this));

  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_sessionmgr_config()->mutable_session_agents()->push_back(fake_agent->url());

  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptComponent(fake_agent->BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  fuchsia::modular::ComponentContextPtr component_context;
  test_harness()->ConnectToModularService(
      std::move(fuchsia::modular::testing::ModularService().set_component_context(
          component_context.NewRequest())));

  fuchsia::modular::AgentControllerPtr agent_controller;
  fuchsia::modular::test::harness::PingerPtr pinger;
  fuchsia::modular::AgentServiceRequest req;
  req.set_service_name(fuchsia::modular::test::harness::Pinger::Name_);
  req.set_channel(pinger.NewRequest().TakeChannel());
  req.set_handler(fake_agent->url());
  req.set_agent_controller(agent_controller.NewRequest());
  component_context->DeprecatedConnectToAgentService(std::move(req));

  EXPECT_FALSE(pinged());
  pinger->Ping();
  RunLoopUntil([this] { return pinged(); });
}
