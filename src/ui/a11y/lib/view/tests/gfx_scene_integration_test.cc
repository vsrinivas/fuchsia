// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/accessibility/view/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"
#include "src/ui/testing/util/gfx_test_view.h"

namespace accessibility_test {
namespace {

using component_testing::ChildRef;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;

constexpr auto kViewProvider = "view-provider";
constexpr auto kRegistryProxy = "a11y-view-registry-proxy";
constexpr auto kA11yManager = "a11y-manager";
constexpr auto kA11yManagerUrl = "#meta/a11y-manager.cm";

// Proxies `fuchsia.ui.accessibility.view.Registry` between a11y manager and the
// scene owner. This proxy enables us to:
//
// (1) Observe the a11y view's ViewRef.
// (2) Synchronize our test based on when the a11y view has been inserted.
class AccessibilityViewRegistryProxy : public LocalComponent,
                                       public fuchsia::ui::accessibility::view::Registry {
 public:
  explicit AccessibilityViewRegistryProxy(async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher) {}
  ~AccessibilityViewRegistryProxy() override = default;

  // |LocalComponent|
  void Start(std::unique_ptr<LocalComponentHandles> local_handles) override {
    FX_CHECK(local_handles->outgoing()->AddPublicService(
                 fidl::InterfaceRequestHandler<fuchsia::ui::accessibility::view::Registry>(
                     [this](auto request) {
                       bindings_.AddBinding(this, std::move(request), dispatcher_);
                     })) == ZX_OK);
    local_handles_ = std::move(local_handles);

    registry_ = local_handles_->svc().Connect<fuchsia::ui::accessibility::view::Registry>();
  }

  // |fuchsia::ui::accessibility::view::Registry|
  void CreateAccessibilityViewHolder(fuchsia::ui::views::ViewRef a11y_view_ref,
                                     fuchsia::ui::views::ViewHolderToken a11y_view_holder_token,
                                     CreateAccessibilityViewHolderCallback callback) override {
    FX_LOGS(INFO)
        << "Accessibility view registry proxy received `CreateAccessibilityViewHolder` request";
    a11y_view_ref_ = std::move(a11y_view_ref);
    a11y_view_holder_token_ = std::move(a11y_view_holder_token);
    callback_ = std::move(callback);
    a11y_view_ref_koid_ = fsl::GetKoid(a11y_view_ref_->reference.get());
  }

  // Passes a11y view creation request received via
  // `CreateAccessibilityViewHolder` through to the scene owner.
  void PassCreateRequestToSceneOwner() {
    registry_->CreateAccessibilityViewHolder(
        std::move(*a11y_view_ref_), std::move(*a11y_view_holder_token_),
        [this](fuchsia::ui::views::ViewHolderToken proxy_view_holder_token) {
          // Pass the proxy view holder token back to a11y manager.
          callback_(std::move(proxy_view_holder_token));

          a11y_view_holder_created_ = true;
          a11y_view_ref_.reset();
          a11y_view_holder_token_.reset();
          callback_ = {};
        });
  }

  bool a11y_view_requested() { return a11y_view_ref_koid_ != ZX_KOID_INVALID; }

  bool a11y_view_holder_created() { return a11y_view_holder_created_; }

  zx_koid_t a11y_view_ref_koid() { return a11y_view_ref_koid_; }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::unique_ptr<LocalComponentHandles> local_handles_;
  fidl::BindingSet<fuchsia::ui::accessibility::view::Registry> bindings_;
  fuchsia::ui::accessibility::view::RegistryPtr registry_;
  std::optional<fuchsia::ui::views::ViewRef> a11y_view_ref_;
  std::optional<fuchsia::ui::views::ViewHolderToken> a11y_view_holder_token_;
  CreateAccessibilityViewHolderCallback callback_;
  zx_koid_t a11y_view_ref_koid_ = ZX_KOID_INVALID;
  bool a11y_view_holder_created_ = false;
};

// This test exercises the handshake between a11y manager and the scene owner
// (gfx scene manager or root presenter) to insert the a11y view into the scene.
// Specifically, it verifies that:
//
// 1. The scene is connected properly.
// 2. Focus is correctly transferred to the client root view after the scene
//    has been fully connected.
//
// The test runs a real scenic, a11y manager and scene owner (RP or GfxSM), and
// a proxy `fuchsia.ui.accessibility.view.Registry` component that sits between
// a11y manager and the scene owner. This component receives calls from a11y
// manager and passes them through to the scene owner. In doing so, it enables
// the test fixture to intercept the a11y view's ViewRef, which the test
// fixture can then use to verify that the a11y view is present in the scene
// graph.
class GfxAccessibilitySceneTestBase : public gtest::RealLoopFixture {
 public:
  GfxAccessibilitySceneTestBase() = default;
  ~GfxAccessibilitySceneTestBase() override = default;

