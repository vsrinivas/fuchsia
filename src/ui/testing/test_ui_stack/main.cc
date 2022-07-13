// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/input3/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/shortcut/cpp/fidl.h>
#include <fuchsia/ui/test/scene/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <cstring>

#include "src/ui/testing/test_ui_stack/test_ui_stack_config_lib.h"
#include "src/ui/testing/ui_test_realm/ui_test_realm.h"

namespace {

template <typename T>
void AddPublicService(sys::ComponentContext* context,
                      sys::ServiceDirectory* realm_exposed_services) {
  FX_CHECK(realm_exposed_services);

  context->outgoing()->AddPublicService(
      fidl::InterfaceRequestHandler<T>([realm_exposed_services](fidl::InterfaceRequest<T> request) {
        realm_exposed_services->Connect(std::move(request));
      }));
}

int run_test_ui_stack(int argc, const char** argv) {
  FX_LOGS(INFO) << "Test UI stack starting";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  // Read component configuration, and convert to UITestRealm::Config.
  auto test_ui_stack_config = test_ui_stack_config_lib::Config::TakeFromStartupHandle();
  ui_testing::UITestRealm::Config config;
  if (test_ui_stack_config.use_modern_ui_stack()) {
    config.use_flatland = true;
    config.scene_owner = ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER;
    config.use_input = true;
    config.accessibility_owner = ui_testing::UITestRealm::AccessibilityOwnerType::FAKE;
  } else {
    config.scene_owner = ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER;
    config.use_input = true;
    config.accessibility_owner = ui_testing::UITestRealm::AccessibilityOwnerType::FAKE;
  }

  // Build test realm.
  ui_testing::UITestRealm realm(config);
  realm.Build();
  auto realm_exposed_services = realm.CloneExposedServicesDirectory();

  // Bind incoming service requests to realm's exposed services directory.

  // Base UI services.
  AddPublicService<fuchsia::accessibility::semantics::SemanticsManager>(
      context.get(), realm_exposed_services.get());
  AddPublicService<fuchsia::ui::composition::Allocator>(context.get(),
                                                        realm_exposed_services.get());
  AddPublicService<fuchsia::ui::composition::Flatland>(context.get(), realm_exposed_services.get());
  AddPublicService<fuchsia::ui::scenic::Scenic>(context.get(), realm_exposed_services.get());
  AddPublicService<fuchsia::ui::input::ImeService>(context.get(), realm_exposed_services.get());
  AddPublicService<fuchsia::ui::input3::Keyboard>(context.get(), realm_exposed_services.get());
  AddPublicService<fuchsia::ui::input3::KeyEventInjector>(context.get(),
                                                          realm_exposed_services.get());
  AddPublicService<fuchsia::ui::shortcut::Manager>(context.get(), realm_exposed_services.get());
  AddPublicService<fuchsia::ui::shortcut::Registry>(context.get(), realm_exposed_services.get());

  // Helper services.
  AddPublicService<fuchsia::ui::test::scene::Provider>(context.get(), realm_exposed_services.get());

  context->outgoing()->ServeFromStartupInfo();

  loop.Run();
  return 0;

  FX_LOGS(INFO) << "Test UI stack exiting";
}

}  // namespace

int main(int argc, const char** argv) { return run_test_ui_stack(argc, argv); }
