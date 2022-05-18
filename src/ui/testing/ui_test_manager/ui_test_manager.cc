// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/ui_test_manager/ui_test_manager.h"

#include <fuchsia/accessibility/cpp/fidl.h>
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
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/configuration/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <test/accessibility/cpp/fidl.h>
#include <test/inputsynthesis/cpp/fidl.h>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/lib/fsl/handles/object_info.h"

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
constexpr auto kSceneManagerSceneWithInputUrl = "#meta/scene_manager_scene_with_input.cm";

// System component urls.
constexpr auto kFakeA11yManagerUrl = "#meta/fake-a11y-manager.cm";

constexpr auto kTestRealmName = "test-realm";
constexpr auto kClientSubrealmName = "client-subrealm";

constexpr auto kA11yManagerName = "a11y-manager";

// Contents of config file used to force scenic to use flatland.
constexpr auto kUseFlatlandScenicConfig = R"(
{
  "flatland_buffer_collection_import_mode": "renderer_only",
  "i_can_haz_flatland": true
}
)";

constexpr auto kDefaultScale = 0.f;

// Set of low-level system services that components in the realm can consume
// from parent (test_manager).
std::vector<std::string> DefaultSystemServices() {
  return {fuchsia::logger::LogSink::Name_, fuchsia::scheduler::ProfileProvider::Name_,
          fuchsia::sysmem::Allocator::Name_, fuchsia::tracing::provider::Registry::Name_,
          fuchsia::vulkan::loader::Loader::Name_};
}

std::optional<fuchsia::ui::observation::geometry::ViewDescriptor> ViewDescriptorFromSnapshot(
    const fuchsia::ui::observation::geometry::ViewTreeSnapshot& snapshot, zx_koid_t view_ref_koid) {
  if (!snapshot.has_views()) {
    return std::nullopt;
  }

  auto it = std::find_if(snapshot.views().begin(), snapshot.views().end(),
                         [view_ref_koid](const auto& view) {
                           return view.has_view_ref_koid() && view.view_ref_koid() == view_ref_koid;
                         });
  if (it == snapshot.views().end()) {
    return std::nullopt;
  }

  return fidl::Clone(*it);
}

}  // namespace

UITestManager::UITestManager(UITestManager::Config config)
    : config_(config), focus_chain_listener_binding_(this) {}

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
    url = config_.use_input ? kSceneManagerSceneWithInputUrl : kSceneManagerSceneUrl;
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
  RouteServices({fuchsia::logger::LogSink::Name_},
                /* source = */ ParentRef(),
                /* targets = */ {ChildRef{kA11yManagerName}});
  RouteServices({fuchsia::accessibility::semantics::SemanticsManager::Name_,
                 test::accessibility::Magnifier::Name_},
                /* source = */ ChildRef{kA11yManagerName},
                /* target = */ {ParentRef()});

  if (config_.scene_owner && !config_.use_flatland) {
    RouteServices({fuchsia::accessibility::Magnifier::Name_},
                  /* source = */ ChildRef{kA11yManagerName},
                  /* target = */ {ChildRef{kTestRealmName}});
  }
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
    if (config_.scene_owner == UITestManager::SceneOwnerType::SCENE_MANAGER) {
      RouteServices({test::inputsynthesis::Mouse::Name_, test::inputsynthesis::Text::Name_,
                     fuchsia::ui::input::ImeService::Name_, fuchsia::ui::input3::Keyboard::Name_},
                    /* source = */ ChildRef{kTestRealmName},
                    /* targets = */ {ParentRef()});
    }
  } else {
    RouteServices({fuchsia::ui::pointerinjector::Registry::Name_},
                  /* source = */ ChildRef{kTestRealmName},
                  /* targets = */ {ParentRef()});
  }
}

void UITestManager::ConfigureScenic() {
  RouteServices(
      {fuchsia::ui::composition::Allocator::Name_, fuchsia::ui::composition::Flatland::Name_,
       fuchsia::ui::composition::FlatlandDisplay::Name_,
       fuchsia::ui::focus::FocusChainListenerRegistry::Name_, fuchsia::ui::scenic::Scenic::Name_,
       fuchsia::ui::observation::test::Registry::Name_,
       fuchsia::ui::views::ViewRefInstalled::Name_},
      /* source = */ ChildRef{kTestRealmName}, /* targets = */ {ParentRef()});
}

void UITestManager::RouteConfigData() {
  auto config_directory_contents = component_testing::DirectoryContents();
  bool directory_has_contents = false;

  // Override scenic's Override "i_can_haz_flatland" if necessary.
  if (config_.use_flatland) {
    directory_has_contents = true;
    config_directory_contents.AddFile("data/scenic_config", kUseFlatlandScenicConfig);
  }

  // Supply a default display rotation.
  if (config_.scene_owner) {
    directory_has_contents = true;
    config_directory_contents.AddFile("data/display_rotation",
                                      std::to_string(config_.display_rotation));
  }

  if (directory_has_contents) {
    realm_builder_.RouteReadOnlyDirectory("config", {ChildRef{kTestRealmName}},
                                          std::move(config_directory_contents));
  }
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

  // Route config data directories to appropriate recipients (currently, scenic,
  // scene manager, and root presenter are the only use cases for config files.
  RouteConfigData();

  // This step needs to come after ConfigureAccessibility(), because the a11y
  // manager component needs to be added to the realm first.
  ConfigureClientSubrealm();

  realm_root_ = std::make_unique<component_testing::RealmRoot>(realm_builder_.Build());
}

