// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/ui_test_realm/ui_test_realm.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/accessibility/scene/cpp/fidl.h>
#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/input/injection/cpp/fidl.h>
#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/session/scene/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/accessibility/view/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/observation/test/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/configuration/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include <test/accessibility/cpp/fidl.h>
#include <test/inputsynthesis/cpp/fidl.h>

#include "sdk/lib/syslog/cpp/macros.h"

namespace ui_testing {

namespace {

using component_testing::Capability;
using component_testing::ChildRef;
using component_testing::ConfigValue;
using component_testing::Directory;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::RealmBuilder;
using component_testing::Ref;
using component_testing::Route;

// Base realm urls.
constexpr auto kScenicOnlyUrl = "#meta/scenic_only.cm";
constexpr auto kRootPresenterSceneUrl = "#meta/root_presenter_scene.cm";
constexpr auto kRootPresenterSceneWithInputUrl = "#meta/root_presenter_scene_with_input.cm";
constexpr auto kSceneManagerSceneUrl = "#meta/scene_manager_scene.cm";
constexpr auto kSceneManagerSceneWithInputUrl = "#meta/scene_manager_scene_with_input.cm";

// System component urls.
constexpr auto kRealA11yManagerUrl = "#meta/a11y-manager.cm";
constexpr auto kFakeA11yManagerUrl = "#meta/fake-a11y-manager.cm";

constexpr auto kClientSubrealmName = "client-subrealm";

// Component names.
// NOTE: These names must match the names in meta/*.cml.
constexpr auto kA11yManagerName = "a11y-manager";
constexpr auto kScenicName = "scenic";
constexpr auto kRootPresenterName = "root-presenter";
constexpr auto kSceneManagerName = "scene-manager";
constexpr auto kInputPipelineName = "input-pipeline";
constexpr auto kTextManagerName = "text-manager";
constexpr auto kVirtualKeyboardManagerName = "virtual-keyboard-manager";
constexpr auto kSceneProviderName = "scene-provider";

// Contents of config file used to allow scenic to use gfx.
constexpr auto kUseGfxScenicConfig = R"(
{
  "flatland_buffer_collection_import_mode": "renderer_only",
  "i_can_haz_flatland": false
}
)";

// Contents of config file used to force scenic to use flatland.
constexpr auto kUseFlatlandScenicConfig = R"(
{
  "flatland_buffer_collection_import_mode": "renderer_only",
  "i_can_haz_flatland": true
}
)";

// Set of low-level system services that components in the realm can consume
// from parent (test_manager).
std::vector<std::string> DefaultSystemServices() {
  return {fuchsia::logger::LogSink::Name_, fuchsia::scheduler::ProfileProvider::Name_,
          fuchsia::sysmem::Allocator::Name_, fuchsia::tracing::provider::Registry::Name_,
          fuchsia::vulkan::loader::Loader::Name_};
}

// Returns the name of the scene owner component (if any).
std::string SceneOwnerName(const UITestRealm::Config& config) {
  if (!config.scene_owner) {
    return "";
  }

  switch (*config.scene_owner) {
    case UITestRealm::SceneOwnerType::ROOT_PRESENTER:
      return kRootPresenterName;
    case UITestRealm::SceneOwnerType::SCENE_MANAGER:
      return kSceneManagerName;
    default:
      return "";
  }
}

// Returns the name of the input owner component (if any).
std::string InputOwnerName(const UITestRealm::Config& config) {
  if (!config.use_input) {
    return "";
  }

  switch (*config.scene_owner) {
    case UITestRealm::SceneOwnerType::ROOT_PRESENTER:
      return kInputPipelineName;
    case UITestRealm::SceneOwnerType::SCENE_MANAGER:
      return kSceneManagerName;
    default:
      return "";
  }
}

// Returns the name of the virtual keyboard component (if any).
std::string VirtualKeyboardOwnerName(const UITestRealm::Config& config) {
  if (!config.scene_owner) {
    return "";
  }

  switch (*config.scene_owner) {
    case UITestRealm::SceneOwnerType::ROOT_PRESENTER:
      return kRootPresenterName;
    case UITestRealm::SceneOwnerType::SCENE_MANAGER:
      return kVirtualKeyboardManagerName;
    default:
      return "";
  }
}

