// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_test_harness/cpp/fake_session_launcher_component.h"

#include <fuchsia/modular/session/cpp/fidl.h>

namespace modular_testing {

FakeSessionLauncherComponent::FakeSessionLauncherComponent(FakeComponent::Args args)
    : FakeComponent(std::move(args)) {}

FakeSessionLauncherComponent::~FakeSessionLauncherComponent() = default;

// static
std::unique_ptr<FakeSessionLauncherComponent>
FakeSessionLauncherComponent::CreateWithDefaultOptions() {
  return std::make_unique<FakeSessionLauncherComponent>(modular_testing::FakeComponent::Args{
      .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
      .sandbox_services = FakeSessionLauncherComponent::GetDefaultSandboxServices()});
}

// static
std::vector<std::string> FakeSessionLauncherComponent::GetDefaultSandboxServices() {
  return {fuchsia::modular::session::Launcher::Name_};
}

void FakeSessionLauncherComponent::OnCreate(fuchsia::sys::StartupInfo startup_info) {
  component_context()->svc()->Connect(launcher_.NewRequest());
}

}  // namespace modular_testing
