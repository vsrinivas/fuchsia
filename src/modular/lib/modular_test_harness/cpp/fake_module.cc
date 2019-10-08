// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"

#include <fuchsia/modular/cpp/fidl.h>

#include "src/modular/lib/modular_test_harness/cpp/fake_component.h"

namespace modular_testing {

FakeModule::FakeModule(modular_testing::FakeComponent::Args args,
                       fit::function<void(fuchsia::modular::Intent)> on_intent_handled)
    : FakeComponent(std::move(args)), on_intent_handled_(std::move(on_intent_handled)) {}

FakeModule::~FakeModule() = default;

// static
std::unique_ptr<FakeModule> FakeModule::CreateWithDefaultOptions() {
  return std::make_unique<FakeModule>(
      modular_testing::FakeComponent::Args{
          .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
          .sandbox_services = FakeModule::GetDefaultSandboxServices()},
      [](fuchsia::modular::Intent) {});
}

// static
std::vector<std::string> FakeModule::GetDefaultSandboxServices() {
  return {fuchsia::modular::ComponentContext::Name_, fuchsia::modular::ModuleContext::Name_};
}

// |modular_testing::FakeComponent|
void FakeModule::OnCreate(fuchsia::sys::StartupInfo startup_info) {
  component_context()->svc()->Connect(component_context_.NewRequest());
  component_context()->svc()->Connect(module_context_.NewRequest());

  component_context()->outgoing()->AddPublicService<fuchsia::modular::IntentHandler>(
      [this](fidl::InterfaceRequest<fuchsia::modular::IntentHandler> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

// |IntentHandler|
void FakeModule::HandleIntent(fuchsia::modular::Intent intent) {
  if (on_intent_handled_) {
    on_intent_handled_(std::move(intent));
  }
};

}  // namespace modular_testing
