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
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"

namespace accessibility_test {
namespace {

using component_testing::ChildRef;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;

constexpr auto kA11yManager = "a11y-manager";
constexpr auto kA11yManagerUrl = "#meta/a11y-manager.cm";

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
    ui_testing::UITestManager::Config config;
    config.use_flatland = true;
    config.ui_to_client_services = {fuchsia::ui::scenic::Scenic::Name_,
                                    fuchsia::ui::composition::Flatland::Name_};
    config.exposed_client_services = {fuchsia::accessibility::scene::Provider::Name_};
    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(std::move(config));

    FX_LOGS(INFO) << "Building realm";
    realm_ = std::make_unique<Realm>(ui_test_manager_->AddSubrealm());

    // Add real a11y manager.
    realm_->AddChild(kA11yManager, kA11yManagerUrl);

    // Route tracing provider to a11y manager.
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_},
                                            Protocol{fuchsia::logger::LogSink::Name_},
                                            Protocol{fuchsia::ui::scenic::Scenic::Name_},
                                            Protocol{fuchsia::ui::composition::Flatland::Name_}},
                           .source = ParentRef(),
                           .targets = {ChildRef{kA11yManager}}});

    // Route accessibility view registry from scene owner to a11y manager.
    realm_->AddRoute(
        Route{.capabilities = {Protocol{fuchsia::accessibility::scene::Provider::Name_}},
              .source = ChildRef{kA11yManager},
              .targets = {ParentRef()}});

    ui_test_manager_->BuildRealm();
    realm_exposed_services_ = ui_test_manager_->TakeExposedServicesDirectory();
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
  std::unique_ptr<Realm> realm_;
  bool proxy_view_attached_ = false;
  fidl::InterfacePtr<fuchsia::ui::composition::ParentViewportWatcher> proxy_viewport_watcher_;
};

TEST_F(FlatlandAccessibilityViewTest, TestSceneConnected) {
  auto flatland_display =
      realm_exposed_services()->Connect<fuchsia::ui::composition::FlatlandDisplay>();
  auto proxy_session = realm_exposed_services()->Connect<fuchsia::ui::composition::Flatland>();

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
  auto a11y_view_provider =
      realm_exposed_services()->Connect<fuchsia::accessibility::scene::Provider>();
  a11y_view_provider->CreateView(std::move(a11y_view_token), std::move(proxy_viewport_token));

  // Create the proxy view.
  fidl::InterfacePtr<fuchsia::ui::composition::ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  proxy_session->CreateView2(std::move(proxy_view_token), std::move(identity), {},
                             proxy_viewport_watcher_.NewRequest());

  // Watch for connected/disconnected to display events for the proxy view.
  WatchProxyViewStatus();

  proxy_session->Present({});

  // Run until the proxy view has been attached to the scene, which can only
  // happen if the a11y manager has correctly inserted its view.
  RunLoopUntil([this]() { return proxy_view_attached_; });
}

}  // namespace
}  // namespace accessibility_test
