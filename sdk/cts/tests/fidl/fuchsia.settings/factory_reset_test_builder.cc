// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/settings/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/realm_builder.h>
#include <lib/sys/cpp/testing/realm_builder_types.h>

#include <zxtest/zxtest.h>

#include "fuchsia/mem/cpp/fidl.h"

namespace sys::testing {

class FactoryResetTest : public zxtest::Test {};

static Realm::Builder setup() {
  auto context = sys::ComponentContext::Create();
  auto realm_builder = Realm::Builder::New(context.get());

  // Add setui_service child.
  static constexpr auto kSetuiService = Moniker{"setui_service"};
  static constexpr auto kSetuiServiceUrl =
      "fuchsia-pkg://fuchsia.com/setui_service#meta/setui_service.cmx";
  realm_builder.AddComponent(kSetuiService,
                             Component{.source = LegacyComponentUrl{kSetuiServiceUrl}});
  realm_builder.AddRoute(
      CapabilityRoute{.capability = Protocol{fuchsia::settings::FactoryReset::Name_},
                      .source = kSetuiService,
                      .targets = {AboveRoot()}});

  // Add stash child as setui_service's dependency.
  static constexpr auto kStash = Moniker{"stash"};
  static constexpr auto kStashUrl = "fuchsia-pkg://fuchsia.com/stash#meta/stash.cm";
  realm_builder.AddComponent(kStash, Component{.source = ComponentUrl{kStashUrl}});
  realm_builder.AddRoute(CapabilityRoute{
      .capability = Protocol{"fuchsia.stash.Store"}, .source = kStash, .targets = {kSetuiService}});
  realm_builder.AddRoute(CapabilityRoute{
      .capability = Storage{"data", "/data"}, .source = AboveRoot(), .targets = {kStash}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"fuchsia.logger.LogSink"},
                                         .source = AboveRoot(),
                                         .targets = {kStash}});

  // Add root_presenter child as setui_service's dependency.
  static constexpr auto kRootPresenter = Moniker{"root_presenter"};
  static constexpr auto kRootPresenterUrl =
      "fuchsia-pkg://fuchsia.com/root_presenter#meta/root_presenter.cmx";
  realm_builder.AddComponent(kRootPresenter,
                             Component{.source = LegacyComponentUrl{kRootPresenterUrl}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"fuchsia.recovery.policy.Device"},
                                         .source = kRootPresenter,
                                         .targets = {kSetuiService}});
  realm_builder.AddRoute(
      CapabilityRoute{.capability = Protocol{"fuchsia.ui.policy.DeviceListenerRegistry"},
                      .source = kRootPresenter,
                      .targets = {kSetuiService}});

  // Add scenic child as root_presenter's dependency.
  static constexpr auto kScenic = Moniker{"scenic"};
  static constexpr auto kScenicUrl = "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx";
  realm_builder.AddComponent(kScenic, Component{.source = LegacyComponentUrl{kScenicUrl}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"fuchsia.ui.scenic.Scenic"},
                                         .source = kScenic,
                                         .targets = {kRootPresenter}});

  return realm_builder;
}

// Tests that Set() results in an update to factory reset settings.
TEST_F(FactoryResetTest, Set) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  auto realm_builder = setup();
  auto realm = realm_builder.Build(loop.dispatcher());
  auto factory_reset = realm.ConnectSync<fuchsia::settings::FactoryReset>();

  // Setup initial FactoryResetSettings.
  fuchsia::settings::FactoryResetSettings init_settings;
  fuchsia::settings::FactoryReset_Set_Result result;
  init_settings.set_is_local_reset_allowed(true);
  EXPECT_EQ(ZX_OK, factory_reset->Set(std::move(init_settings), &result));

  // Verify initial settings.
  fuchsia::settings::FactoryResetSettings got_settings;
  EXPECT_EQ(ZX_OK, factory_reset->Watch(&got_settings));
  EXPECT_TRUE(got_settings.is_local_reset_allowed());

  // Flip the settings.
  fuchsia::settings::FactoryResetSettings new_settings;
  new_settings.set_is_local_reset_allowed(false);
  EXPECT_EQ(ZX_OK, factory_reset->Set(std::move(new_settings), &result));

  // Verify the new settings.
  EXPECT_EQ(ZX_OK, factory_reset->Watch(&got_settings));
  EXPECT_FALSE(got_settings.is_local_reset_allowed());
}

}  // namespace sys::testing
