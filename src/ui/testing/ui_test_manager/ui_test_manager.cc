// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/ui_test_manager/ui_test_manager.h"

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/input/injection/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/session/scene/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/configuration/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/ui/testing/ui_test_manager/gfx_root_presenter_scene.h"
#include "src/ui/testing/ui_test_manager/gfx_scene_manager_scene.h"

namespace ui_testing {

namespace {

using sys::testing::Capability;
using sys::testing::ChildRef;
using sys::testing::LocalComponent;
using sys::testing::LocalComponentHandles;
using sys::testing::ParentRef;
using sys::testing::Protocol;
using sys::testing::Ref;
using sys::testing::Route;

// Base realm urls.
constexpr auto kRootPresenterSceneUrl = "#meta/root_presenter_scene.cm";
constexpr auto kRootPresenterSceneWithInputUrl = "#meta/root_presenter_scene_with_input.cm";
constexpr auto kSceneManagerSceneUrl = "#meta/scene_manager_scene.cm";

// System component urls.
constexpr auto kFakeA11yManagerUrl = "#meta/fake-a11y-manager.cm";

constexpr auto kTestRealmName = "test-realm";
constexpr auto kClientSubrealmName = "client-subrealm";

constexpr auto kA11yManagerName = "a11y-manager";

}  // namespace

UITestManager::UITestManager(UITestManager::Config config) : config_(config) {
  FX_CHECK(!config_.use_flatland) << "Flatland not currently supported";
  FX_CHECK(config_.scene_owner.has_value()) << "Null scene owner not currently supported";
}

void UITestManager::SetUseFlatlandConfig(bool use_flatland) {
  // Not yet implemented.
}

component_testing::Realm UITestManager::AddSubrealm() {
  return realm_builder_.AddChildRealm(kClientSubrealmName);
}

void UITestManager::AddBaseRealmComponent() {
  std::string url = "";
  if (*config_.scene_owner == UITestManager::SceneOwnerType::ROOT_PRESENTER) {
    url = config_.use_input ? kRootPresenterSceneWithInputUrl : kRootPresenterSceneUrl;
  } else if (config_.scene_owner == UITestManager::SceneOwnerType::SCENE_MANAGER) {
    url = kSceneManagerSceneUrl;
  }

  realm_builder_.AddChild(kTestRealmName, url);
}

void UITestManager::ConfigureDefaultSystemServices() {
  std::vector<Ref> targets = {ChildRef{kTestRealmName}, ChildRef{kClientSubrealmName}};

  // Route system services from parent to both test realm and client subrealm.
  realm_builder_.AddRoute(Route{.capabilities =
                                    {
                                        // Redirect logging output for the test realm to
                                        // the host console output.
                                        Protocol{fuchsia::logger::LogSink::Name_},
                                        Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                                        Protocol{fuchsia::sysmem::Allocator::Name_},
                                        Protocol{fuchsia::tracing::provider::Registry::Name_},
                                        Protocol{fuchsia::vulkan::loader::Loader::Name_},
                                    },
                                .source = ParentRef(),
                                .targets = std::move(targets)});
}

void UITestManager::ConfigureSceneOwner() {
  FX_CHECK(config_.scene_owner.has_value());

  if (*config_.scene_owner == UITestManager::SceneOwnerType::ROOT_PRESENTER) {
    realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::policy::Presenter::Name_}},
                                  .source = ChildRef{kTestRealmName},
                                  .targets = {ParentRef()}});
  } else if (*config_.scene_owner == UITestManager::SceneOwnerType::SCENE_MANAGER) {
    realm_builder_.AddRoute(
        Route{.capabilities = {Protocol{fuchsia::session::scene::Manager::Name_}},
              .source = ChildRef{kTestRealmName},
              .targets = {ParentRef()}});
  }

  // If the user specifies a view provider, they must also supply a view
  // provider in their subrealm.
  realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                                .source = ChildRef{kClientSubrealmName},
                                .targets = {ParentRef()}});
}

void UITestManager::ConfigureAccessibility() {
  if (!config_.accessibility_owner) {
    return;
  }

  FX_CHECK(config_.accessibility_owner == UITestManager::AccessibilityOwnerType::FAKE)
      << "Real a11y manager not currently supported";

  realm_builder_.AddChild(kA11yManagerName, kFakeA11yManagerUrl);

  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_}},
            .source = ChildRef{kA11yManagerName},
            .targets = {ChildRef{kClientSubrealmName}}});
}