std::unique_ptr<sys::ServiceDirectory> UITestManager::TakeExposedServicesDirectory() {
  return std::make_unique<sys::ServiceDirectory>(realm_root_->CloneRoot());
}

void UITestManager::InitializeScene() {
  FX_CHECK(realm_root_) << "BuildRealm() must be called before InitializeScene()";
  FX_CHECK(config_.scene_owner.has_value()) << "Scene owner must be specified";
  FX_CHECK(!observer_registry_ && !geometry_provider_) << "InitializeScene() called twice";

  // Register geometry observer. We should do this before attaching the client
  // view, so that we see all the view tree snapshots.
  realm_root_->Connect<fuchsia::ui::observation::test::Registry>(observer_registry_.NewRequest());
  observer_registry_->RegisterGlobalGeometryProvider(geometry_provider_.NewRequest());

  // Register focus chain listener.
  auto focus_chain_listener_registry =
      realm_root_->Connect<fuchsia::ui::focus::FocusChainListenerRegistry>();
  focus_chain_listener_registry->Register(focus_chain_listener_binding_.NewBinding());

  if (*config_.scene_owner == UITestManager::SceneOwnerType::ROOT_PRESENTER) {
    root_presenter_ = realm_root_->Connect<fuchsia::ui::policy::Presenter>();

    auto client_view_tokens = scenic::ViewTokenPair::New();
    auto [client_control_ref, client_view_ref] = scenic::ViewRefPair::New();
    client_view_ref_ = fidl::Clone(client_view_ref);

    root_presenter_->PresentOrReplaceView2(std::move(client_view_tokens.view_holder_token),
                                           fidl::Clone(client_view_ref),
                                           /* presentation */ nullptr);

    auto client_view_provider = realm_root_->Connect<fuchsia::ui::app::ViewProvider>();
    client_view_provider->CreateViewWithViewRef(std::move(client_view_tokens.view_token.value),
                                                std::move(client_control_ref),
                                                std::move(client_view_ref));
  } else if (config_.scene_owner == UITestManager::SceneOwnerType::SCENE_MANAGER) {
    scene_manager_ = realm_root_->Connect<fuchsia::session::scene::Manager>();
    auto view_provider = realm_root_->Connect<fuchsia::ui::app::ViewProvider>();
    scene_manager_->SetRootView(
        std::move(view_provider),
        [this](fuchsia::ui::views::ViewRef view_ref) { client_view_ref_ = std::move(view_ref); });
  } else {
    FX_LOGS(FATAL) << "Unsupported scene owner";
  }

  WatchViewTree();
}

void UITestManager::WatchViewTree() {
  FX_CHECK(geometry_provider_)
      << "Geometry observer must be registered before calling WatchViewTree()";

  geometry_provider_->Watch([this](auto response) {
    if (!response.has_error()) {
      std::vector<fuchsia::ui::observation::geometry::ViewTreeSnapshot>* updates =
          response.mutable_updates();
      if (updates && !updates->empty()) {
        last_view_tree_snapshot_ = std::move(updates->back());
      }

      WatchViewTree();
      return;
    }

    const auto& error = response.error();

    if (error.has_channel_overflow() && error.channel_overflow()) {
      FX_LOGS(FATAL) << "Geometry provider channel overflowed";
    } else if (error.has_buffer_overflow() && error.buffer_overflow()) {
      FX_LOGS(FATAL) << "Geometry provider buffer overflowed";
    } else if (error.has_views_overflow() && error.views_overflow()) {
      FX_LOGS(FATAL) << "Geometry provider attempted to report too many views";
    }
  });
}

void UITestManager::OnFocusChange(fuchsia::ui::focus::FocusChain focus_chain,
                                  OnFocusChangeCallback callback) {
  last_focus_chain_ = std::move(focus_chain);
  callback();
}

bool UITestManager::ClientViewIsRendering() {
  if (!last_view_tree_snapshot_) {
    return false;
  }

  auto client_view_ref_koid = ClientViewRefKoid();
  if (!client_view_ref_koid) {
    return false;
  }

  return ViewDescriptorFromSnapshot(*last_view_tree_snapshot_, *client_view_ref_koid) !=
         std::nullopt;
}

bool UITestManager::ClientViewIsFocused() {
  if (!last_focus_chain_) {
    return false;
  }

  auto client_view_ref_koid = ClientViewRefKoid();
  if (!client_view_ref_koid) {
    return false;
  }

  if (!last_focus_chain_->has_focus_chain()) {
    return false;
  }

  if (last_focus_chain_->focus_chain().empty()) {
    return false;
  }

  return fsl::GetKoid(last_focus_chain_->focus_chain().back().reference.get()) ==
         client_view_ref_koid;
}

std::optional<zx_koid_t> UITestManager::ClientViewRefKoid() {
  if (!client_view_ref_) {
    return std::nullopt;
  }

  return fsl::GetKoid(client_view_ref_->reference.get());
}

float UITestManager::ClientViewScaleFactor() {
  if (!last_view_tree_snapshot_) {
    return kDefaultScale;
  }

  auto client_view_ref_koid = ClientViewRefKoid();
  if (!client_view_ref_koid) {
    return kDefaultScale;
  }

  const auto client_view_descriptor =
      ViewDescriptorFromSnapshot(*last_view_tree_snapshot_, *client_view_ref_koid);

  if (!client_view_descriptor || !client_view_descriptor->has_layout()) {
    return kDefaultScale;
  }

  const auto& pixel_scale = client_view_descriptor->layout().pixel_scale;

  return std::max(pixel_scale[0], pixel_scale[1]);
}

}  // namespace ui_testing
