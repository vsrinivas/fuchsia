// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/lifecycle/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <vector>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/utils/helpers.h"

#define EXPECT_VIEW_REF_MATCH(view_ref1, view_ref2) \
  EXPECT_EQ(utils::ExtractKoid(view_ref1), utils::ExtractKoid(view_ref2))

// This test exercises the focus protocols implemented by Scenic (fuchsia.ui.focus.FocusChain,
// fuchsia.ui.views.Focuser, fuchsia.ui.views.ViewRefFocused) in the context of the GFX compositor
// interface.  The geometry is not important in this test, so we use the following two-node (plus a
// scene node) tree topology:
//   (scene)
//      |
//    parent
//      |
//    child
namespace integration_tests {

const std::map<std::string, std::string> LocalServices() {
  return {{"fuchsia.ui.composition.Allocator",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.scenic.Scenic",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.focus.FocusChainListenerRegistry",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
          {"fuchsia.ui.lifecycle.LifecycleController",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.hardware.display.Provider",
           "fuchsia-pkg://fuchsia.com/fake-hardware-display-controller-provider#meta/hdcp.cmx"}};
}

// Allow these global services from outside the test environment.
const std::vector<std::string> GlobalServices() {
  return {"fuchsia.vulkan.loader.Loader", "fuchsia.sysmem.Allocator"};
}

// "Long enough" time to wait before assuming focus chain updates won't arrive.
// Should not be used when actually expecting an update to occur.
const zx::duration kWaitTime = zx::msec(2);

using fuchsia::ui::focus::FocusChain;
using fuchsia::ui::focus::FocusChainListener;
using fuchsia::ui::views::ViewRef;

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

  std::unique_ptr<scenic::ViewHolder> view_holder;
};

// Test fixture that sets up an environment with a Scenic we can connect to.
class GfxFocusIntegrationTest : public gtest::TestWithEnvironmentFixture,
                                public FocusChainListener {
 protected:
  GfxFocusIntegrationTest() : focus_chain_listener_(this) {}

  fuchsia::ui::scenic::Scenic* scenic() { return scenic_.get(); }

  void SetUp() override {
    TestWithEnvironmentFixture::SetUp();

    environment_ =
        CreateNewEnclosingEnvironment("gfx_focus_integration_test_environment", CreateServices());
    WaitForEnclosingEnvToStart(environment_.get());

    // Connects to scenic lifecycle controller in order to shutdown scenic at the end of the test.
    // This ensures the correct ordering of shutdown under CFv1: first scenic, then the fake display
    // controller.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    environment_->ConnectToService<fuchsia::ui::lifecycle::LifecycleController>(
        scenic_lifecycle_controller_.NewRequest());

    environment_->ConnectToService(scenic_.NewRequest());
    scenic_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });
    environment_->ConnectToService(focus_chain_listener_registry_.NewRequest());
    focus_chain_listener_registry_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to FocusChainListener: " << zx_status_get_string(status);
    });

    // Set up focus chain listener and wait for the initial null focus chain.
    fidl::InterfaceHandle<FocusChainListener> listener_handle;
    focus_chain_listener_.Bind(listener_handle.NewRequest());
    focus_chain_listener_registry_->Register(std::move(listener_handle));
    EXPECT_EQ(CountReceivedFocusChains(), 0u);
    RunLoopUntil([this] { return CountReceivedFocusChains() == 1u; });
    EXPECT_FALSE(LastFocusChain()->has_focus_chain());

    // Set up root view.
    fuchsia::ui::scenic::SessionEndpoints endpoints;
    endpoints.set_view_focuser(root_focuser_.NewRequest());
    endpoints.set_view_ref_focused(root_focused_.NewRequest());
    root_session_ = std::make_unique<RootSession>(scenic(), std::move(endpoints));
    root_session_->session.set_error_handler([](zx_status_t status) {
      FAIL() << "Root session terminated: " << zx_status_get_string(status);
    });
    BlockingPresent(root_session_->session);

    // Now that the scene exists, wait for a valid focus chain.  It should only contain the scene
    // node.
    RunLoopUntil([this] { return CountReceivedFocusChains() == 2u; });
    EXPECT_TRUE(LastFocusChain()->has_focus_chain());
    EXPECT_EQ(LastFocusChain()->focus_chain().size(), 1u);

    // Make the tests less confusing by starting count at 0.
    observed_focus_chains_.clear();
  }

  void TearDown() override {
    // Avoid spurious errors since we are about to kill scenic.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    focus_chain_listener_registry_.set_error_handler(nullptr);
    scenic_.set_error_handler(nullptr);

    zx_status_t terminate_status = scenic_lifecycle_controller_->Terminate();
    FX_CHECK(terminate_status == ZX_OK)
        << "Failed to terminate Scenic with status: " << zx_status_get_string(terminate_status);
  }

  void BlockingPresent(scenic::Session& session) {
    bool presented = false;
    session.set_on_frame_presented_handler([&presented](auto) { presented = true; });
    session.Present2(0, 0, [](auto) {});
    RunLoopUntil([&presented] { return presented; });
    session.set_on_frame_presented_handler([](auto) {});
  }

  void AttachToScene(fuchsia::ui::views::ViewHolderToken token) {
    root_session_->view_holder =
        std::make_unique<scenic::ViewHolder>(&root_session_->session, std::move(token), "holder");
    root_session_->scene.AddChild(*root_session_->view_holder);
    BlockingPresent(root_session_->session);
  }

  // Configures services available to the test environment. This method is called by |SetUp()|. It
  // shadows but calls |TestWithEnvironmentFixture::CreateServices()|.
  std::unique_ptr<sys::testing::EnvironmentServices> CreateServices() {
    auto services = TestWithEnvironmentFixture::CreateServices();
    for (const auto& [name, url] : LocalServices()) {
      const zx_status_t is_ok = services->AddServiceWithLaunchInfo({.url = url}, name);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << name;
    }

    for (const auto& service : GlobalServices()) {
      const zx_status_t is_ok = services->AllowParentService(service);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << service;
    }

    return services;
  }

  bool RequestFocusChange(fuchsia::ui::views::FocuserPtr& view_focuser_ptr, const ViewRef& target) {
    ViewRef clone;
    fidl::Clone(target, &clone);

    bool request_processed = false;
    bool request_honored = false;
    view_focuser_ptr->RequestFocus(std::move(clone),
                                   [&request_processed, &request_honored](auto result) {
                                     request_processed = true;
                                     if (!result.is_err()) {
                                       request_honored = true;
                                     }
                                   });
    RunLoopUntil([&request_processed] { return request_processed; });
    EXPECT_TRUE(request_processed);
    return request_honored;
  }

  // |fuchsia::ui::focus::FocusChainListener|
  void OnFocusChange(FocusChain focus_chain, OnFocusChangeCallback callback) override {
    observed_focus_chains_.push_back(std::move(focus_chain));
    callback();  // Receipt.
  }

  size_t CountReceivedFocusChains() const { return observed_focus_chains_.size(); }

  const FocusChain* LastFocusChain() const {
    if (observed_focus_chains_.empty()) {
      return nullptr;
    } else {
      // Can't do std::optional<const FocusChain&>.
      return &observed_focus_chains_.back();
    }
  }

  fuchsia::ui::views::FocuserPtr root_focuser_;
  fuchsia::ui::views::ViewRefFocusedPtr root_focused_;
  std::unique_ptr<RootSession> root_session_;

 private:
  fuchsia::ui::focus::FocusChainListenerRegistryPtr focus_chain_listener_registry_;
  fidl::Binding<FocusChainListener> focus_chain_listener_;

  std::vector<FocusChain> observed_focus_chains_;

  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  fuchsia::ui::lifecycle::LifecycleControllerSyncPtr scenic_lifecycle_controller_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
};