void UITestManager::ConfigureInput() {
  if (!config_.use_input) {
    return;
  }

  // Infer that input pipeline owns input if root presenter or scene mnaager
  // owns the scene.
  if (config_.scene_owner) {
    realm_builder_.AddRoute(
        Route{.capabilities = {Protocol{fuchsia::input::injection::InputDeviceRegistry::Name_},
                               Protocol{fuchsia::ui::policy::DeviceListenerRegistry::Name_}},
              .source = ChildRef{kTestRealmName},
              .targets = {ParentRef()}});
  } else {
    realm_builder_.AddRoute(
        Route{.capabilities = {Protocol{fuchsia::ui::pointerinjector::Registry::Name_}},
              .source = ChildRef{kTestRealmName},
              .targets = {ParentRef()}});
  }

  // Specify display rotation.
  realm_builder_.RouteReadOnlyDirectory(
      "config", {ChildRef{kTestRealmName}},
      std::move(component_testing::DirectoryContents().AddFile("data/display_rotation", "90")));
}

void UITestManager::ConfigureScenic() {
  realm_builder_.AddRoute(
      Route{.capabilities =
                {
                    Protocol{fuchsia::ui::composition::Allocator::Name_},
                    Protocol{fuchsia::ui::composition::Flatland::Name_},
                    Protocol{fuchsia::ui::composition::FlatlandDisplay::Name_},
                    Protocol{fuchsia::ui::focus::FocusChainListenerRegistry::Name_},
                    Protocol{fuchsia::ui::scenic::Scenic::Name_},
                    Protocol{fuchsia::ui::views::ViewRefInstalled::Name_},
                },
            .source = ChildRef{kTestRealmName},
            .targets = {ParentRef()}});
}

void UITestManager::BuildRealm() {
  AddBaseRealmComponent();

  ConfigureDefaultSystemServices();

  // Route API to present scene root to ui test manager.
  // Note that ui test manger mediates scene setup, so clients do not use these
  // APIs directly.
  ConfigureSceneOwner();

  // Expose input APIs out of the realm.
  ConfigureInput();

  // Set up a11y manager, if requested, and route semantics manager service to
  // client subrealm.
  ConfigureAccessibility();

  // Route base scenic services to client subrealm.
  // We also expose these services to parent, so that the ui test manager can
  // use them for scene setup and monitoring.
  ConfigureScenic();

  // Route services to parent that client requested to expose.
  for (const auto& service : config_.exposed_client_services) {
    realm_builder_.AddRoute(Route{.capabilities = {Protocol{service}},
                                  .source = ChildRef{kClientSubrealmName},
                                  .targets = {ParentRef()}});
  }

  // Route requested services from ui realm to client subrealm.
  for (const auto& service : config_.ui_to_client_services) {
    realm_builder_.AddRoute(Route{.capabilities = {Protocol{service}},
                                  .source = ChildRef{kTestRealmName},
                                  .targets = {ChildRef{kClientSubrealmName}}});
  }

  realm_root_ = std::make_unique<component_testing::RealmRoot>(realm_builder_.Build());
}

std::unique_ptr<sys::ServiceDirectory> UITestManager::TakeExposedServicesDirectory() {
  return std::make_unique<sys::ServiceDirectory>(realm_root_->CloneRoot());
}

void UITestManager::InitializeScene() {
  if (*config_.scene_owner == UITestManager::SceneOwnerType::ROOT_PRESENTER) {
    scene_ = std::make_unique<ui_testing::GfxRootPresenterScene>(realm_root_);
  } else if (config_.scene_owner == UITestManager::SceneOwnerType::SCENE_MANAGER) {
    scene_ = std::make_unique<ui_testing::GfxSceneManagerScene>(realm_root_);
  } else {
    FX_LOGS(FATAL) << "Unsupported scene owner";
  }

  scene_->Initialize();
}

bool UITestManager::ClientViewIsAttached() { return scene_->ClientViewIsAttached(); }

bool UITestManager::ClientViewIsRendering() { return scene_->ClientViewIsRendering(); }

std::optional<zx_koid_t> UITestManager::ClientViewRefKoid() { return scene_->ClientViewRefKoid(); }

float UITestManager::ClientViewScaleFactor() { return scene_->ClientViewScaleFactor(); }

}  // namespace ui_testing