  void SetUp() override {
    ui_testing::UITestRealm::Config config;
    config.scene_owner = SceneOwner();
    config.ui_to_client_services = {fuchsia::ui::scenic::Scenic::Name_,
                                    fuchsia::ui::accessibility::view::Registry::Name_};
    // Use this service to start the a11y manager explicitly.
    config.exposed_client_services = {fuchsia::accessibility::semantics::SemanticsManager::Name_};
    ui_test_manager_.emplace(std::move(config));

    FX_LOGS(INFO) << "Building realm";
    realm_.emplace(ui_test_manager_->AddSubrealm());

    // Add the a11y manager.
    realm_->AddChild(kA11yManager, kA11yManagerUrl);

    // Add a test view provider.
    test_view_.emplace(dispatcher(), /* content = */ ui_testing::TestView::ContentType::DEFAULT);
    realm_->AddLocalChild(kViewProvider, &(*test_view_));

    // Add the a11y view registry proxy.
    a11y_view_registry_proxy_.emplace(dispatcher());
    realm_->AddLocalChild(kRegistryProxy, &(*a11y_view_registry_proxy_));

    // Route low-level system services to a11y manager.
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_},
                                            Protocol{fuchsia::logger::LogSink::Name_}},
                           .source = ParentRef(),
                           .targets = {ChildRef{kA11yManager}}});

    // Expose `SemanticsManager` service out of the realm.
    realm_->AddRoute(Route{
        .capabilities = {Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_}},
        .source = ChildRef{kA11yManager},
        .targets = {ParentRef()}});

    // Expose `ViewProvider` service out of the realm.
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                           .source = ChildRef{kViewProvider},
                           .targets = {ParentRef()}});

    // Consume `Scenic` service from UI layer (via parent).
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                           .source = ParentRef(),
                           .targets = {ChildRef{kViewProvider}, ChildRef{kA11yManager}}});

    // Route accessibility view registry service from UI layer -> proxy -> a11y
    // manager.
    realm_->AddRoute(
        Route{.capabilities = {Protocol{fuchsia::ui::accessibility::view::Registry::Name_}},
              .source = ParentRef(),
              .targets = {ChildRef{kRegistryProxy}}});
    realm_->AddRoute(
        Route{.capabilities = {Protocol{fuchsia::ui::accessibility::view::Registry::Name_}},
              .source = ChildRef{kRegistryProxy},
              .targets = {ChildRef{kA11yManager}}});

    ui_test_manager_->BuildRealm();
    realm_exposed_services_ = ui_test_manager_->CloneExposedServicesDirectory();
  }

  virtual ui_testing::UITestRealm::SceneOwnerType SceneOwner() = 0;

 protected:
  std::optional<ui_testing::UITestManager> ui_test_manager_;
  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
  std::optional<Realm> realm_;
  std::optional<ui_testing::GfxTestView> test_view_;
  std::optional<AccessibilityViewRegistryProxy> a11y_view_registry_proxy_;
};

class ClientAttachesFirst
    : public GfxAccessibilitySceneTestBase,
      public ::testing::WithParamInterface<ui_testing::UITestRealm::SceneOwnerType> {
 public:
  ClientAttachesFirst() = default;
  ~ClientAttachesFirst() override = default;

  ui_testing::UITestRealm::SceneOwnerType SceneOwner() override { return GetParam(); }
};

