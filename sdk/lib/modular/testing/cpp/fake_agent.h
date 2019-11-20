// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULAR_TESTING_CPP_FAKE_AGENT_H_
#define LIB_MODULAR_TESTING_CPP_FAKE_AGENT_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular/cpp/agent.h>
#include <lib/modular/testing/cpp/fake_component.h>

namespace modular_testing {

// FakeAgent is a utility class for intercepting Agent components. This class is designed to be used
// alongside modular_testing::TestHarnessBuilder::InterceptComponent(). Clients may instantiate this
// class directly, or sub-class and override OnCreate() and OnDestroy().
//
// USAGE:
// ======
// * Pass BuildInterceptOptions() to TestHarnessBuilder::InterceptComponent() to route the
//   component's launch to this instance.
// * Use is_running() to determine if the agent is running.
// * Use AddAgentService<>() to publish agent services.
// * Use component_context() to connect to incoming services, and add public services.
//
// EXAMPLE:
// ========
// ..
// modular_testing::TestHarnessBuilder builder;
// auto fake_agent = modular_testing::FakeAgent::CreateWithDefaultOptions();
// builder.InterceptComponent(fake_agent.BuildInterceptOptions());
// builder.BuildAndRun(test_harness());
// ..
class FakeAgent : public FakeComponent {
 public:
  FakeAgent() = delete;
  explicit FakeAgent(FakeComponent::Args args);

  FakeAgent(const FakeAgent&) = delete;
  FakeAgent(FakeAgent&&) = delete;
  void operator=(const FakeAgent&) = delete;
  void operator=(FakeAgent&&) = delete;

  ~FakeAgent() override;

  // Instantiates a FakeAgent with a randomly generated URL, default sandbox services (see
  // GetDefaultSandboxServices()).
  static std::unique_ptr<FakeAgent> CreateWithDefaultOptions();

  // Returns the default list of services (capabilities) an agent expects in its namespace.
  // This method is useful when setting up an agent for interception.
  //
  // Default services:
  //  * fuchsia.modular.ComponentContext
  //  * fuchsia.modular.AgentContext
  static std::vector<std::string> GetDefaultSandboxServices();

  // Returns the agent's |fuchsia::modular::ComponentContext|.
  fuchsia::modular::ComponentContext* modular_component_context() {
    return modular_component_context_.get();
  }

  // Returns the agent's |fuchsia::modular::AgentContext|.
  fuchsia::modular::AgentContext* agent_context() { return agent_context_.get(); }

  // Adds a service to the service namespace which is exposed to clients
  // connecting to the agent.
  template <typename Interface>
  void AddAgentService(fidl::InterfaceRequestHandler<Interface> handler) {
    // Buffer the AddAgentService calls until the agent is actually launched.
    buffered_add_agent_service_calls_.push_back([this, handler = std::move(handler)]() mutable {
      agent_->AddService<Interface>(std::move(handler));
    });

    FlushAddAgentServiceIfRunning();
  }

 private:
  // |FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override;

  // Process the pending AddAgentService() calls which were buffered until is_running() == true.
  void FlushAddAgentServiceIfRunning();

  fuchsia::modular::ComponentContextPtr modular_component_context_;
  fuchsia::modular::AgentContextPtr agent_context_;

  std::vector<fit::closure> buffered_add_agent_service_calls_;

  std::unique_ptr<modular::Agent> agent_;
};

}  // namespace modular_testing

#endif  // LIB_MODULAR_TESTING_CPP_FAKE_AGENT_H_
