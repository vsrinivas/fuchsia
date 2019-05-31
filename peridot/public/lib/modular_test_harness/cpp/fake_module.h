// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULAR_TEST_HARNESS_CPP_FAKE_MODULE_H_
#define LIB_MODULAR_TEST_HARNESS_CPP_FAKE_MODULE_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular_test_harness/cpp/fake_component.h>
#include <src/lib/fxl/logging.h>

namespace modular {
namespace testing {

// A fake module that exposes an IntentHandler service that can be used with
// the TestHarness. Refer to detailed documentation at test_harness_fixture.h.
//
// EXAMPLE USAGE
// ...
//
// FakeModule fake_module;
//
// builder.InterceptComponent(
//   fake_module->GetOnCreateHandler(),
//   { .url = fake_module_url,
//     .sandbox_services = fake_module->GetSandboxServices() });
//
// ...
class FakeModule : public modular::testing::FakeComponent,
                   fuchsia::modular::IntentHandler {
 public:
  FakeModule();

  // |on_intent_handled| will be invoked whenever HandleIntent() is called.
  FakeModule(fit::function<void(fuchsia::modular::Intent)> on_intent_handled);

  ~FakeModule();

  fuchsia::modular::ComponentContext* modular_component_context() {
    return component_context_.get();
  }

  // Returns the module's |module_context_|
  fuchsia::modular::ModuleContext* module_context() {
    return module_context_.get();
  }

  static std::vector<std::string> GetSandboxServices();

 private:
  // |FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override;

  // |IntentHandler|
  void HandleIntent(fuchsia::modular::Intent intent) override;

  // A callback to be executed when HandleIntent() is invoked.
  fit::function<void(fuchsia::modular::Intent)> on_intent_handled_;
  fuchsia::modular::ComponentContextPtr component_context_;
  fuchsia::modular::ModuleContextPtr module_context_;
  fidl::BindingSet<fuchsia::modular::IntentHandler> bindings_;
};

}  // namespace testing
}  // namespace modular

#endif  // LIB_MODULAR_TEST_HARNESS_CPP_FAKE_MODULE_H_
