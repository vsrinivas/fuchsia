// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/modular_test_harness/cpp/fake_agent.h>

namespace modular {
namespace testing {

FakeAgent::FakeAgent() = default;

FakeAgent::FakeAgent(
    fit::function<void(std::string client_url)> connect_callback)
    : on_connect_(std::move(connect_callback)) {}

FakeAgent::~FakeAgent() = default;

// |modular::testing::FakeComponent|
void FakeAgent::OnCreate(fuchsia::sys::StartupInfo startup_info) {
  component_context()->svc()->Connect(component_context_.NewRequest());
  component_context()->svc()->Connect(agent_context_.NewRequest());

  component_context()->outgoing()->AddPublicService<fuchsia::modular::Agent>(
      bindings_.GetHandler(this));
}

void FakeAgent::set_on_run_task(
    fit::function<void(std::string task_id, RunTaskCallback callback)>
        on_run_task) {
  on_run_task_ = std::move(on_run_task);
}

// |fuchsia::modular::Agent|
void FakeAgent::Connect(
    std::string requestor_url,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> services_request) {
  services_.AddBinding(std::move(services_request));
  if (on_connect_) {
    on_connect_(requestor_url);
  }
}

// |fuchsia::modular::Agent|
void FakeAgent::RunTask(std::string task_id, RunTaskCallback callback) {
  if (on_run_task_) {
    on_run_task_(task_id, std::move(callback));
  }
}

std::vector<std::string> FakeAgent::GetSandboxServices() {
  return {"fuchsia.modular.ComponentContext", "fuchsia.modular.AgentContext"};
}

}  // namespace testing
}  // namespace modular
