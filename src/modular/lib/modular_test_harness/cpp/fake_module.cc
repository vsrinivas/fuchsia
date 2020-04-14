// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/modular/testing/cpp/fake_component.h>

namespace modular_testing {

FakeModule::FakeModule(modular_testing::FakeComponent::Args args)
    : FakeComponent(std::move(args)) {}

FakeModule::~FakeModule() = default;

// static
std::unique_ptr<FakeModule> FakeModule::CreateWithDefaultOptions() {
  return std::make_unique<FakeModule>(
      modular_testing::FakeComponent::Args{
          .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
          .sandbox_services = FakeModule::GetDefaultSandboxServices()});
}

// static
std::vector<std::string> FakeModule::GetDefaultSandboxServices() {
  return {fuchsia::modular::ComponentContext::Name_, fuchsia::modular::ModuleContext::Name_};
}

// |modular_testing::FakeComponent|
void FakeModule::OnCreate(fuchsia::sys::StartupInfo startup_info) {
  component_context()->svc()->Connect(component_context_.NewRequest());
  component_context()->svc()->Connect(module_context_.NewRequest());
}

}  // namespace modular_testing
