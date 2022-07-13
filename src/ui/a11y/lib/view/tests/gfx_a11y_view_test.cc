// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/accessibility/view/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"

namespace accessibility_test {
namespace {

using component_testing::ChildRef;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;

constexpr auto kMockSceneOwner = "scene-owner";

constexpr auto kA11yManager = "a11y-manager";
constexpr auto kA11yManagerUrl = "#meta/a11y-manager.cm";

scenic::Session CreateSession(fuchsia::ui::scenic::Scenic* scenic,
                              fuchsia::ui::scenic::SessionEndpoints endpoints) {
  FX_DCHECK(scenic);
  FX_DCHECK(!endpoints.has_session());
  FX_DCHECK(!endpoints.has_session_listener());

  fuchsia::ui::scenic::SessionPtr session_ptr;
  fuchsia::ui::scenic::SessionListenerHandle listener_handle;
  auto listener_request = listener_handle.NewRequest();

  endpoints.set_session(session_ptr.NewRequest());
  endpoints.set_session_listener(std::move(listener_handle));
  scenic->CreateSessionT(std::move(endpoints), [] {});

  return scenic::Session(std::move(session_ptr), std::move(listener_request));
}

// Sets up the root of a scene.
// Present() must be called separately by the creator, since this does not have access to the
// looper.
struct RootSession {
  RootSession(fuchsia::ui::scenic::Scenic* scenic, fuchsia::ui::scenic::SessionEndpoints endpoints)
      : session(CreateSession(scenic, std::move(endpoints))),
        compositor(&session),
        layer_stack(&session),
        layer(&session),
        renderer(&session),
        scene(&session),
        camera(scene) {
    compositor.SetLayerStack(layer_stack);
    layer_stack.AddLayer(layer);
    layer.SetRenderer(renderer);
    renderer.SetCamera(camera);
  }

  scenic::Session session;
  scenic::DisplayCompositor compositor;
  scenic::LayerStack layer_stack;
  scenic::Layer layer;
  scenic::Renderer renderer;
  scenic::Scene scene;
  scenic::Camera camera;
  std::unique_ptr<scenic::ViewHolder> a11y_view_holder;
  std::unique_ptr<scenic::View> proxy_view;
};

// See GfxAccessibilityViewTest documentation below for details on the mock
// scene owner's role in the test.
class MockSceneOwner : public LocalComponent, public fuchsia::ui::accessibility::view::Registry {
 public:
  MockSceneOwner(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // |LocalComponent|
  void Start(std::unique_ptr<LocalComponentHandles> local_handles) override {
    FX_CHECK(local_handles->outgoing()->AddPublicService(
                 fidl::InterfaceRequestHandler<fuchsia::ui::accessibility::view::Registry>(
                     [this](auto request) {
                       bindings_.AddBinding(this, std::move(request), dispatcher_);
                     })) == ZX_OK);
    local_handles_ = std::move(local_handles);
  }

  // |fuchsia::ui::accessibility::view::Registry|
  void CreateAccessibilityViewHolder(fuchsia::ui::views::ViewRef a11y_view_ref,
                                     fuchsia::ui::views::ViewHolderToken a11y_view_holder_token,
                                     CreateAccessibilityViewHolderCallback callback) override {
    scenic_ = local_handles_->svc().Connect<fuchsia::ui::scenic::Scenic>();

    // Set up scene root.
    fuchsia::ui::scenic::SessionEndpoints endpoints;
    root_session_ = std::make_unique<RootSession>(scenic_.get(), std::move(endpoints));
    root_session_->session.set_error_handler([](zx_status_t status) {
      FX_LOGS(FATAL) << "Scenic connection closed: " << zx_status_get_string(status);
    });

    // Attach a11y view holder.
    root_session_->a11y_view_holder = std::make_unique<scenic::ViewHolder>(
        &root_session_->session, std::move(a11y_view_holder_token), "a11y-view-holder");
    root_session_->scene.AddChild(*root_session_->a11y_view_holder);

    // Create the proxy view.
    auto [proxy_view_token, proxy_view_holder_token] = scenic::ViewTokenPair::New();
    auto [control_ref, view_ref] = scenic::ViewRefPair::New();
    root_session_->proxy_view =
        std::make_unique<scenic::View>(&root_session_->session, std::move(proxy_view_token),
                                       std::move(control_ref), std::move(view_ref), "proxy-view");

    // Listen for ViewAttachedToScene event on proxy view.
    root_session_->session.set_event_handler(
        [this](const std::vector<fuchsia::ui::scenic::Event>& events) {
          for (const auto& event : events) {
            if (!event.is_gfx())
              continue;  // skip non-gfx events

            if (event.gfx().is_view_attached_to_scene()) {
              FX_LOGS(INFO) << "Proxy view attached to scene";
              proxy_view_attached_ = true;
            }
          }
        });

    // Return the proxy view holder token to the a11y manager.
    callback(std::move(proxy_view_holder_token));

    // Present changes.
    root_session_->session.Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0,
                                    [](auto) {});
  }

