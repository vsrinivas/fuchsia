// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/scene/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <zircon/status.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/a11y/lib/view/flatland_accessibility_view.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"

namespace accessibility_test {
namespace {

// This test fixture sets up a test realm with scenic and a11y manager.
// The test fixture mocks the "scene owner" portion of the handshake by creating
// a flatland display, attaching the a11y viewport as its content, and
// requesting the a11y manager to insert its view. Finally, the test fixture
// inserts a proxy view as a child of the proxy viewport the a11y manager
// creates. If the proxy view is attached to the scene, the a11y manager must
// have performed its portion of the handshake correctly. The final topology
// should be:
//
//      flatland display (owned by test fixture)
//            |
//      a11y view transform (owned by a11y manager)
//            |
//      proxy viewport transform (owned by a11y manager)
//            |
//       proxy view transform (owned by test fixture)
class FlatlandAccessibilityViewTest : public gtest::RealLoopFixture {
 public:
  FlatlandAccessibilityViewTest() = default;
  ~FlatlandAccessibilityViewTest() override = default;

  void SetUp() override {
    // Don't specify a scene_owner to force a scenic-only realm.
    ui_testing::UITestRealm::Config config;
    config.use_flatland = true;
    config.ui_to_client_services = {fuchsia::ui::scenic::Scenic::Name_,
                                    fuchsia::ui::composition::Flatland::Name_};
    config.exposed_client_services = {fuchsia::accessibility::scene::Provider::Name_};
    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(std::move(config));

    ui_test_manager_->BuildRealm();
    realm_exposed_services_ = ui_test_manager_->CloneExposedServicesDirectory();
  }

  sys::ServiceDirectory* realm_exposed_services() { return realm_exposed_services_.get(); }

  void WatchProxyViewStatus() {
    FX_LOGS(INFO) << "Watching proxy view status";
    proxy_viewport_watcher_->GetStatus(
        [this](fuchsia::ui::composition::ParentViewportStatus status) {
          if (status == fuchsia::ui::composition::ParentViewportStatus::CONNECTED_TO_DISPLAY) {
            proxy_view_attached_ = true;
          } else {
            proxy_view_attached_ = false;
          }

          FX_LOGS(INFO) << "proxy_view_attached_ = " << std::boolalpha << proxy_view_attached_;

          WatchProxyViewStatus();
        });
  }

 protected:
  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;
  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
  bool proxy_view_attached_ = false;
  fidl::InterfacePtr<fuchsia::ui::composition::ParentViewportWatcher> proxy_viewport_watcher_;
};

TEST_F(FlatlandAccessibilityViewTest, TestSceneConnected) {
  auto flatland_display =
      realm_exposed_services()->Connect<fuchsia::ui::composition::FlatlandDisplay>();
  auto proxy_flatland = realm_exposed_services()->Connect<fuchsia::ui::composition::Flatland>();

  a11y::FlatlandAccessibilityView a11y_view(
      realm_exposed_services()->Connect<fuchsia::ui::composition::Flatland>(),
      realm_exposed_services()->Connect<fuchsia::ui::composition::Flatland>());

  // Set up the display, and add the a11y viewport as the display content.
  // Note that we don't need an extra view between the display and the a11y
  // view; we're only concerned if the a11y view creates its view and the
  // proxy viewport correctly.
  fidl::InterfacePtr<fuchsia::ui::composition::ChildViewWatcher> child_view_watcher;
  auto [a11y_view_token, a11y_viewport_token] = scenic::ViewCreationTokenPair::New();
  flatland_display->SetContent(std::move(a11y_viewport_token), child_view_watcher.NewRequest());

  // Create the proxy view/viewport tokens.
  auto [proxy_view_token, proxy_viewport_token] = scenic::ViewCreationTokenPair::New();

  // Request for the a11y manager to insert its view.
  a11y_view.CreateView(std::move(a11y_view_token), std::move(proxy_viewport_token));

  // Create the proxy view.
  fidl::InterfacePtr<fuchsia::ui::composition::ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  proxy_flatland->CreateView2(std::move(proxy_view_token), std::move(identity), {},
                              proxy_viewport_watcher_.NewRequest());

  // Watch for connected/disconnected to display events for the proxy view.
  WatchProxyViewStatus();

  proxy_flatland->Present({});

  // Run until the proxy view has been attached to the scene, which can only
  // happen if the a11y manager has correctly inserted its view.
  RunLoopUntil([this]() { return proxy_view_attached_; });

  // Verify that the a11y view is ready.
  bool a11y_view_ready = false;
  a11y_view.add_scene_ready_callback([&a11y_view_ready]() { return a11y_view_ready = true; });
  RunLoopUntil([&a11y_view_ready] { return a11y_view_ready; });

  EXPECT_TRUE(a11y_view_ready);

  // Verify that the a11y view has its ViewRef.
  EXPECT_TRUE(a11y_view.view_ref().has_value());
}

}  // namespace
}  // namespace accessibility_test