TEST_F(GfxFocusIntegrationTest, RequestValidity_RequestUnconnected_ShouldFail) {
  EXPECT_EQ(CountReceivedFocusChains(), 0u);

  // Create the root View.
  auto [root_view_token, root_view_holder_token] = scenic::ViewTokenPair::New();
  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  ViewRef target;
  fidl::Clone(view_ref, &target);
  scenic::View view(&root_session_->session, std::move(root_view_token), std::move(control_ref),
                    std::move(view_ref), "root_view");
  BlockingPresent(root_session_->session);

  // Not connected yet, so focus change requests should fail.
  EXPECT_FALSE(RequestFocusChange(root_focuser_, target));
  RunLoopWithTimeout(kWaitTime);
  EXPECT_EQ(CountReceivedFocusChains(), 0u);
}

TEST_F(GfxFocusIntegrationTest, RequestValidity_RequestorConnected_SelfRequest_ShouldSucceed) {
  // Create the root View and attach it to the scene.
  auto [root_view_token, root_view_holder_token] = scenic::ViewTokenPair::New();
  auto [control_ref, root_view_ref] = scenic::ViewRefPair::New();
  ViewRef root_view_ref_copy;
  fidl::Clone(root_view_ref, &root_view_ref_copy);
  scenic::View view(&root_session_->session, std::move(root_view_token), std::move(control_ref),
                    std::move(root_view_ref_copy), "root_view");
  BlockingPresent(root_session_->session);
  AttachToScene(std::move(root_view_holder_token));

  EXPECT_EQ(CountReceivedFocusChains(), 0u);
  // First move focus from the scene to the root view, then from root view to root view.
  // Both requests should succeed.
  ASSERT_TRUE(RequestFocusChange(root_focuser_, root_view_ref));
  ASSERT_TRUE(RequestFocusChange(root_focuser_, root_view_ref));
  // Should only receive one focus chain, since it didn't change from the second request.
  RunLoopUntil([this] { return CountReceivedFocusChains() == 1; });
  RunLoopWithTimeout(kWaitTime);
  EXPECT_EQ(CountReceivedFocusChains(), 1u);
  // Should contain scene node + root view.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[1], root_view_ref);
}

