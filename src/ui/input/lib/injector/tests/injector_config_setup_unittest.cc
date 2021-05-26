// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/injector/injector_config_setup.h"

#include <fuchsia/ui/pointerinjector/configuration/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace input {
namespace {

zx_koid_t ExtractKoid(const fuchsia::ui::views::ViewRef& view_ref) {
  zx_info_handle_basic_t info{};
  if (view_ref.reference.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) !=
      ZX_OK) {
    return ZX_KOID_INVALID;  // no info
  }

  return info.koid;
}

class InjectorConfigSetupTest : public gtest::TestLoopFixture {
 protected:
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(InjectorConfigSetupTest, GetViewRefs) {
  // Create InjectorConfigSetup.
  auto [context_control_ref, context_view_ref] = scenic::ViewRefPair::New();
  auto [target_control_ref, target_view_ref] = scenic::ViewRefPair::New();
  InjectorConfigSetup pointer_injector_setup(
      context_provider_.context(), fidl::Clone(context_view_ref), fidl::Clone(target_view_ref));

  // Connect to the Setup service exposed by InjectorConfigSetup.
  fuchsia::ui::pointerinjector::configuration::SetupPtr setup_proxy;
  context_provider_.ConnectToPublicService(setup_proxy.NewRequest());

  // Assert GetViewRefs() returns the correct viewrefs.
  bool checked_view_refs{false};
  auto context_view_ref_koid = ExtractKoid(context_view_ref);
  auto target_view_ref_koid = ExtractKoid(target_view_ref);
  setup_proxy->GetViewRefs(
      [&](fuchsia::ui::views::ViewRef context, fuchsia::ui::views::ViewRef target) {
        EXPECT_EQ(ExtractKoid(context), context_view_ref_koid);
        EXPECT_EQ(ExtractKoid(target), target_view_ref_koid);
        checked_view_refs = true;
      });

  RunLoopUntilIdle();
  EXPECT_TRUE(checked_view_refs);
}

TEST_F(InjectorConfigSetupTest, WatchViewport_ViewportExists) {
  // Create InjectorConfigSetup.
  auto [context_control_ref, context_view_ref] = scenic::ViewRefPair::New();
  auto [target_control_ref, target_view_ref] = scenic::ViewRefPair::New();
  InjectorConfigSetup pointer_injector_setup(
      context_provider_.context(), fidl::Clone(context_view_ref), fidl::Clone(target_view_ref));

  // Set a viewport.
  fuchsia::ui::pointerinjector::Viewport viewport;
  viewport.set_extents({{{0, 0}, {100, 100}}});
  viewport.set_viewport_to_context_transform({1, 0, 0, 0, 1, 0, 0, 0, 1});
  pointer_injector_setup.UpdateViewport(fidl::Clone(viewport));

  // Connect to the Setup service exposed by InjectorConfigSetup.
  fuchsia::ui::pointerinjector::configuration::SetupPtr setup_proxy;
  context_provider_.ConnectToPublicService(setup_proxy.NewRequest());

  // Assert WatchViewport() returns the correct viewport.
  bool watch_viewport_completed{false};
  setup_proxy->WatchViewport([&](fuchsia::ui::pointerinjector::Viewport watched_viewport) {
    EXPECT_EQ(watched_viewport.extents(), viewport.extents());
    EXPECT_EQ(watched_viewport.viewport_to_context_transform(),
              viewport.viewport_to_context_transform());
    watch_viewport_completed = true;
  });

  RunLoopUntilIdle();
  EXPECT_TRUE(watch_viewport_completed);
}

TEST_F(InjectorConfigSetupTest, WatchViewport_NoViewport) {
  // Create InjectorConfigSetup.
  auto [context_control_ref, context_view_ref] = scenic::ViewRefPair::New();
  auto [target_control_ref, target_view_ref] = scenic::ViewRefPair::New();
  InjectorConfigSetup pointer_injector_setup(
      context_provider_.context(), fidl::Clone(context_view_ref), fidl::Clone(target_view_ref));

  // Connect to the Setup service exposed by InjectorConfigSetup.
  fuchsia::ui::pointerinjector::configuration::SetupPtr setup_proxy;
  context_provider_.ConnectToPublicService(setup_proxy.NewRequest());

  // Call WatchViewport() before setting a viewport.
  bool watch_viewport_completed{false};
  fuchsia::ui::pointerinjector::Viewport watched_viewport;
  setup_proxy->WatchViewport([&](fuchsia::ui::pointerinjector::Viewport viewport) {
    watched_viewport = std::move(viewport);
    watch_viewport_completed = true;
  });

  // Set a viewport.
  fuchsia::ui::pointerinjector::Viewport viewport;
  viewport.set_extents({{{0, 0}, {100, 100}}});
  viewport.set_viewport_to_context_transform({1, 0, 0, 0, 1, 0, 0, 0, 1});
  pointer_injector_setup.UpdateViewport(fidl::Clone(viewport));

  RunLoopUntilIdle();
  EXPECT_TRUE(watch_viewport_completed);

  // Assert that the viewports match.
  EXPECT_EQ(watched_viewport.extents(), viewport.extents());
  EXPECT_EQ(watched_viewport.viewport_to_context_transform(),
            viewport.viewport_to_context_transform());
}

TEST_F(InjectorConfigSetupTest, WatchViewport_ViewportUpdated) {
  // Create InjectorConfigSetup.
  auto [context_control_ref, context_view_ref] = scenic::ViewRefPair::New();
  auto [target_control_ref, target_view_ref] = scenic::ViewRefPair::New();
  InjectorConfigSetup pointer_injector_setup(
      context_provider_.context(), fidl::Clone(context_view_ref), fidl::Clone(target_view_ref));

  // Set a viewport.
  fuchsia::ui::pointerinjector::Viewport viewport;
  viewport.set_extents({{{0, 0}, {100, 100}}});
  viewport.set_viewport_to_context_transform({1, 0, 0, 0, 1, 0, 0, 0, 1});
  pointer_injector_setup.UpdateViewport(std::move(viewport));

  // Connect to the Setup service exposed by InjectorConfigSetup.
  fuchsia::ui::pointerinjector::configuration::SetupPtr setup_proxy;
  context_provider_.ConnectToPublicService(setup_proxy.NewRequest());

  // Update the viewport.
  fuchsia::ui::pointerinjector::Viewport updated_viewport;
  updated_viewport.set_extents({{{200, 200}, {300, 300}}});
  updated_viewport.set_viewport_to_context_transform({0, 0, 1, 0, 1, 0, 1, 0, 0});
  pointer_injector_setup.UpdateViewport(fidl::Clone(updated_viewport));

  // Assert WatchViewport() returns the updated viewport.
  bool watch_viewport_completed{false};
  setup_proxy->WatchViewport([&](fuchsia::ui::pointerinjector::Viewport watched_viewport) {
    EXPECT_EQ(watched_viewport.extents(), updated_viewport.extents());
    EXPECT_EQ(watched_viewport.viewport_to_context_transform(),
              updated_viewport.viewport_to_context_transform());
    watch_viewport_completed = true;
  });

  RunLoopUntilIdle();
  EXPECT_TRUE(watch_viewport_completed);
}

TEST_F(InjectorConfigSetupTest, WatchViewport_CalledTwice) {
  // Create InjectorConfigSetup.
  auto [context_control_ref, context_view_ref] = scenic::ViewRefPair::New();
  auto [target_control_ref, target_view_ref] = scenic::ViewRefPair::New();
  InjectorConfigSetup pointer_injector_setup(
      context_provider_.context(), fidl::Clone(context_view_ref), fidl::Clone(target_view_ref));

  // Connect to the Setup service exposed by InjectorConfigSetup.
  fuchsia::ui::pointerinjector::configuration::SetupPtr setup_proxy;
  bool setup_connection_error{false};
  setup_proxy.set_error_handler([&setup_connection_error](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_BAD_STATE);
    setup_connection_error = true;
  });
  context_provider_.ConnectToPublicService(setup_proxy.NewRequest());

