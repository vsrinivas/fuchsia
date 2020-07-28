// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_SESSION_LAUNCHER_COMPONENT_H_
#define SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_SESSION_LAUNCHER_COMPONENT_H_

#include <fuchsia/modular/session/cpp/fidl.h>
#include <lib/modular/testing/cpp/fake_component.h>

namespace modular_testing {

// Session launcher component fake that provides access to |fuchsia.modular.session.Launcher|
class FakeSessionLauncherComponent : public modular_testing::FakeComponent {
 public:
  explicit FakeSessionLauncherComponent(FakeComponent::Args args);

  ~FakeSessionLauncherComponent() override;

  // Instantiates a FakeSessionLauncherComponent with a randomly generated URL and default sandbox
  // services (see GetDefaultSandboxServices()).
  static std::unique_ptr<FakeSessionLauncherComponent> CreateWithDefaultOptions();

  // Returns the default list of services (capabilities) a session component expects in its
  // namespace. This method is useful when setting up a session component for interception.
  //
  // Default services:
  //  * fuchsia.modular.session.Launcher
  static std::vector<std::string> GetDefaultSandboxServices();

  // Requires: FakeComponent::is_running()
  fuchsia::modular::session::Launcher* launcher() { return launcher_.get(); }

 protected:
  // |modular_testing::FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override;

 private:
  fuchsia::modular::session::LauncherPtr launcher_;
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_SESSION_LAUNCHER_COMPONENT_H_
