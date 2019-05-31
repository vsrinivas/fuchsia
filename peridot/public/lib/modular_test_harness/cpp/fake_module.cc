// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/modular_test_harness/cpp/fake_module.h>

namespace modular {
namespace testing {

FakeModule::FakeModule() = default;

FakeModule::FakeModule(
    fit::function<void(fuchsia::modular::Intent)> on_intent_handled)
    : on_intent_handled_(std::move(on_intent_handled)) {}

FakeModule::~FakeModule() = default;

// |modular::testing::FakeComponent|
void FakeModule::OnCreate(fuchsia::sys::StartupInfo startup_info) {
  component_context()->svc()->Connect(component_context_.NewRequest());
  component_context()->svc()->Connect(module_context_.NewRequest());
  module_context_.set_error_handler([this](zx_status_t err) {
    if (err != ZX_OK) {
      ZX_PANIC("Could not connect to ModuleContext service.");
    }
  });

  component_context()
      ->outgoing()
      ->AddPublicService<fuchsia::modular::IntentHandler>(
          [this](
              fidl::InterfaceRequest<fuchsia::modular::IntentHandler> request) {
            bindings_.AddBinding(this, std::move(request));
          });
}

std::vector<std::string> FakeModule::GetSandboxServices() {
  return {"fuchsia.modular.ComponentContext", "fuchsia.modular.ModuleContext"};
}

// |IntentHandler|
void FakeModule::HandleIntent(fuchsia::modular::Intent intent) {
  if (on_intent_handled_) {
    on_intent_handled_(std::move(intent));
  }
};

}  // namespace testing
}  // namespace modular
