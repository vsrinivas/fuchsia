// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_AGENT_H_
#define SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_AGENT_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular/cpp/agent.h>

#include "src/modular/lib/modular_test_harness/cpp/fake_component.h"

namespace modular {
namespace testing {

// An Agent implementation which provides access to
// |fuchsia::modular::AgentContext|, implements boilerplate for exposing
// services, and exposes task running callback registration.
class FakeAgent : public FakeComponent {
 public:
  explicit FakeAgent(FakeComponent::Args args);
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
    agent_->AddService<Interface>(std::move(handler));
  }

 private:
  // |FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override;

  fuchsia::modular::ComponentContextPtr modular_component_context_;
  fuchsia::modular::AgentContextPtr agent_context_;

  std::unique_ptr<modular::Agent> agent_;
};

}  // namespace testing
}  // namespace modular

#endif  // SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_AGENT_H_