  // Assert WatchViewport() fails when called twice.
  setup_proxy->WatchViewport([&](fuchsia::ui::pointerinjector::Viewport) {});
  setup_proxy->WatchViewport([&](fuchsia::ui::pointerinjector::Viewport) {});

  RunLoopUntilIdle();
  EXPECT_TRUE(setup_connection_error);
}

TEST_F(InjectorConfigSetupTest, WatchViewport_ReconnectWithOutstandingCall) {
  // Create InjectorConfigSetup.
  auto [context_control_ref, context_view_ref] = scenic::ViewRefPair::New();
  auto [target_control_ref, target_view_ref] = scenic::ViewRefPair::New();
  InjectorConfigSetup pointer_injector_setup(
      context_provider_.context(), fidl::Clone(context_view_ref), fidl::Clone(target_view_ref));

  // Connect to the Setup service exposed by InjectorConfigSetup.
  fuchsia::ui::pointerinjector::configuration::SetupPtr setup_proxy;
  context_provider_.ConnectToPublicService(setup_proxy.NewRequest());

  // Disconnect after calling WatchViewport().
  setup_proxy->WatchViewport([&](fuchsia::ui::pointerinjector::Viewport) {});
  setup_proxy.Unbind();
  RunLoopUntilIdle();

  // Set a viewport so WatchViewport() returns.
  fuchsia::ui::pointerinjector::Viewport viewport;
  viewport.set_extents({{{0, 0}, {100, 100}}});
  viewport.set_viewport_to_context_transform({1, 0, 0, 0, 1, 0, 0, 0, 1});
  pointer_injector_setup.UpdateViewport(std::move(viewport));

  // Reconnect and call to WatchViewport() should succeed.
  context_provider_.ConnectToPublicService(setup_proxy.NewRequest());
  bool watched_viewport{false};
  setup_proxy->WatchViewport(
      [&](fuchsia::ui::pointerinjector::Viewport) { watched_viewport = true; });

  RunLoopUntilIdle();
  EXPECT_TRUE(watched_viewport);
}

