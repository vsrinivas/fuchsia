// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

namespace integration_tests {
namespace {

// Name for the scenic subrealm.
constexpr auto kScenicRealm = "scenic_realm";
constexpr auto kScenicRealmUrl = "#meta/scenic_only.cm";
constexpr auto kRootPresenter = "root_presenter";
constexpr auto kRootPresenterUrl = "#meta/root_presenter.cm";

}  // namespace

using Route = component_testing::Route;
using RealmRoot = component_testing::RealmRoot;
using Protocol = component_testing::Protocol;
using ChildRef = component_testing::ChildRef;
using ParentRef = component_testing::ParentRef;
using RealmBuilder = component_testing::RealmBuilder;

ScenicRealmBuilder::ScenicRealmBuilder(RealmBuilderArgs args)
    : realm_builder_(RealmBuilder::Create()) {
  if (args.scene_owner.has_value()) {
    if (args.scene_owner == SceneOwner::ROOT_PRESENTER) {
      scene_owner_ = {kRootPresenter, kRootPresenterUrl};
    }
  }

  Init(std::move(args));
}

ScenicRealmBuilder& ScenicRealmBuilder::Init(RealmBuilderArgs args) {
  // Configure the scenic subrealm for the test fixture.
  realm_builder_.AddChild(kScenicRealm, kScenicRealmUrl);

  // Route the protocols required by the scenic subrealm from the test_manager.
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                             Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                             Protocol{fuchsia::sysmem::Allocator::Name_},
                             Protocol{fuchsia::tracing::provider::Registry::Name_},
                             Protocol{fuchsia::vulkan::loader::Loader::Name_}},
            .source = ParentRef(),
            .targets = {ChildRef{kScenicRealm}}});

  // Configure the scene owner for the test fixture. This setup is done for tests requiring a scene
  // owner and a view provider.
  // TODO(fxb/95644): Add support for Scene Manager.
  if (scene_owner_.has_value()) {
    realm_builder_.AddChild(scene_owner_->first, scene_owner_->second);

    // Route the protocols required by the root presenter.
    realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                                  .source = ChildRef{kScenicRealm},
                                  .targets = {ChildRef{scene_owner_->first}}});
  }

  // Configure the ViewProvider for the test fixture. This setup is done for tests requiring a view
  // provider.
  if (args.view_provider_config.has_value()) {
    auto& config = args.view_provider_config.value();
    realm_builder_.AddChild(config.name, config.component_url);

    // Route the protocol exposed by the view provider.
    realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                                  .source = ChildRef{config.name},
                                  .targets = {ParentRef()}});

    // Route the protocols required by the view provider.
    realm_builder_.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                                  .source = ChildRef{kScenicRealm},
                                  .targets = {ChildRef{config.name}}});
  }

  return *this;
}

ScenicRealmBuilder& ScenicRealmBuilder::AddRealmProtocol(const ProtocolName& protocol) {
  realm_builder_.AddRoute(Route{.capabilities = {Protocol{protocol}},
                                .source = ChildRef{kScenicRealm},
                                .targets = {ParentRef()}});

  return *this;
}

ScenicRealmBuilder& ScenicRealmBuilder::AddSceneOwnerProtocol(const ProtocolName& protocol) {
  FX_DCHECK(scene_owner_.has_value()) << "precondition.";

  realm_builder_.AddRoute(Route{.capabilities = {Protocol{protocol}},
                                .source = ChildRef{scene_owner_->first},
                                .targets = {ParentRef()}});

  return *this;
}

ScenicRealmBuilder& ScenicRealmBuilder::AddMockComponent(const MockComponent& mock_component) {
  FX_DCHECK(mock_component.impl != nullptr) << "precondition.";

  realm_builder_.AddLocalChild(mock_component.name, mock_component.impl);

  return *this;
}

ScenicRealmBuilder& ScenicRealmBuilder::RouteMockComponentProtocolToSceneOwner(
    const std::string& component_name, const ProtocolName& protocol) {
  FX_DCHECK(scene_owner_.has_value()) << "precondition";

  realm_builder_.AddRoute(Route{.capabilities = {Protocol{protocol}},
                                .source = ChildRef{component_name},
                                .targets = {ChildRef{scene_owner_->first}}});

  return *this;
}

RealmRoot ScenicRealmBuilder::Build() { return realm_builder_.Build(); }

}  // namespace integration_tests
