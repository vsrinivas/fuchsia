// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_SETTINGS_INTL_H_
#define SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_SETTINGS_INTL_H_

#include <lib/modular/testing/cpp/fake_component.h>

#include <fuchsia/settings/cpp/fidl.h>
#include <sdk/lib/sys/cpp/component_context.h>

namespace modular_testing {

// Story shell fake that provides access to the StoryShellContext.
//
// EXAMPLE USAGE (see test_harness_fixture.h for more details on how to use the
// test harness):
//
// modular_testing::FakeStoryShell fake_story_shell(
//    {.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
//     .sandbox_services = {"fuchsia.modular.StoryShellContext"}});
//
// modular_testing::TestHarnessBuilder builder;
// builder.InterceptSessionShell(fake_story_shell.BuildInterceptOptions());
// builder.BuildAndRun(test_harness()));
//
// // Wait for the session shell to be intercepted.
// RunLoopUntil([&] { return fake_story_shell.is_running(); });
class FakeSettingsIntl : public modular_testing::FakeComponent, fuchsia::settings::Intl {
 public:
  explicit FakeSettingsIntl(FakeComponent::Args args);
  ~FakeSettingsIntl() override;

  // Instantiates a FakeStoryShell with a randomly generated URL and default sandbox services
  // (see GetDefaultSandboxServices()).
  static std::unique_ptr<FakeSettingsIntl> CreateWithDefaultOptions();

  // Produces a handler function that can be used in the outgoing service
  // provider.
  fidl::InterfaceRequestHandler<fuchsia::settings::Intl> GetHandler();

 private:
  // |modular_testing::FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override;

  // |modular_testing::FakeComponent|
  void OnDestroy() override;

  // |fuchsia::settings::Intl|
  void Watch(WatchCallback callback) override;

  // |fuchsia::settings::Intl|
  void Set(fuchsia::settings::IntlSettings settings, SetCallback callback) override;

  fidl::BindingSet<fuchsia::settings::Intl> bindings_;
  fuchsia::settings::IntlSettingsPtr intl_settings_ptr;
  WatchCallback watch_callback_ = nullptr;
  std::unique_ptr<fuchsia::settings::IntlSettings> settings_;
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_SETTINGS_INTL_H_
