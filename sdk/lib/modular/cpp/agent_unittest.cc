// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/modular/cpp/agent.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <test/modular/agent/cpp/fidl.h>

class AgentTest : public gtest::RealLoopFixture, public test::modular::agent::Pinger {
 protected:
  int ping_count() { return ping_count_; }

 private:
  // |test::modular::agent::Pinger|
  void Ping() override { ping_count_++; }

  int ping_count_ = 0;
};

// Test that a Lifecycle/Terminate signal will cause Agent and Lifecycle interfaces to close out and
// a termination callback to be called.
TEST_F(AgentTest, LifecycleTerminate) {
  sys::testing::ComponentContextProvider ctx_provider;

  bool terminated = false;
  modular::Agent agent(ctx_provider.context()->outgoing(), [&terminated] { terminated = true; });

  auto lifecycle_ptr = ctx_provider.ConnectToPublicService<fuchsia::modular::Lifecycle>();
  auto agent_ptr = ctx_provider.ConnectToPublicService<fuchsia::modular::Agent>();

  lifecycle_ptr->Terminate();

  // Check that |on_terminate| was invoked, agent & lifecycle interfaces are closed.
  RunLoopUntil([&] { return terminated && !lifecycle_ptr && !agent_ptr; });
}

// Test that modular::Agent::AddService<>() adds services which can be acquired using
// |Agent.Connect|.
TEST_F(AgentTest, AddService) {
  sys::testing::ComponentContextProvider ctx_provider;

  // Setup the agent and expose the pinger service.
  modular::Agent agent(ctx_provider.context()->outgoing(), [] {});
  fidl::BindingSet<test::modular::agent::Pinger> pinger_bindings;
  agent.AddService<test::modular::agent::Pinger>(pinger_bindings.GetHandler(this));

  // Connect to the Pinger agent service.
  auto agent_ptr = ctx_provider.ConnectToPublicService<fuchsia::modular::Agent>();
  fuchsia::sys::ServiceProviderPtr sp;
  agent_ptr->Connect("", sp.NewRequest());
  test::modular::agent::PingerPtr pinger;
  sp->ConnectToService(test::modular::agent::Pinger::Name_, pinger.NewRequest().TakeChannel());
  pinger->Ping();

  // Check that Pinger/Ping() was received;  this should verify that Pinger was registered
  RunLoopUntil([this]() { return ping_count() > 0; });
}

TEST_F(AgentTest, ReentrentDestroy) {
  sys::testing::ComponentContextProvider ctx_provider;

  std::unique_ptr<modular::Agent> agent;
  auto val = std::make_unique<int>(42);
  agent =
      std::make_unique<modular::Agent>(ctx_provider.context()->outgoing(),
                                       /* on_terminate */ [&agent, val = std::move(val)]() mutable {
                                         agent.reset();
                                         // this should abort/crash if |on_terminate| wasn't
                                         // reentrant after |agent| is destroyed:
                                         *val.get() += 1;
                                       });

  auto lifecycle_ptr = ctx_provider.ConnectToPublicService<fuchsia::modular::Lifecycle>();
  lifecycle_ptr->Terminate();

  RunLoopUntil([&] { return !lifecycle_ptr; });
}
