// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

namespace integration_tests {
namespace {

// Name for the scenic component.
constexpr auto kScenic = "scenic";
constexpr auto kScenicRealmUrl = "#meta/scenic_only.cm";

}  // namespace

using Route = component_testing::Route;
using RealmRoot = component_testing::RealmRoot;
using Protocol = component_testing::Protocol;
using ChildRef = component_testing::ChildRef;
using ParentRef = component_testing::ParentRef;
using RealmBuilder = component_testing::RealmBuilder;
using component_testing::DirectoryContents;

ScenicRealmBuilder::ScenicRealmBuilder(RealmBuilderArgs args)
    : realm_builder_(RealmBuilder::CreateFromRelativeUrl(kScenicRealmUrl)) {
  Init(std::move(args));
}

ScenicRealmBuilder& ScenicRealmBuilder::Init(RealmBuilderArgs args) {
  // Route /config/data/scenic_config to scenic.
  auto config_directory_contents = DirectoryContents();
  config_directory_contents.AddFile("scenic_config", BuildScenicConfig(args.use_flatland));
  realm_builder_.RouteReadOnlyDirectory("config-data", {ChildRef{.name = kScenic}},
                                        std::move(config_directory_contents));

  // Route the protocols required by the scenic subrealm from the test_manager.
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                             Protocol{fuchsia::media::ProfileProvider::Name_},
                             Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                             Protocol{fuchsia::sysmem::Allocator::Name_},
                             Protocol{fuchsia::tracing::provider::Registry::Name_},
                             Protocol{fuchsia::vulkan::loader::Loader::Name_}},
            .source = ParentRef(),
            .targets = {ChildRef{kScenic}}});

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
                                  .source = ChildRef{kScenic},
                                  .targets = {ChildRef{config.name}}});
  }

  return *this;
}

ScenicRealmBuilder& ScenicRealmBuilder::AddRealmProtocol(const ProtocolName& protocol) {
  realm_builder_.AddRoute(Route{
      .capabilities = {Protocol{protocol}}, .source = ChildRef{kScenic}, .targets = {ParentRef()}});

  return *this;
}

ScenicRealmBuilder& ScenicRealmBuilder::AddMockComponent(const MockComponent& mock_component) {
  FX_DCHECK(mock_component.impl != nullptr) << "precondition.";

  realm_builder_.AddLocalChild(mock_component.name, mock_component.impl);

  return *this;
}

RealmRoot ScenicRealmBuilder::Build() { return realm_builder_.Build(); }

std::string ScenicRealmBuilder::BuildScenicConfig(bool use_flatland) {
  std::ostringstream config;

  config << "{"
         << "   \"i_can_haz_flatland\" : " << std::boolalpha << use_flatland << "}";

  return config.str();
}

}  // namespace integration_tests
