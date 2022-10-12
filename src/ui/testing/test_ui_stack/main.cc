// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/element/cpp/fidl.h>
#include <fuchsia/input/interaction/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/display/singleton/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/input3/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/shortcut/cpp/fidl.h>
#include <fuchsia/ui/shortcut2/cpp/fidl.h>
#include <fuchsia/ui/test/input/cpp/fidl.h>
#include <fuchsia/ui/test/scene/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <cstring>

#include <test/inputsynthesis/cpp/fidl.h>

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

  // Check for unsupported Flatland x Root Presenter configuration.
  FX_DCHECK(!test_ui_stack_config.use_flatland() || test_ui_stack_config.use_scene_manager())
      << "Unsupported UI configuration: Flatland x Root Presenter.";

  config.use_flatland = test_ui_stack_config.use_flatland();
  config.scene_owner = test_ui_stack_config.use_scene_manager()
                           ? ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER
                           : ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER;
  config.accessibility_owner = ui_testing::UITestRealm::AccessibilityOwnerType::FAKE;
  config.use_input = true;
  config.display_rotation = test_ui_stack_config.display_rotation();

  // Build test realm.
  ui_testing::UITestRealm realm(config);
  realm.Build();
  auto realm_exposed_services = realm.CloneExposedServicesDirectory();

  // Bind incoming service requests to realm's exposed services directory.

  // Base UI services.
  AddPublicService<fuchsia::accessibility::semantics::SemanticsManager>(
      context.get(), realm_exposed_services.get());
  AddPublicService<fuchsia::element::GraphicalPresenter>(context.get(),
                                                         realm_exposed_services.get());
  AddPublicService<fuchsia::input::interaction::Notifier>(context.get(),
                                                          realm_exposed_services.get());
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
  AddPublicService<fuchsia::ui::shortcut2::Registry>(context.get(), realm_exposed_services.get());
  AddPublicService<fuchsia::ui::pointerinjector::Registry>(context.get(),
                                                           realm_exposed_services.get());
  AddPublicService<fuchsia::ui::composition::Screenshot>(context.get(),
                                                         realm_exposed_services.get());
  AddPublicService<fuchsia::ui::display::singleton::Info>(context.get(),
                                                          realm_exposed_services.get());

  // Helper services.
  AddPublicService<fuchsia::ui::test::input::Registry>(context.get(), realm_exposed_services.get());
  AddPublicService<fuchsia::ui::test::scene::Controller>(context.get(),
                                                         realm_exposed_services.get());

  // Input-synthesis services.
  // TODO(fxbug.dev/107054): Remove these as soon as they are replaceable by
  // fuchsia.ui.test.input, which is the preferred testing library.
  AddPublicService<test::inputsynthesis::Mouse>(context.get(), realm_exposed_services.get());
  AddPublicService<test::inputsynthesis::Text>(context.get(), realm_exposed_services.get());

  context->outgoing()->ServeFromStartupInfo();

  loop.Run();
  return 0;

  FX_LOGS(INFO) << "Test UI stack exiting";
}

}  // namespace

int main(int argc, const char** argv) { return run_test_ui_stack(argc, argv); }