INSTANTIATE_TEST_SUITE_P(GfxAccessibilityTestWithParams, ClientAttachesFirst,
                         ::testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                           ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER));

TEST_P(ClientAttachesFirst, SceneReconnectedAndClientRefocused) {
  FX_LOGS(INFO) << "Starting test case";

  // Attach the client view.
  ui_test_manager_->InitializeScene();
  FX_LOGS(INFO) << "Waiting for client view to render";
  RunLoopUntil([this] {
    return ui_test_manager_->ClientViewIsRendering() && ui_test_manager_->ClientViewIsFocused();
  });

  // Connect to the semantics manager service, which prompts a11y manager to
  // start.
  auto semantics_manager_unused =
      realm_exposed_services_->Connect<fuchsia::accessibility::semantics::SemanticsManager>();
  FX_LOGS(INFO) << "Waiting for a11y manager to request view";
  RunLoopUntil([this] { return a11y_view_registry_proxy_->a11y_view_requested(); });

  // The a11y view should not yet be part of the scene.
  // NOTE: Any view with a descendant that renders content is considered
  // "rendering".
  EXPECT_FALSE(ui_test_manager_->ViewIsRendering(a11y_view_registry_proxy_->a11y_view_ref_koid()));

  // Pass the a11y view request through to the scene owner, and wait until the
  // a11y view and client view are both attached to the scene and the client
  // view is re-focused.
  a11y_view_registry_proxy_->PassCreateRequestToSceneOwner();
  FX_LOGS(INFO) << "Waiting for a11y and client views to render";
  RunLoopUntil([this] {
    return ui_test_manager_->ClientViewIsRendering() &&
           ui_test_manager_->ViewIsRendering(a11y_view_registry_proxy_->a11y_view_ref_koid());
  });

  FX_LOGS(INFO) << "Waiting for client view to receive focus";
  RunLoopUntil([this] { return ui_test_manager_->ClientViewIsFocused(); });
}

// Root presenter waits for the client root view to attach before inserting the
// a11y view, so we can only test the case where the a11y view attaches first on
// scene manager.
class A11yViewAttachesFirstTest : public GfxAccessibilitySceneTestBase {
 public:
  A11yViewAttachesFirstTest() = default;
  ~A11yViewAttachesFirstTest() = default;

  ui_testing::UITestRealm::SceneOwnerType SceneOwner() override {
    return ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER;
  }
};

TEST_F(A11yViewAttachesFirstTest, A11yViewAttachesFirst) {
  FX_LOGS(INFO) << "Starting test case";

  // Connect to the semantics manager service, and wait for a11y manager to
  // request to insert its view.
  auto semantics_manager_unused =
      realm_exposed_services_->Connect<fuchsia::accessibility::semantics::SemanticsManager>();
  FX_LOGS(INFO) << "Waiting for a11y manager to request view";
  RunLoopUntil([this] { return a11y_view_registry_proxy_->a11y_view_requested(); });

  // The a11y view should not yet be part of the scene.
  // NOTE: Any view with a descendant that renders content is considered
  // "rendering".
  EXPECT_FALSE(ui_test_manager_->ViewIsRendering(a11y_view_registry_proxy_->a11y_view_ref_koid()));

  // Pass the a11y view request through to the scene owner, and wait until the
  // a11y view holder has been created.
  a11y_view_registry_proxy_->PassCreateRequestToSceneOwner();
  RunLoopUntil([this] { return a11y_view_registry_proxy_->a11y_view_holder_created(); });

  // Attach the client view, and wait for both the client and a11y views to be
  // attached to the scene.
  ui_test_manager_->InitializeScene();
  FX_LOGS(INFO) << "Waiting for a11y and client views to render";
  RunLoopUntil([this] {
    return ui_test_manager_->ClientViewIsRendering() &&
           ui_test_manager_->ViewIsRendering(a11y_view_registry_proxy_->a11y_view_ref_koid()) &&
           ui_test_manager_->ClientViewIsFocused();
  });
}

}  // namespace
}  // namespace accessibility_test
