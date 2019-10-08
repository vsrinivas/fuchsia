// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"

#include <fuchsia/modular/cpp/fidl.h>

namespace modular_testing {

FakeSessionShell::FakeSessionShell(FakeComponent::Args args) : FakeComponent(std::move(args)) {}

FakeSessionShell::~FakeSessionShell() = default;

// static
std::unique_ptr<FakeSessionShell> FakeSessionShell::CreateWithDefaultOptions() {
  return std::make_unique<FakeSessionShell>(modular_testing::FakeComponent::Args{
      .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
      .sandbox_services = FakeSessionShell::GetDefaultSandboxServices()});
}

// static
std::vector<std::string> FakeSessionShell::GetDefaultSandboxServices() {
  return {fuchsia::modular::ComponentContext::Name_, fuchsia::modular::SessionShellContext::Name_,
          fuchsia::modular::PuppetMaster::Name_};
}

void FakeSessionShell::OnCreate(fuchsia::sys::StartupInfo startup_info) {
  component_context()->svc()->Connect(session_shell_context_.NewRequest());
  session_shell_context_->GetStoryProvider(story_provider_.NewRequest());

  component_context()->outgoing()->AddPublicService(session_shell_impl_.GetHandler());
}

}  // namespace modular_testing