  bool proxy_view_attached() const { return proxy_view_attached_; }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::unique_ptr<LocalComponentHandles> local_handles_;
  fidl::BindingSet<fuchsia::ui::accessibility::view::Registry> bindings_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
  std::unique_ptr<RootSession> root_session_;
  bool proxy_view_attached_ = false;
};

// This test fixture sets up a test realm with scenic, a11y manager, and a mock
// scene owner. The mock scene owner directly owns the root of the scene, and
// serves fuchsia::ui::accessibility::view::Registry.
//
// When a11y manager attempts to create its view, the mock scene owner will
// create the scene root and a proxy view, which is the child of the a11y view.
// The final state of the scene should be:
//
//      scene root (owned by mock scene owner)
//            |
//      a11y view holder (owned by mock scene owner)
//            |
//        a11y view (owned by a11y manager)
//            |
//      proxy view hodler (owned by a11y manager)
//            |
//       proxy view (owned by scene owner)
//
// The scene owner can observe signals on the a11y view holder and proxy view to
// verify the state of the a11y view and proxy view holder (owned by the a11y
// manager). In order for the proxy view to be attached to the scene, the a11y
// manger and mock scene owner must successfully complete the handshake to
// insert the a11y view.
class GfxAccessibilityViewTest : public gtest::RealLoopFixture {
 public:
  GfxAccessibilityViewTest() = default;
  ~GfxAccessibilityViewTest() override = default;

  void SetUp() override {
    // Don't specify a scene_owner to force a scenic-only realm.
    ui_testing::UITestRealm::Config config;
    config.ui_to_client_services = {fuchsia::ui::scenic::Scenic::Name_};
    // Expose the semantics manager service out of the realm. The test fixture
    // will connect to this service to force the a11y manager to start.
    config.exposed_client_services = {fuchsia::accessibility::semantics::SemanticsManager::Name_};
    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(std::move(config));

    FX_LOGS(INFO) << "Building realm";
    realm_ = std::make_unique<Realm>(ui_test_manager_->AddSubrealm());

    // Add real a11y manager.
    realm_->AddChild(kA11yManager, kA11yManagerUrl);

    // Add mock scene owner.
    mock_scene_owner_ = std::make_unique<MockSceneOwner>(dispatcher());
    realm_->AddLocalChild(kMockSceneOwner, mock_scene_owner_.get());

    // Route tracing provider to a11y manager.
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_},
                                            Protocol{fuchsia::logger::LogSink::Name_}},
                           .source = ParentRef(),
                           .targets = {ChildRef{kA11yManager}}});

    // Route scenic to both.
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                           .source = ParentRef(),
                           .targets = {ChildRef{kA11yManager}, ChildRef{kMockSceneOwner}}});

    // Route accessibility view registry from scene owner to a11y manager.
    realm_->AddRoute(
        Route{.capabilities = {Protocol{fuchsia::ui::accessibility::view::Registry::Name_}},
              .source = ChildRef{kMockSceneOwner},
              .targets = {ChildRef{kA11yManager}}});

    // Expose semantics manager service.
    realm_->AddRoute(Route{
        .capabilities = {Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_}},
        .source = ChildRef{kA11yManager},
        .targets = {ParentRef()}});

    ui_test_manager_->BuildRealm();
    realm_exposed_services_ = ui_test_manager_->CloneExposedServicesDirectory();
  }

  sys::ServiceDirectory* realm_exposed_services() { return realm_exposed_services_.get(); }

  MockSceneOwner* mock_scene_owner() { return mock_scene_owner_.get(); }

 private:
  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;
  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
  std::unique_ptr<Realm> realm_;

  std::unique_ptr<MockSceneOwner> mock_scene_owner_;
};

TEST_F(GfxAccessibilityViewTest, TestSceneConnected) {
  ASSERT_FALSE(mock_scene_owner()->proxy_view_attached());

  // Connect to an a11y service to force the a11y manager to start.
  auto semantics_manager =
      realm_exposed_services()->Connect<fuchsia::accessibility::semantics::SemanticsManager>();

  // The a11y manager will attempt to create its view during startup.
  // In order for the proxy view to receive a "view attached to scene" event,
  // there must be a fully connected path from the root of the scene to the
  // proxy view. This state can only be achieved if the a11y manager has
  // correctly inserted its view.
  RunLoopUntil([this]() { return mock_scene_owner()->proxy_view_attached(); });
}

}  // namespace
}  // namespace accessibility_test
