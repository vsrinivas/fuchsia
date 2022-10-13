// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <optional>

#include <gtest/gtest.h>
#include <test/focus/cpp/fidl.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"
#include "src/ui/testing/util/flatland_test_view.h"
#include "src/ui/testing/util/gfx_test_view.h"

namespace {

// Types imported for the realm_builder library.
using component_testing::ChildRef;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;

constexpr auto kViewProvider = "view-provider";

std::vector<ui_testing::UITestRealm::Config> UIConfigurationsToTest() {
  std::vector<ui_testing::UITestRealm::Config> configs;

  // GFX x root presenter
  {
    ui_testing::UITestRealm::Config config;
    config.scene_owner = ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER;
    config.ui_to_client_services = {fuchsia::ui::scenic::Scenic::Name_};
    configs.push_back(config);
  }

  // GFX x scene manager
  {
    ui_testing::UITestRealm::Config config;
    config.scene_owner = ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER;
    config.ui_to_client_services = {fuchsia::ui::scenic::Scenic::Name_};
    configs.push_back(config);
  }

  // Flatland x scene manager
  {
    ui_testing::UITestRealm::Config config;
    config.use_flatland = true;
    config.scene_owner = ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER;
    config.ui_to_client_services = {fuchsia::ui::composition::Flatland::Name_,
                                    fuchsia::ui::composition::Allocator::Name_};
    configs.push_back(config);
  }

  return configs;
}

// This test fixture exercises the interactions between scenic, the scene owner,
// and a client view with respect to focus.
//
// The test uses the following components: scenic, root presnter, and a local
// mock component that provides a test client view.
class FocusInputTest : public gtest::RealLoopFixture,
                       public testing::WithParamInterface<ui_testing::UITestRealm::Config> {
 protected:
  FocusInputTest() = default;
  ~FocusInputTest() override = default;

  void SetUp() override {
    FX_LOGS(INFO) << "Setting up test case";
    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(GetParam());

    // Build realm.
    FX_LOGS(INFO) << "Building realm";
    realm_ = std::make_unique<Realm>(ui_test_manager_->AddSubrealm());

    auto config = GetParam();

    // Add a test view provider.
    if (config.use_flatland) {
      test_view_ = std::make_unique<ui_testing::FlatlandTestView>(
          dispatcher(), ui_testing::TestView::ContentType::COORDINATE_GRID);
    } else {
      test_view_ = std::make_unique<ui_testing::GfxTestView>(
          dispatcher(), ui_testing::TestView::ContentType::COORDINATE_GRID);
    }

    realm_->AddLocalChild(kViewProvider, test_view_.get());
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                           .source = ChildRef{kViewProvider},
                           .targets = {ParentRef()}});

    for (const auto& protocol : config.ui_to_client_services) {
      realm_->AddRoute(Route{.capabilities = {Protocol{protocol}},
                             .source = ParentRef(),
                             .targets = {ChildRef{kViewProvider}}});
    }

    ui_test_manager_->BuildRealm();
    realm_exposed_services_ = ui_test_manager_->CloneExposedServicesDirectory();

    FX_LOGS(INFO) << "Finished setup";
  }

  ui_testing::UITestManager* ui_test_manager() { return ui_test_manager_.get(); }
  sys::ServiceDirectory* realm_exposed_services() { return realm_exposed_services_.get(); }

 private:
  // Configures a RealmBuilder realm and manages scene on behalf of the test
  // fixture.
  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;

  // Exposed services directory for the realm owned by `ui_test_manager_`.
  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;

  // Configured by the test fixture, and attached as a subrealm to ui test
  // manager's realm.
  std::unique_ptr<Realm> realm_;

  // Presents trivial content to the scene.
  std::unique_ptr<ui_testing::TestView> test_view_;
};

INSTANTIATE_TEST_SUITE_P(FocusInputTestWithParams, FocusInputTest,
                         ::testing::ValuesIn(UIConfigurationsToTest()));

