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
#include <fuchsia/ui/accessibility/view/cpp/fidl.h>
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

using component_testing::Capability;
using component_testing::ChildRef;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Ref;
using component_testing::Route;

// Base realm urls.
constexpr auto kScenicOnlyUrl = "#meta/scenic_only.cm";
constexpr auto kRootPresenterSceneUrl = "#meta/root_presenter_scene.cm";
constexpr auto kRootPresenterSceneWithInputUrl = "#meta/root_presenter_scene_with_input.cm";
constexpr auto kSceneManagerSceneUrl = "#meta/scene_manager_scene.cm";

// System component urls.
constexpr auto kFakeA11yManagerUrl = "#meta/fake-a11y-manager.cm";

constexpr auto kTestRealmName = "test-realm";
constexpr auto kClientSubrealmName = "client-subrealm";

constexpr auto kA11yManagerName = "a11y-manager";

// Set of low-level system services that components in the realm can consume
// from parent (test_manager).
std::vector<std::string> DefaultSystemServices() {
  return {fuchsia::logger::LogSink::Name_, fuchsia::scheduler::ProfileProvider::Name_,
          fuchsia::sysmem::Allocator::Name_, fuchsia::tracing::provider::Registry::Name_,
          fuchsia::vulkan::loader::Loader::Name_};
}

}  // namespace

UITestManager::UITestManager(UITestManager::Config config) : config_(config) {
  FX_CHECK(!config_.use_flatland) << "Flatland not currently supported";
}

void UITestManager::SetUseFlatlandConfig(bool use_flatland) {
  // Not yet implemented.
}

void UITestManager::RouteServices(std::vector<std::string> services, Ref source,
                                  std::vector<Ref> targets) {
  if (services.empty()) {
    return;
  }

  std::vector<Capability> protocols;
  for (const auto& service : services) {
    protocols.emplace_back(Protocol{service});
  }

  realm_builder_.AddRoute(
      Route{.capabilities = protocols, .source = std::move(source), .targets = std::move(targets)});
}

component_testing::Realm UITestManager::AddSubrealm() {
  has_client_subrealm_ = true;
  return realm_builder_.AddChildRealm(kClientSubrealmName);
}

void UITestManager::AddBaseRealmComponent() {
  std::string url = "";

  // If no scene owner is specified, then use the base scenic realm.
  if (!config_.scene_owner) {
    url = kScenicOnlyUrl;
  } else if (config_.scene_owner == UITestManager::SceneOwnerType::ROOT_PRESENTER) {
    url = config_.use_input ? kRootPresenterSceneWithInputUrl : kRootPresenterSceneUrl;
  } else if (config_.scene_owner == UITestManager::SceneOwnerType::SCENE_MANAGER) {
    url = kSceneManagerSceneUrl;
  }

  realm_builder_.AddChild(kTestRealmName, url);
}

void UITestManager::ConfigureTestSubrealm() {
  // Route default system services to test subrealm.
  RouteServices(DefaultSystemServices(), /* source = */ ParentRef(),
                /* targets = */ {ChildRef{kTestRealmName}});
}

void UITestManager::ConfigureClientSubrealm() {
  if (!has_client_subrealm_) {
    return;
  }

  // Route default system services to test subrealm.
  RouteServices(DefaultSystemServices(), /* source = */ ParentRef(),
                /* targets = */ {ChildRef{kClientSubrealmName}});

  // Route services to parent that client requested to expose.
  RouteServices(config_.exposed_client_services, /* source = */ ChildRef{kClientSubrealmName},
                /* targets = */ {ParentRef()});

  // Route requested services from ui realm to client subrealm.
  RouteServices(config_.ui_to_client_services, /* source = */ ChildRef{kTestRealmName},
                /* targets = */ {ChildRef{kClientSubrealmName}});

  // Route requested services from client subrealm to ui realm.
  RouteServices(config_.client_to_ui_services, /* source = */ ChildRef{kClientSubrealmName},
                /* targets = */ {ChildRef{kTestRealmName}});

  if (config_.accessibility_owner) {
    RouteServices({fuchsia::accessibility::semantics::SemanticsManager::Name_},
                  /* source = */ ChildRef{kA11yManagerName},
                  /* target = */ {ChildRef{kClientSubrealmName}});
  }

  // If the user specifies a view provider, they must also supply a view
  // provider in their subrealm.
  RouteServices({fuchsia::ui::app::ViewProvider::Name_},
                /* source = */ ChildRef{kClientSubrealmName}, /* targets = */ {ParentRef()});
}