// List of scenic services available in the test realm.
std::vector<std::string> ScenicServices(const UITestRealm::Config& config) {
  if (config.use_flatland) {
    // Note that we expose FlatlandDisplay to the client subrealm for now, since
    // we only have in-tree test clients at the moment. Once UITestManager is
    // used for out-of-tree tests, we'll want to add a flag to
    // UITestRealm::Config to control whether we expose internal-only APIs to
    // the client subrealm.
    return {fuchsia::ui::observation::test::Registry::Name_,
            fuchsia::ui::composition::Allocator::Name_, fuchsia::ui::composition::Flatland::Name_,
            fuchsia::ui::composition::FlatlandDisplay::Name_, fuchsia::ui::scenic::Scenic::Name_};
  } else {
    return {fuchsia::ui::observation::test::Registry::Name_,
            fuchsia::ui::focus::FocusChainListenerRegistry::Name_,
            fuchsia::ui::scenic::Scenic::Name_, fuchsia::ui::views::ViewRefInstalled::Name_};
  }
}

// List of a11y services available in the test realm.
std::vector<std::string> AccessibilityServices(const UITestRealm::Config& config) {
  if (!config.accessibility_owner) {
    return {};
  }

  return {fuchsia::accessibility::semantics::SemanticsManager::Name_,
          fuchsia::accessibility::Magnifier::Name_};
}

// List of scene owner services available in the test realm.
std::vector<std::string> SceneOwnerServices(const UITestRealm::Config& config) {
  if (!config.scene_owner)
    return {};

  if (config.scene_owner == UITestRealm::SceneOwnerType::ROOT_PRESENTER) {
    return {fuchsia::ui::accessibility::view::Registry::Name_,
            fuchsia::ui::pointerinjector::configuration::Setup::Name_,
            fuchsia::ui::policy::Presenter::Name_};
  } else if (config.scene_owner == UITestRealm::SceneOwnerType::SCENE_MANAGER) {
    return {fuchsia::session::scene::Manager::Name_,
            fuchsia::ui::accessibility::view::Registry::Name_};
  }

  return {};
}

// List of input services available in the test realm.
std::vector<std::string> InputServices(const UITestRealm::Config& config) {
  if (!config.use_input) {
    return {};
  }

  if (config.scene_owner) {
    return {fuchsia::input::injection::InputDeviceRegistry::Name_,
            fuchsia::ui::policy::DeviceListenerRegistry::Name_};
  } else {
    return {fuchsia::ui::pointerinjector::Registry::Name_};
  }
}

// Returns a mapping from ui service name to the component that vends the
// service.
std::map<std::string, std::string> GetServiceToComponentMap(UITestRealm::Config config) {
  std::map<std::string, std::string> service_to_component;

  for (const auto& service : ScenicServices(config)) {
    service_to_component[service] = kScenicName;
  }

  for (const auto& service : AccessibilityServices(config)) {
    service_to_component[service] = kA11yManagerName;
  }

  for (const auto& service : SceneOwnerServices(config)) {
    service_to_component[service] = SceneOwnerName(config);
  }

  for (const auto& service : InputServices(config)) {
    service_to_component[service] = InputOwnerName(config);
  }

  // Additional input services.
  if (config.use_input) {
    service_to_component[fuchsia::ui::input::ImeService::Name_] = kTextManagerName;
    service_to_component[fuchsia::ui::input3::Keyboard::Name_] = kTextManagerName;

    service_to_component[fuchsia::input::virtualkeyboard::ControllerCreator::Name_] =
        VirtualKeyboardOwnerName(config);
    service_to_component[fuchsia::input::virtualkeyboard::Manager::Name_] =
        VirtualKeyboardOwnerName(config);
  }

  return service_to_component;
}

}  // namespace

UITestRealm::UITestRealm(UITestRealm::Config config) : config_(config) {}

std::string UITestRealm::CalculateBaseRealmUrl() {
  if (config_.scene_owner == UITestRealm::SceneOwnerType::ROOT_PRESENTER) {
    return config_.use_input ? kRootPresenterSceneWithInputUrl : kRootPresenterSceneUrl;
  } else if (config_.scene_owner == UITestRealm::SceneOwnerType::SCENE_MANAGER) {
    return config_.use_input ? kSceneManagerSceneWithInputUrl : kSceneManagerSceneUrl;
  } else {
    // If we have exhausted all potential scene owner options, then scene_owner
    // should be std::nullopt.
    FX_CHECK(!config_.scene_owner) << "Unrecognized scene owner";

    // If no scene owner is specified, use the scenic-only realm.
    return kScenicOnlyUrl;
  }
}