TEST_F(GfxFocusIntegrationTest, RequestValidity_RequestorConnected_ChildRequest_ShouldSucceed) {
  EXPECT_EQ(CountReceivedFocusChains(), 0u);

  // Create the root View.
  auto [root_view_token, root_view_holder_token] = scenic::ViewTokenPair::New();
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  ViewRef root_view_ref_copy;
  fidl::Clone(root_view_ref, &root_view_ref_copy);
  scenic::View root_view(&root_session_->session, std::move(root_view_token),
                         std::move(root_control_ref), std::move(root_view_ref_copy), "root_view");

  // Create the child view and connect it to the parent.
  scenic::Session child_session = CreateSession(scenic(), {});
  auto [child_view_token, child_view_holder_token] = scenic::ViewTokenPair::New();
  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  ViewRef child_view_ref_copy;
  fidl::Clone(child_view_ref, &child_view_ref_copy);
  scenic::View child_view(&child_session, std::move(child_view_token), std::move(child_control_ref),
                          std::move(child_view_ref_copy), "child_view");

  scenic::ViewHolder child_view_holder(&root_session_->session, std::move(child_view_holder_token),
                                       "child_holder");
  root_view.AddChild(child_view_holder);
  AttachToScene(std::move(root_view_holder_token));
  BlockingPresent(child_session);
  BlockingPresent(root_session_->session);
  EXPECT_EQ(CountReceivedFocusChains(), 0u);

  // Try to move focus to child. Should succeed.
  ASSERT_TRUE(RequestFocusChange(root_focuser_, child_view_ref));
  RunLoopUntil([this] { return CountReceivedFocusChains() == 1u; });  // Succeeds or times out.
  // Should contain scene node + root view + child_view.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 3u);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[1], root_view_ref);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[2], child_view_ref);
}

