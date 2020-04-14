// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_MODULE_H_
#define SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_MODULE_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/modular/testing/cpp/fake_component.h>

namespace modular_testing {

// A fake module that exposes an IntentHandler service that can be used with
// TestHarnessBuilder. Refer to detailed documentation at test_harness_fixture.h.
//
// EXAMPLE USAGE:
//
// ...
// modular_testing::TestHarnessBuilder builder;
// auto fake_module = modular_testing::FakeModule::CreateWithDefaultOptions();
// builder.InterceptComponent(fake_module->BuildInterceptOptions());
// builder.BuildAndRun(test_harness());
// ...
class FakeModule : public modular_testing::FakeComponent {
 public:
  explicit FakeModule(modular_testing::FakeComponent::Args args);

  ~FakeModule() override;

  // Instantiates a FakeModule with a randomly generated URL, default sandbox
  // services (see GetDefaultSandboxServices()).
  static std::unique_ptr<FakeModule> CreateWithDefaultOptions();

  // Returns the default list of services (capabilities) a module expects in its namespace.
  // This method is useful when setting up a module for interception.
  //
  // Default services:
  //  * fuchsia.modular.ComponentContext
  //  * fuchsia.modular.ModuleContext
  static std::vector<std::string> GetDefaultSandboxServices();

  fuchsia::modular::ComponentContext* modular_component_context() {
    return component_context_.get();
  }

  // Returns the module's |module_context_|
  fuchsia::modular::ModuleContext* module_context() { return module_context_.get(); }

 protected:
  // |FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override;

 private:
  // A callback to be executed when HandleIntent() is invoked.
  fuchsia::modular::ComponentContextPtr component_context_;
  fuchsia::modular::ModuleContextPtr module_context_;
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_MODULE_H_