void UITestRealm::RouteServices(std::vector<std::string> services, Ref source,
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

component_testing::Realm UITestRealm::AddSubrealm() {
  has_client_subrealm_ = true;
  return realm_builder_.AddChildRealm(kClientSubrealmName);
}

void UITestRealm::ConfigureClientSubrealm() {
  if (!has_client_subrealm_) {
    return;
  }

  // Route default system services to test subrealm.
  RouteServices(DefaultSystemServices(), /* source = */ ParentRef(),
                /* targets = */ {ChildRef{kClientSubrealmName}});

  // Route any passthrough capabilities to the client subrealm.
  if (!config_.passthrough_capabilities.empty()) {
    realm_builder_.AddRoute(Route{.capabilities = config_.passthrough_capabilities,
                                  .source = ParentRef(),
                                  .targets = {ChildRef{kClientSubrealmName}}});
  }

  // Route services to parent that client requested to expose.
  RouteServices(config_.exposed_client_services, /* source = */ ChildRef{kClientSubrealmName},
                /* targets = */ {ParentRef()});

  // Route services client requested from ui subrealm.
  auto service_to_component = GetServiceToComponentMap(config_);
  for (const auto& service : config_.ui_to_client_services) {
    auto it = service_to_component.find(service);
    FX_CHECK(it != service_to_component.end())
        << "Service is not available for the specified realm configuration: " << service;

    RouteServices({service}, /* source = */ ChildRef{it->second},
                  /* targets = */ {ChildRef{kClientSubrealmName}});
  }

  // Route ViewProvider to parent if the client specifies a scene owner.
  if (config_.scene_owner) {
    RouteServices({fuchsia::ui::app::ViewProvider::Name_},
                  /* source = */ ChildRef{kClientSubrealmName},
                  /* targets = */ {ParentRef()});
  }

  // TODO(fxbug.dev/98545): Remove this escape hatch, or generalize to any
  // capability.
  //
  // Allow child realm components to access to config-data directory by default.
  //
  // NOTE: The client must offer the "config-data" capability to #realm_builder in
  // its test .cml file.
  realm_builder_.AddRoute(
      {.capabilities = {Directory{
           .name = "config-data", .rights = fuchsia::io::R_STAR_DIR, .path = "/config/data"}},
       .source = ParentRef(),
       .targets = {ChildRef{kClientSubrealmName}}});
}

void UITestRealm::ConfigureAccessibility() {
  std::string a11y_manager_url;
  // Add real a11y manager to the test realm, if requested.
  // Otherwise, add fake a11y manager if it's requested, OR if the test uses
  // `FlatlandSceneManager` (which will only render a client view if the a11y
  // view is present).
  if (config_.accessibility_owner == UITestRealm::AccessibilityOwnerType::REAL) {
    a11y_manager_url = kRealA11yManagerUrl;
  } else if (config_.accessibility_owner == UITestRealm::AccessibilityOwnerType::FAKE ||
             (config_.scene_owner == UITestRealm::SceneOwnerType::SCENE_MANAGER &&
              config_.use_flatland)) {
    a11y_manager_url = kFakeA11yManagerUrl;
  } else {
    return;
  }

  realm_builder_.AddChild(kA11yManagerName, a11y_manager_url);
  RouteServices({fuchsia::logger::LogSink::Name_},
                /* source = */ ParentRef(),
                /* targets = */ {ChildRef{kA11yManagerName}});
  RouteServices({fuchsia::ui::composition::Flatland::Name_, fuchsia::ui::scenic::Scenic::Name_},
                /* source = */ ChildRef{kScenicName},
                /* targets = */ {ChildRef{kA11yManagerName}});
  RouteServices({fuchsia::accessibility::semantics::SemanticsManager::Name_,
                 test::accessibility::Magnifier::Name_},
                /* source = */ ChildRef{kA11yManagerName},
                /* targets = */ {ParentRef()});

  if (config_.scene_owner) {
    if (config_.use_flatland) {
      RouteServices({fuchsia::accessibility::scene::Provider::Name_},
                    /* source = */ ChildRef{kA11yManagerName},
                    /* targets = */ {ChildRef{SceneOwnerName(config_)}});
    } else {
      RouteServices({fuchsia::accessibility::Magnifier::Name_},
                    /* source = */ ChildRef{kA11yManagerName},
                    /* targets = */ {ChildRef{SceneOwnerName(config_)}});
    }
  }
}

void UITestRealm::RouteConfigData() {
  auto config_directory_contents = component_testing::DirectoryContents();
  std::vector<Ref> targets;

  // Override scenic's "i_can_haz_flatland" flag.
  if (config_.use_flatland) {
    config_directory_contents.AddFile("scenic_config", kUseFlatlandScenicConfig);
    targets.push_back(ChildRef{kScenicName});
  } else {
    config_directory_contents.AddFile("scenic_config", kUseGfxScenicConfig);
    targets.push_back(ChildRef{kScenicName});
  }

  std::string scene_owner_name = SceneOwnerName(config_);
  if (config_.scene_owner) {
    // Supply a default display rotation.
    config_directory_contents.AddFile("display_rotation", std::to_string(config_.display_rotation));

    if (config_.display_pixel_density > 0) {
      config_directory_contents.AddFile("display_pixel_density",
                                        std::to_string(config_.display_pixel_density));
    }

    if (!config_.display_usage.empty()) {
      config_directory_contents.AddFile("display_usage", config_.display_usage);
    }

    targets.push_back(ChildRef{scene_owner_name});
  }

  if (!targets.empty()) {
    realm_builder_.RouteReadOnlyDirectory("config-data", std::move(targets),
                                          std::move(config_directory_contents));
  }
}

void UITestRealm::ConfigureSceneProvider() {
  // The scene provider component will only be present in the test realm if the
  // client specifies a scene owner.
  if (!config_.scene_owner) {
    return;
  }

  // scene-provider has more config fields than we set here, load the defaults.
  realm_builder_.InitMutableConfigFromPackage(kSceneProviderName);

  bool use_scene_manager = config_.scene_owner == SceneOwnerType::SCENE_MANAGER;
  realm_builder_.SetConfigValue(kSceneProviderName, "use_scene_manager",
                                ConfigValue::Bool(use_scene_manager));
}

void UITestRealm::ConfigureActivityService() {
  if (!config_.use_input) {
    return;
  }

  realm_builder_.InitMutableConfigFromPackage(InputOwnerName(config_));
  realm_builder_.SetConfigValue(InputOwnerName(config_), "idle_threshold_minutes",
                                ConfigValue::Uint64(config_.idle_threshold_minutes));
}

void UITestRealm::Build() {
  // Set up a11y manager, if requested, and route semantics manager service to
  // client subrealm.
  //
  // NOTE: We opt to configure accessibility dynamically, rather then in the
  // .cml for the base realms, because there are three different a11y
  // configurations (fake, real, none), which can each apply to scenes
  // with/without input. The a11y service routing is also different for gfx and
  // flatland, so it would be unwieldy to create a separate static declaration
  // for every a11y configuration tested.
  ConfigureAccessibility();

  // Route config data directories to appropriate recipients (currently, scenic,
  // scene manager, and root presenter are the only use cases for config files.
  RouteConfigData();

  // This step needs to come after ConfigureAccessibility(), because the a11y
  // manager component needs to be added to the realm first.
  ConfigureClientSubrealm();

  // Override component config for scene provider to specify which API to use to
  // attach the client view to the scene.
  ConfigureSceneProvider();

  // Override component config for input owner to specify how long the idle
  // threshold timeout should be.
  ConfigureActivityService();

  realm_root_ = std::make_unique<component_testing::RealmRoot>(realm_builder_.Build());
}

std::unique_ptr<sys::ServiceDirectory> UITestRealm::CloneExposedServicesDirectory() {
  FX_CHECK(realm_root_)
      << "Client must call Build() before attempting to take exposed services directory";

  return std::make_unique<sys::ServiceDirectory>(realm_root_->CloneRoot());
}

}  // namespace ui_testing
