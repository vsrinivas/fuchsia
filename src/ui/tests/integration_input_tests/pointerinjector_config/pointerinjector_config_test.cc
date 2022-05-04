// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/accessibility/view/cpp/fidl.h>
#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/configuration/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <gtest/gtest.h>
#include <test/accessibility/cpp/fidl.h>

#include "src/ui/a11y/lib/magnifier/tests/mocks/mock_magnifier.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"

// Types imported for the realm_builder library.
using component_testing::ChildRef;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;
using RealmBuilder = component_testing::RealmBuilder;

constexpr zx::duration kTimeout = zx::min(5);

zx_koid_t ExtractKoid(const fuchsia::ui::views::ViewRef& view_ref) {
  zx_info_handle_basic_t info{};
  if (view_ref.reference.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) !=
      ZX_OK) {
    return ZX_KOID_INVALID;  // no info
  }

  return info.koid;
}

class PointerInjectorConfigTest : public gtest::RealLoopFixture {
 protected:
  PointerInjectorConfigTest() = default;
  ~PointerInjectorConfigTest() override {}

  void SetUp() override {
    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);

    // Initialize ui test manager.
    ui_testing::UITestManager::Config config;
    config.scene_owner = ui_testing::UITestManager::SceneOwnerType::ROOT_PRESENTER;
    config.accessibility_owner = ui_testing::UITestManager::AccessibilityOwnerType::FAKE;
    config.use_input = true;
    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(config);

    ui_test_manager_->BuildRealm();
    realm_exposed_services_ = ui_test_manager_->TakeExposedServicesDirectory();
  }

  Realm* realm() { return realm_.get(); }
  sys::ServiceDirectory* realm_exposed_services() { return realm_exposed_services_.get(); }

 private:
  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;
  std::unique_ptr<Realm> realm_;
  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
};

// Checks that GetViewRefs() returns the same ViewRefs after a11y registers a view.
TEST_F(PointerInjectorConfigTest, GetViewRefs) {
  // Connect to pointerinjector::configuration::Setup.
  auto config_setup =
      realm_exposed_services()->Connect<fuchsia::ui::pointerinjector::configuration::Setup>();

  // Get ViewRefs before a11y sets up.
  std::optional<fuchsia::ui::views::ViewRef> first_context;
  std::optional<fuchsia::ui::views::ViewRef> first_target;
  config_setup->GetViewRefs(
      [&](fuchsia::ui::views::ViewRef context, fuchsia::ui::views::ViewRef target) {
        first_context = std::move(context);
        first_target = std::move(target);
      });

  RunLoopUntil([&first_context, &first_target]() {
    return first_context.has_value() && first_target.has_value();
  });

  // Create view token and view ref pairs for a11y view.
  auto [a11y_view_token, a11y_view_holder_token] = scenic::ViewTokenPair::New();
  auto [a11y_control_ref, a11y_view_ref] = scenic::ViewRefPair::New();
  auto a11y = realm_exposed_services()->Connect<fuchsia::ui::accessibility::view::Registry>();
  a11y->CreateAccessibilityViewHolder(std::move(a11y_view_ref), std::move(a11y_view_holder_token),
                                      [](fuchsia::ui::views::ViewHolderToken) {});

  // Get ViewRefs after a11y is set up.
  bool checked_view_refs = false;
  config_setup->GetViewRefs(
      [&](fuchsia::ui::views::ViewRef context, fuchsia::ui::views::ViewRef target) {
        // Check that the ViewRefs match
        EXPECT_EQ(ExtractKoid(context), ExtractKoid(*first_context));
        EXPECT_EQ(ExtractKoid(target), ExtractKoid(*first_target));
        checked_view_refs = true;
      });

  RunLoopUntil([&] { return checked_view_refs; });
}

TEST_F(PointerInjectorConfigTest, WatchViewport) {
  // Connect to pointerinjector::configuration::Setup.
  auto config_setup =
      realm_exposed_services()->Connect<fuchsia::ui::pointerinjector::configuration::Setup>();

  // Get the starting viewport.
  fuchsia::ui::pointerinjector::Viewport starting_viewport, updated_viewport;
  bool watch_viewport_returned = false;
  config_setup->WatchViewport([&](fuchsia::ui::pointerinjector::Viewport viewport) {
    starting_viewport = std::move(viewport);
    watch_viewport_returned = true;
  });

  RunLoopUntil([&watch_viewport_returned] { return watch_viewport_returned; });

  // Queue another call to WatchViewport().
  bool viewport_updated = false;
  config_setup->WatchViewport([&](fuchsia::ui::pointerinjector::Viewport viewport) {
    updated_viewport = std::move(viewport);
    viewport_updated = true;
  });

  // Trigger a viewport update and assert that the queued WatchViewport() returns.
  auto magnifier = realm_exposed_services()->Connect<test::accessibility::Magnifier>();
  magnifier->SetMagnification(100, 100, 100, []() {});
  RunLoopUntil([&] { return viewport_updated; });

  EXPECT_NE(updated_viewport.viewport_to_context_transform(),
            starting_viewport.viewport_to_context_transform());
}