// This test exercises the focus contract with the scene owner: the view offered to the
// scene owner will have focus transferred to it. The test itself offers such a view to
// the scene owner (`test_view`).
TEST_P(FocusInputTest, TestView_ReceivesFocusTransfer_FromSceneOwner) {
  EXPECT_FALSE(ui_test_manager()->ClientViewIsFocused());

  // Create a test view, and attach to the scene.
  FX_LOGS(INFO) << "Starting test case";
  ui_test_manager()->InitializeScene();

  FX_LOGS(INFO) << "Waiting for focus change";
  RunLoopUntil([this] { return ui_test_manager()->ClientViewIsFocused(); });
}

// This test ensures that multiple clients can connect to the FocusChainListenerRegistry.
// It does not set up a scene; these "early" listeners should observe an empty focus chain.
// NOTE. This test does not use test.focus.ResponseListener. There's not a client that listens to
// ViewRefFocused.
TEST_P(FocusInputTest, SimultaneousCallsTo_FocusChainListenerRegistry) {
  // This implements the FocusChainListener class. Its purpose is to test that focus events
  // are actually sent out to the listeners.
  class FocusChainListenerImpl : public fuchsia::ui::focus::FocusChainListener {
   public:
    FocusChainListenerImpl(
        fidl::InterfaceRequest<fuchsia::ui::focus::FocusChainListener> listener_request,
        std::vector<fuchsia::ui::focus::FocusChain>& collector)
        : listener_binding_(this, std::move(listener_request)), collector_(collector) {}

   private:
    // |fuchsia.ui.focus.FocusChainListener|
    void OnFocusChange(fuchsia::ui::focus::FocusChain focus_chain,
                       OnFocusChangeCallback callback) override {
      collector_.push_back(std::move(focus_chain));
      callback();
    }

    fidl::Binding<fuchsia::ui::focus::FocusChainListener> listener_binding_;
    std::vector<fuchsia::ui::focus::FocusChain>& collector_;
  };

  // Register two Focus Chain listeners.
  std::vector<fuchsia::ui::focus::FocusChain> collected_a;
  fidl::InterfaceHandle<fuchsia::ui::focus::FocusChainListener> listener_a;
  auto listener_a_impl =
      std::make_unique<FocusChainListenerImpl>(listener_a.NewRequest(), collected_a);

  std::vector<fuchsia::ui::focus::FocusChain> collected_b;
  fidl::InterfaceHandle<fuchsia::ui::focus::FocusChainListener> listener_b;
  auto listener_b_impl =
      std::make_unique<FocusChainListenerImpl>(listener_b.NewRequest(), collected_b);

  // Connects to the listener registry and start listening.
  fuchsia::ui::focus::FocusChainListenerRegistryPtr focus_chain_listener_registry =
      realm_exposed_services()->Connect<fuchsia::ui::focus::FocusChainListenerRegistry>();
  focus_chain_listener_registry.set_error_handler([](zx_status_t status) {
    FX_LOGS(FATAL) << "Error from fuchsia::ui::focus::FocusChainListenerRegistry"
                   << zx_status_get_string(status);
  });
  focus_chain_listener_registry->Register(std::move(listener_a));
  focus_chain_listener_registry->Register(std::move(listener_b));

  RunLoopUntil([&collected_a, &collected_b] {
    // Wait until both listeners see their first report.
    return (collected_a.size() > 0 && collected_b.size() > 0);
  });

  // Client "a" is clean, and collected a focus chain.
  ASSERT_EQ(collected_a.size(), 1u);
  // It's empty, since there's no scene at time of connection.
  EXPECT_FALSE(collected_a[0].has_focus_chain());

  // Client "b" is clean, and collected a focus chain.
  ASSERT_EQ(collected_b.size(), 1u);
  // It's empty, since there's no scene at time of connection.
  EXPECT_FALSE(collected_b[0].has_focus_chain());
}

}  // namespace
