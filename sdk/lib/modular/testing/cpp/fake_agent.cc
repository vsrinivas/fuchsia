// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/modular/testing/cpp/fake_agent.h>

namespace modular_testing {

FakeAgent::FakeAgent(FakeComponent::Args args) : FakeComponent(std::move(args)) {}

FakeAgent::~FakeAgent() = default;

// static
std::unique_ptr<FakeAgent> FakeAgent::CreateWithDefaultOptions() {
  return std::make_unique<FakeAgent>(modular_testing::FakeComponent::Args{
      .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
      .sandbox_services = FakeAgent::GetDefaultSandboxServices()});
}

// static
std::vector<std::string> FakeAgent::GetDefaultSandboxServices() {
  return {fuchsia::modular::ComponentContext::Name_};
}

// |modular_testing::FakeComponent|
void FakeAgent::OnCreate(fuchsia::sys::StartupInfo startup_info) {
  FakeComponent::OnCreate(std::move(startup_info));

  component_context()->svc()->Connect(modular_component_context_.NewRequest());
  agent_ = std::make_unique<modular::Agent>(component_context()->outgoing(),
                                            /* on_terminate */
                                            [this] {
                                              Exit(0);
                                              // |OnDestroy| is invoked at this point.
                                            });
  FlushAddAgentServiceIfRunning();
}

void FakeAgent::FlushAddAgentServiceIfRunning() {
  if (is_running()) {
    for (auto& call : buffered_add_agent_service_calls_) {
      call();
    }
    buffered_add_agent_service_calls_.clear();
  }
}

}  // namespace modular_testing
