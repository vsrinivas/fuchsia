// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/accessibility/view/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/configuration/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <gtest/gtest.h>

#include "src/ui/a11y/lib/magnifier/tests/mocks/mock_magnifier.h"

constexpr char kRootPresenter[] =
    "fuchsia-pkg://fuchsia.com/pointerinjector-config-test#meta/root_presenter.cmx";
constexpr char kScenic[] = "fuchsia-pkg://fuchsia.com/pointerinjector-config-test#meta/scenic.cmx";
constexpr zx::duration kTimeout = zx::min(5);

zx_koid_t ExtractKoid(const fuchsia::ui::views::ViewRef& view_ref) {
  zx_info_handle_basic_t info{};
  if (view_ref.reference.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) !=
      ZX_OK) {
    return ZX_KOID_INVALID;  // no info
  }

  return info.koid;
}

// Common services for each test.
const std::map<std::string, std::string> LocalServices() {
  return {
      // Root presenter is included in this test's package.
      {fuchsia::ui::pointerinjector::configuration::Setup::Name_, kRootPresenter},
      {fuchsia::ui::policy::Presenter::Name_, kRootPresenter},
      // Scenic protocols.
      {"fuchsia.ui.scenic.Scenic", kScenic},
      {"fuchsia.ui.focus.FocusChainListenerRegistry", kScenic},
      // Misc protocols.
      {"fuchsia.cobalt.LoggerFactory",
       "fuchsia-pkg://fuchsia.com/mock_cobalt#meta/mock_cobalt.cmx"},
      {"fuchsia.hardware.display.Provider",
       "fuchsia-pkg://fuchsia.com/fake-hardware-display-controller-provider#meta/hdcp.cmx"},
  };
}

// Allow these global services from outside the test environment.
const std::vector<std::string> GlobalServices() {
  return {"fuchsia.vulkan.loader.Loader", "fuchsia.sysmem.Allocator",
          "fuchsia.scheduler.ProfileProvider"};
}

class PointerInjectorConfigTest : public gtest::TestWithEnvironmentFixture {
 protected:
  explicit PointerInjectorConfigTest() {
    auto services = sys::testing::EnvironmentServices::Create(real_env());
    zx_status_t is_ok;

    // Add common services.
    for (const auto& [name, url] : LocalServices()) {
      const zx_status_t is_ok = services->AddServiceWithLaunchInfo({.url = url}, name);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << name;
    }

    // Enable services from outside this test.
    for (const auto& service : GlobalServices()) {
      const zx_status_t is_ok = services->AllowParentService(service);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << service;
    }

    // Setup MockMagnifier for Root Presenter to connect to.
    is_ok = services->AddService(magnifier_bindings_.GetHandler(&magnifier_));
    FX_CHECK(is_ok == ZX_OK);

    test_env_ = CreateNewEnclosingEnvironment(
        "pointerinjector_config_test_env", std::move(services), {.inherit_parent_services = true});

    WaitForEnclosingEnvToStart(test_env_.get());

    // A dummy call to PresentView() so that Scenic services are initialized.
    auto root_presenter = test_env_->ConnectToService<fuchsia::ui::policy::Presenter>();
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
    root_presenter->PresentView(std::move(view_holder_token), nullptr);

    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);
  }

  ~PointerInjectorConfigTest() override {}

  accessibility_test::MockMagnifier magnifier_;
  fidl::BindingSet<fuchsia::accessibility::Magnifier> magnifier_bindings_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> test_env_;
};

// Checks that GetViewRefs() returns the same ViewRefs after a11y registers a view.
TEST_F(PointerInjectorConfigTest, GetViewRefs) {
  // Connect to pointerinjector::configuration::Setup.
  auto config_setup =
      test_env_->ConnectToService<fuchsia::ui::pointerinjector::configuration::Setup>();

  // Get ViewRefs before a11y sets up.
  fuchsia::ui::views::ViewRef first_context;
  fuchsia::ui::views::ViewRef first_target;
  config_setup->GetViewRefs(
      [&](fuchsia::ui::views::ViewRef context, fuchsia::ui::views::ViewRef target) {
        first_context = std::move(context);
        first_target = std::move(target);
      });

  // Create view token and view ref pairs for a11y view.
  auto [a11y_view_token, a11y_view_holder_token] = scenic::ViewTokenPair::New();
  auto [a11y_control_ref, a11y_view_ref] = scenic::ViewRefPair::New();
  auto a11y = test_env_->ConnectToService<fuchsia::ui::accessibility::view::Registry>();
  a11y->CreateAccessibilityViewHolder(std::move(a11y_view_ref), std::move(a11y_view_holder_token),
                                      [](fuchsia::ui::views::ViewHolderToken) {});

  // Get ViewRefs after a11y is set up.
  bool checked_view_refs = false;
  config_setup->GetViewRefs(
      [&](fuchsia::ui::views::ViewRef context, fuchsia::ui::views::ViewRef target) {
        // Check that the ViewRefs match
        EXPECT_EQ(ExtractKoid(context), ExtractKoid(first_context));
        EXPECT_EQ(ExtractKoid(target), ExtractKoid(first_target));
        checked_view_refs = true;
      });

  RunLoopUntil([&] { return checked_view_refs; });
}

TEST_F(PointerInjectorConfigTest, WatchViewport) {
  // Connect to pointerinjector::configuration::Setup.
  auto config_setup =
      test_env_->ConnectToService<fuchsia::ui::pointerinjector::configuration::Setup>();

  // Get the starting viewport.
  fuchsia::ui::pointerinjector::Viewport starting_viewport, updated_viewport;
  bool watch_viewport_returned = false;
  config_setup->WatchViewport([&](fuchsia::ui::pointerinjector::Viewport viewport) {
    starting_viewport = std::move(viewport);
    watch_viewport_returned = true;
  });

  RunLoopUntil([this, &watch_viewport_returned] {
    return magnifier_.handler().is_bound() && watch_viewport_returned;
  });

  // Queue another call to WatchViewport().
  bool viewport_updated = false;
  config_setup->WatchViewport([&](fuchsia::ui::pointerinjector::Viewport viewport) {
    updated_viewport = std::move(viewport);
    viewport_updated = true;
  });

  // Trigger a viewport update and assert that the queued WatchViewport() returns.
  magnifier_.handler()->SetClipSpaceTransform(100, 100, 100, []() {});
  RunLoopUntil([&] { return viewport_updated; });

  EXPECT_NE(updated_viewport.viewport_to_context_transform(),
            starting_viewport.viewport_to_context_transform());
}