TEST_F(GfxFocusIntegrationTest, FocusChain_Updated_OnViewDisconnect) {
  EXPECT_EQ(CountReceivedFocusChains(), 0u);

  // Create the root View.
  auto [root_view_token, root_view_holder_token] = scenic::ViewTokenPair::New();
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  ViewRef root_view_ref_copy;
  fidl::Clone(root_view_ref, &root_view_ref_copy);
  scenic::View root_view(&root_session_->session, std::move(root_view_token),
                         std::move(root_control_ref), std::move(root_view_ref_copy), "root_view");

  // Create the child view and connect it to the parent.
  scenic::Session child_session = CreateSession(scenic(), {});
  auto [child_view_token, child_view_holder_token] = scenic::ViewTokenPair::New();
  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  ViewRef child_view_ref_copy;
  fidl::Clone(child_view_ref, &child_view_ref_copy);
  scenic::View child_view(&child_session, std::move(child_view_token), std::move(child_control_ref),
                          std::move(child_view_ref_copy), "child_view");
  scenic::ViewHolder child_view_holder(&root_session_->session, std::move(child_view_holder_token),
                                       "child_holder");
  root_view.AddChild(child_view_holder);
  BlockingPresent(child_session);
  AttachToScene(std::move(root_view_holder_token));

  // Try to move focus to child. Should succeed.
  ASSERT_TRUE(RequestFocusChange(root_focuser_, child_view_ref));
  RunLoopUntil([this] { return CountReceivedFocusChains() == 1u; });  // Succeeds or times out.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 3u);

  // Disconnect the child and watch the focus chain update.
  root_view.DetachChild(child_view_holder);
  BlockingPresent(root_session_->session);
  RunLoopUntil([this] { return CountReceivedFocusChains() == 2u; });  // Succeeds or times out.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[1], root_view_ref);
}

TEST_F(GfxFocusIntegrationTest, ViewFocuserDisconnectDoesNotKillSession) {
  root_session_->session.set_error_handler(
      [](zx_status_t) { FAIL() << "Client shut down unexpectedly."; });

  root_focuser_.Unbind();

  // Wait "long enough" and observe that the session channel doesn't close.
  RunLoopWithTimeout(kWaitTime);
}

TEST_F(GfxFocusIntegrationTest, ViewRefFocused_HappyCase) {
  std::optional<bool> root_focused;
  root_focused_->Watch([&root_focused](auto update) {
    ASSERT_TRUE(update.has_focused());
    root_focused = update.focused();
  });

  RunLoopUntilIdle();
  EXPECT_FALSE(root_focused.has_value());

  // Create the root View.
  auto [root_view_token, root_view_holder_token] = scenic::ViewTokenPair::New();
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  ViewRef root_view_ref_copy;
  fidl::Clone(root_view_ref, &root_view_ref_copy);
  scenic::View root_view(&root_session_->session, std::move(root_view_token),
                         std::move(root_control_ref), std::move(root_view_ref_copy), "root_view");
  AttachToScene(std::move(root_view_holder_token));
  BlockingPresent(root_session_->session);

  ASSERT_TRUE(RequestFocusChange(root_focuser_, root_view_ref));

  RunLoopUntil([&root_focused] { return root_focused.has_value(); });
  EXPECT_TRUE(root_focused.value());
  EXPECT_TRUE(root_focused_);
}

TEST_F(GfxFocusIntegrationTest, ViewRefFocusedDisconnectedWhenSessionDies) {
  EXPECT_TRUE(root_focused_);
  root_session_.reset();
  RunLoopUntil([this] { return !root_focused_; });  // Succeeds or times out.
  EXPECT_FALSE(root_focused_);
}

TEST_F(GfxFocusIntegrationTest, ViewRefFocusedDisconnectDoesNotKillSession) {
  root_session_->session.set_error_handler(
      [](zx_status_t) { FAIL() << "Client shut down unexpectedly."; });

  root_focused_.Unbind();

  // Wait "long enough" and observe that the session channel doesn't close.
  RunLoopWithTimeout(kWaitTime);
}

}  // namespace integration_tests