void UITestManager::ConfigureSceneOwner() {
  if (!config_.scene_owner) {
    return;
  }

  if (config_.scene_owner == UITestManager::SceneOwnerType::ROOT_PRESENTER) {
    RouteServices(
        {fuchsia::ui::policy::Presenter::Name_, fuchsia::ui::accessibility::view::Registry::Name_},
        /* source = */ ChildRef{kTestRealmName}, /* targets = */ {ParentRef()});
  } else if (config_.scene_owner == UITestManager::SceneOwnerType::SCENE_MANAGER) {
    RouteServices({fuchsia::session::scene::Manager::Name_,
                   fuchsia::ui::accessibility::view::Registry::Name_},
                  /* source = */ ChildRef{kTestRealmName}, /* targets = */ {ParentRef()});
  }
}

void UITestManager::ConfigureAccessibility() {
  if (!config_.accessibility_owner) {
    return;
  }

  FX_CHECK(config_.accessibility_owner == UITestManager::AccessibilityOwnerType::FAKE)
      << "Real a11y manager not currently supported";

  realm_builder_.AddChild(kA11yManagerName, kFakeA11yManagerUrl);
  RouteServices({fuchsia::accessibility::semantics::SemanticsManager::Name_},
                /* source = */ ChildRef{kA11yManagerName},
                /* target = */ {ParentRef()});
}

void UITestManager::ConfigureInput() {
  if (!config_.use_input) {
    return;
  }

  // Infer that input pipeline owns input if root presenter or scene mnaager
  // owns the scene.
  if (config_.scene_owner) {
    RouteServices({fuchsia::ui::pointerinjector::configuration::Setup::Name_,
                   fuchsia::input::injection::InputDeviceRegistry::Name_,
                   fuchsia::ui::policy::DeviceListenerRegistry::Name_},
                  /* source = */ ChildRef{kTestRealmName},
                  /* targets = */ {ParentRef()});
  } else {
    RouteServices({fuchsia::ui::pointerinjector::Registry::Name_},
                  /* source = */ ChildRef{kTestRealmName},
                  /* targets = */ {ParentRef()});
  }

  // Specify display rotation.
  realm_builder_.RouteReadOnlyDirectory(
      "config", {ChildRef{kTestRealmName}},
      std::move(component_testing::DirectoryContents().AddFile("data/display_rotation", "90")));
}

void UITestManager::ConfigureScenic() {
  RouteServices(
      {fuchsia::ui::composition::Allocator::Name_, fuchsia::ui::composition::Flatland::Name_,
       fuchsia::ui::composition::FlatlandDisplay::Name_,
       fuchsia::ui::focus::FocusChainListenerRegistry::Name_, fuchsia::ui::scenic::Scenic::Name_,
       fuchsia::ui::views::ViewRefInstalled::Name_},
      /* source = */ ChildRef{kTestRealmName}, /* targets = */ {ParentRef()});
}

void UITestManager::BuildRealm() {
  AddBaseRealmComponent();

  // Add routes to/from the test realm and client subrealm (if applicable).
  ConfigureTestSubrealm();

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

  // This step needs to come after ConfigureAccessibility(), because the a11y
  // manager component needs to be added to the realm first.
  ConfigureClientSubrealm();

  realm_root_ = std::make_unique<component_testing::RealmRoot>(realm_builder_.Build());
}

std::unique_ptr<sys::ServiceDirectory> UITestManager::TakeExposedServicesDirectory() {
  return std::make_unique<sys::ServiceDirectory>(realm_root_->CloneRoot());
}

void UITestManager::InitializeScene() {
  FX_CHECK(config_.scene_owner.has_value()) << "Scene owner must be specified";

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