TEST_F(InjectorConfigSetupTest, WatchViewport_ReconnectWithoutOutstandingCall) {
  // Create InjectorConfigSetup.
  auto [context_control_ref, context_view_ref] = scenic::ViewRefPair::New();
  auto [target_control_ref, target_view_ref] = scenic::ViewRefPair::New();
  InjectorConfigSetup pointer_injector_setup(
      context_provider_.context(), fidl::Clone(context_view_ref), fidl::Clone(target_view_ref));

  // Connect to the Setup service exposed by InjectorConfigSetup and immediately disconnect.
  fuchsia::ui::pointerinjector::configuration::SetupPtr setup_proxy;
  context_provider_.ConnectToPublicService(setup_proxy.NewRequest());
  setup_proxy.Unbind();
  RunLoopUntilIdle();

  // Set a viewport so WatchViewport() returns.
  fuchsia::ui::pointerinjector::Viewport viewport;
  viewport.set_extents({{{0, 0}, {100, 100}}});
  viewport.set_viewport_to_context_transform({1, 0, 0, 0, 1, 0, 0, 0, 1});
  pointer_injector_setup.UpdateViewport(std::move(viewport));

  // Reconnect and call to WatchViewport() should succeed.
  context_provider_.ConnectToPublicService(setup_proxy.NewRequest());
  bool watched_viewport{false};
  setup_proxy->WatchViewport(
      [&](fuchsia::ui::pointerinjector::Viewport) { watched_viewport = true; });

  RunLoopUntilIdle();
  EXPECT_TRUE(watched_viewport);
}

}  // namespace
}  // namespace input
