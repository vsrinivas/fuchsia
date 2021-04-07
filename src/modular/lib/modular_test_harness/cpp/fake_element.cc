// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_test_harness/cpp/fake_element.h"

namespace modular_testing {

FakeElement::FakeElement(modular_testing::FakeComponent::Args args)
    : FakeComponent(std::move(args)) {
  spec_.set_component_url(url());
}

FakeElement::~FakeElement() = default;

// static
std::unique_ptr<FakeElement> FakeElement::CreateWithDefaultOptions() {
  return std::make_unique<FakeElement>(modular_testing::FakeComponent::Args{
      .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
      .sandbox_services = GetDefaultSandboxServices()});
}

// static
std::vector<std::string> FakeElement::GetDefaultSandboxServices() {
  return {"fuchsia.testing.modular.TestProtocol"};
}

// |modular_testing::FakeComponent|
void FakeElement::OnCreate(fuchsia::sys::StartupInfo startup_info) {
  on_create_(std::move(startup_info));
}

// |modular_testing::FakeComponent|
void FakeElement::OnDestroy() { on_destroy_(); }

}  // namespace modular_testing
