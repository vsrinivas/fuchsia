// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_TESTS_SEMANTICS_INTEGRATION_TEST_FIXTURE_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_TESTS_SEMANTICS_INTEGRATION_TEST_FIXTURE_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include <memory>

#include "src/ui/a11y/lib/view/view_manager.h"

namespace accessibility_test {

// Test fixture that sets up an owned instance of semantics manager to run against.
//
// It publishes a SemanticsManager service in the test environment to allow components
// that use accessibility to connect to it. For components that are launched from the
// tests it provides methods to create a presentation and view holder token and method
// to retrieve the koid of a view launched by the test.
class SemanticsIntegrationTest : public sys::testing::TestWithEnvironment {
 public:
  SemanticsIntegrationTest(const std::string& environment_label);

  // |testing::Test|
  void SetUp() override;

  // Configures services available to the test environment. This method is called by |SetUp()|. It
  // shadows but calls |TestWithEnvironment::CreateServices()|. In addition the default
  // implementation wires up SemanticsManager.
  virtual void CreateServices(std::unique_ptr<sys::testing::EnvironmentServices>& services) {}

  a11y::ViewManager* view_manager() { return &view_manager_; }
  sys::testing::EnclosingEnvironment* environment() { return environment_.get(); }

  fuchsia::ui::views::ViewToken CreatePresentationViewToken();

  // Runs the event loop and sends taps on the center of the screen until a view ref KOID is
  // registered. May return ZX_KOID_INVALID if no KOID is intercepted after repeated attempts.
  zx_koid_t WaitForKoid();

  fuchsia::ui::scenic::Scenic* scenic() { return scenic_.get(); }

 private:
  const std::string environment_label_;
  std::unique_ptr<sys::ComponentContext> const component_context_;

  a11y::ViewManager view_manager_;
  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticsManager> semantics_manager_bindings_;

  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TESTS_SEMANTICS_INTEGRATION_TEST_FIXTURE_H_
