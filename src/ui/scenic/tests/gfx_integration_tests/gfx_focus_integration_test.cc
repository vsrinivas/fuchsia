// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/testing/cpp/real_loop.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <vector>

#include <zxtest/zxtest.h>

#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"
#include "src/ui/scenic/tests/utils/utils.h"

#define EXPECT_VIEW_REF_MATCH(view_ref1, view_ref2) \
  EXPECT_EQ(ExtractKoid(view_ref1), ExtractKoid(view_ref2))

// This test exercises the focus protocols implemented by Scenic (fuchsia.ui.focus.FocusChain,
// fuchsia.ui.views.Focuser, fuchsia.ui.views.ViewRefFocused) in the context of the GFX compositor
// interface.  The geometry is not important in this test, so we use the following three-node
// tree topology (note that a root view is not necessary in gfx, the scene node acts as the view for
// the root session for focus-related policy):
//   scene
//     |
//  parent
//     |
//   child
namespace integration_tests {

using RealmRoot = component_testing::RealmRoot;

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
class GfxFocusIntegrationTest : public zxtest::Test,
                                public loop_fixture::RealLoop,
                                public FocusChainListener {
 protected:
  GfxFocusIntegrationTest() : focus_chain_listener_(this) {}

  fuchsia::ui::scenic::Scenic* scenic() { return scenic_.get(); }

  void SetUp() override {
    // Build the realm topology and route the protocols required by this test fixture from the
    // scenic subrealm.
    realm_ = std::make_unique<RealmRoot>(
        ScenicRealmBuilder({.use_flatland = false})
            .AddRealmProtocol(fuchsia::ui::scenic::Scenic::Name_)
            .AddRealmProtocol(fuchsia::ui::focus::FocusChainListenerRegistry::Name_)
            .Build());

    scenic_ = realm_->Connect<fuchsia::ui::scenic::Scenic>();
    scenic_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });

    focus_chain_listener_registry_ =
        realm_->Connect<fuchsia::ui::focus::FocusChainListenerRegistry>();
    focus_chain_listener_registry_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to FocusChainListener: %s", zx_status_get_string(status));
    });

    // Set up focus chain listener and wait for the initial null focus chain.
    fidl::InterfaceHandle<FocusChainListener> listener_handle;
    focus_chain_listener_.Bind(listener_handle.NewRequest());
    focus_chain_listener_registry_->Register(std::move(listener_handle));
    EXPECT_EQ(CountReceivedFocusChains(), 0u);
    RunLoopUntil([this] { return CountReceivedFocusChains() == 1u; });
    EXPECT_FALSE(LastFocusChain()->has_focus_chain());

    // Set up root.
    fuchsia::ui::scenic::SessionEndpoints endpoints;
    endpoints.set_view_focuser(root_focuser_.NewRequest());
    endpoints.set_view_ref_focused(root_focused_.NewRequest());
    root_session_ = std::make_unique<RootSession>(scenic(), std::move(endpoints));
    root_session_->session.set_error_handler([](zx_status_t status) {
      FAIL("Root session terminated: %s", zx_status_get_string(status));
    });
    BlockingPresent(root_session_->session);

    // Now that the scene exists, wait for a valid focus chain.  It should only contain the scene
    // node.
    RunLoopUntil([this] { return CountReceivedFocusChains() == 2u; });
    EXPECT_TRUE(LastFocusChain()->has_focus_chain());
    EXPECT_EQ(LastFocusChain()->focus_chain().size(), 1u);

    // And the root's ViewRefFocused Watch call should fire, since it is now focused.
    bool root_focused = false;
    root_focused_->Watch([&root_focused](auto update) {
      ASSERT_TRUE(update.has_focused());
      root_focused = update.focused();
    });
    RunLoopUntil([&root_focused] { return root_focused; });

    // Make the tests less confusing by starting count at 0.
    observed_focus_chains_.clear();
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

  void SetAutoFocus(fuchsia::ui::views::FocuserPtr& view_focuser_ptr, const ViewRef& target) {
    bool request_processed = false;
    fuchsia::ui::views::FocuserSetAutoFocusRequest request{};
    request.set_view_ref(fidl::Clone(target));
    view_focuser_ptr->SetAutoFocus(std::move(request), [&request_processed](auto result) {
      request_processed = true;
      if (result.is_err()) {
        FAIL();
      }
    });
    RunLoopUntil([&request_processed] { return request_processed; });
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

  fuchsia::ui::scenic::ScenicPtr scenic_;
  std::unique_ptr<RealmRoot> realm_;
};

TEST_F(GfxFocusIntegrationTest, RequestValidity_RequestUnconnected_ShouldFail) {
  EXPECT_EQ(CountReceivedFocusChains(), 0u);

  // Create the parent View.
  scenic::Session parent_session = CreateSession(scenic(), {});
  auto [parent_view_token, parent_view_holder_token] = scenic::ViewTokenPair::New();
  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  ViewRef target;
  fidl::Clone(view_ref, &target);
  scenic::View view(&parent_session, std::move(parent_view_token), std::move(control_ref),
                    std::move(view_ref), "parent_view");
  BlockingPresent(parent_session);

  // Not connected yet, so focus change requests should fail.
  EXPECT_FALSE(RequestFocusChange(root_focuser_, target));
  RunLoopWithTimeout(kWaitTime);
  EXPECT_EQ(CountReceivedFocusChains(), 0u);
}

TEST_F(GfxFocusIntegrationTest, RequestValidity_RequestorConnected_SelfRequest_ShouldSucceed) {
  // Create the parent View and attach it to the scene.
  fuchsia::ui::scenic::SessionEndpoints endpoints;
  fuchsia::ui::views::FocuserPtr parent_focuser;
  endpoints.set_view_focuser(parent_focuser.NewRequest());
  scenic::Session parent_session = CreateSession(scenic(), std::move(endpoints));
  auto [parent_view_token, parent_view_holder_token] = scenic::ViewTokenPair::New();
  auto [control_ref, parent_view_ref] = scenic::ViewRefPair::New();
  ViewRef parent_view_ref_copy;
  fidl::Clone(parent_view_ref, &parent_view_ref_copy);
  scenic::View view(&parent_session, std::move(parent_view_token), std::move(control_ref),
                    std::move(parent_view_ref_copy), "parent_view");
  BlockingPresent(parent_session);
  AttachToScene(std::move(parent_view_holder_token));

  EXPECT_EQ(CountReceivedFocusChains(), 0u);
  // First move focus from the scene to the parent_view, then from parent_view to parent_view.
  // Both requests should succeed.
  ASSERT_TRUE(RequestFocusChange(root_focuser_, parent_view_ref));
  ASSERT_TRUE(RequestFocusChange(parent_focuser, parent_view_ref));
  // Should only receive one focus chain, since it didn't change from the second request.
  RunLoopUntil([this] { return CountReceivedFocusChains() == 1; });
  RunLoopWithTimeout(kWaitTime);
  EXPECT_EQ(CountReceivedFocusChains(), 1u);
  // Should contain scene node + parent_view.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[1], parent_view_ref);
}

TEST_F(GfxFocusIntegrationTest, RequestValidity_RequestorConnected_ChildRequest_ShouldSucceed) {
  EXPECT_EQ(CountReceivedFocusChains(), 0u);

  // Create the parent View.
  scenic::Session parent_session = CreateSession(scenic(), {});
  auto [parent_view_token, parent_view_holder_token] = scenic::ViewTokenPair::New();
  auto [parent_control_ref, parent_view_ref] = scenic::ViewRefPair::New();
  ViewRef parent_view_ref_copy;
  fidl::Clone(parent_view_ref, &parent_view_ref_copy);
  scenic::View parent_view(&parent_session, std::move(parent_view_token),
                           std::move(parent_control_ref), std::move(parent_view_ref_copy),
                           "parent_view");

  // Create the child view and connect it to the parent.
  scenic::Session child_session = CreateSession(scenic(), {});
  auto [child_view_token, child_view_holder_token] = scenic::ViewTokenPair::New();
  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  ViewRef child_view_ref_copy;
  fidl::Clone(child_view_ref, &child_view_ref_copy);
  scenic::View child_view(&child_session, std::move(child_view_token), std::move(child_control_ref),
                          std::move(child_view_ref_copy), "child_view");

  scenic::ViewHolder child_view_holder(&parent_session, std::move(child_view_holder_token),
                                       "child_holder");
  parent_view.AddChild(child_view_holder);
  AttachToScene(std::move(parent_view_holder_token));
  BlockingPresent(child_session);
  BlockingPresent(parent_session);
  EXPECT_EQ(CountReceivedFocusChains(), 0u);

  // Try to move focus to child. Should succeed.
  ASSERT_TRUE(RequestFocusChange(root_focuser_, child_view_ref));
  RunLoopUntil([this] { return CountReceivedFocusChains() == 1u; });  // Succeeds or times out.
  // Should contain scene node + parent_view + child_view.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 3u);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[1], parent_view_ref);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[2], child_view_ref);
}

// Sets up the following scene:
//   Root
//    |
//  Parent
//    |
//  Child (unfocusable)
// And then sets AutoFocus from Root to Child and observes focus going to Parent.
// (Focus starts at Root, tries to go to Child but it's unfocusable so reverts to its first
// focusable ancestor; Parent).
TEST_F(GfxFocusIntegrationTest, AutoFocus_RequestFocus_Focusable_Interaction) {
  // Create the parent View.
  scenic::Session parent_session = CreateSession(scenic(), {});
  auto [parent_view_token, parent_view_holder_token] = scenic::ViewTokenPair::New();
  auto [parent_control_ref, parent_view_ref] = scenic::ViewRefPair::New();
  ViewRef parent_view_ref_copy = fidl::Clone(parent_view_ref);
  scenic::View parent_view(&parent_session, std::move(parent_view_token),
                           std::move(parent_control_ref), std::move(parent_view_ref_copy),
                           "parent_view");

  // Create the child view and connect it to the parent. Make it unfocusable.
  scenic::Session child_session = CreateSession(scenic(), {});
  auto [child_view_token, child_view_holder_token] = scenic::ViewTokenPair::New();
  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  ViewRef child_view_ref_copy = fidl::Clone(child_view_ref);
  scenic::View child_view(&child_session, std::move(child_view_token), std::move(child_control_ref),
                          std::move(child_view_ref_copy), "child_view");

  scenic::ViewHolder child_view_holder(&parent_session, std::move(child_view_holder_token),
                                       "child_holder");
  parent_view.AddChild(child_view_holder);
  child_view_holder.SetViewProperties({.focus_change = false});
  AttachToScene(std::move(parent_view_holder_token));
  BlockingPresent(child_session);
  BlockingPresent(parent_session);
  EXPECT_EQ(CountReceivedFocusChains(), 0u);

  // Set auto focus to child view
  SetAutoFocus(root_focuser_, child_view_ref);
  RunLoopUntil([this] { return CountReceivedFocusChains() == 1u; });  // Succeeds or times out.
  // Should contain scene node + parent_view + child_view.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain().back(), parent_view_ref);
}

// Scene:
//   root         root         root
//          ->     |      ->
//   child       child        child
//
// 1. Set root's auto focus target to child.
// 2. Connect child to scene. Observe focus moving to child.
// 3. Disconnect child from scene. Observe focus return to root.
TEST_F(GfxFocusIntegrationTest, AutoFocus_SceneUpdate_Interaction) {
  // Create the parent View.
  scenic::Session child_session = CreateSession(scenic(), {});
  auto [child_view_token, child_view_holder_token] = scenic::ViewTokenPair::New();
  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  ViewRef child_view_ref_copy = fidl::Clone(child_view_ref);
  scenic::View child_view(&child_session, std::move(child_view_token), std::move(child_control_ref),
                          std::move(child_view_ref_copy), "child_view");
  BlockingPresent(child_session);

  SetAutoFocus(root_focuser_, child_view_ref);

  // Nothing should happen.
  RunLoopWithTimeout(zx::msec(1));
  EXPECT_EQ(CountReceivedFocusChains(), 0u);

  // Attach the child to the scene -> focus goes to child.
  AttachToScene(std::move(child_view_holder_token));
  RunLoopUntil([this] { return CountReceivedFocusChains() == 1u; });  // Succeeds or times out.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain().back(), child_view_ref);

  // Detach the child -> focus goes to root.
  root_session_->scene.DetachChildren();
  BlockingPresent(root_session_->session);
  RunLoopUntil([this] { return CountReceivedFocusChains() == 2u; });  // Succeeds or times out.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 1u);
}

TEST_F(GfxFocusIntegrationTest, FocusChain_Updated_OnViewDisconnect) {
  EXPECT_EQ(CountReceivedFocusChains(), 0u);

  // Create the parent View.
  scenic::Session parent_session = CreateSession(scenic(), {});
  auto [parent_view_token, parent_view_holder_token] = scenic::ViewTokenPair::New();
  auto [parent_control_ref, parent_view_ref] = scenic::ViewRefPair::New();
  ViewRef parent_view_ref_copy;
  fidl::Clone(parent_view_ref, &parent_view_ref_copy);
  scenic::View parent_view(&parent_session, std::move(parent_view_token),
                           std::move(parent_control_ref), std::move(parent_view_ref_copy),
                           "parent_view");

  // Create the child view and connect it to the parent.
  scenic::Session child_session = CreateSession(scenic(), {});
  auto [child_view_token, child_view_holder_token] = scenic::ViewTokenPair::New();
  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  ViewRef child_view_ref_copy;
  fidl::Clone(child_view_ref, &child_view_ref_copy);
  scenic::View child_view(&child_session, std::move(child_view_token), std::move(child_control_ref),
                          std::move(child_view_ref_copy), "child_view");
  scenic::ViewHolder child_view_holder(&parent_session, std::move(child_view_holder_token),
                                       "child_holder");
  parent_view.AddChild(child_view_holder);
  BlockingPresent(child_session);
  BlockingPresent(parent_session);
  AttachToScene(std::move(parent_view_holder_token));

  // Try to move focus to child. Should succeed.
  ASSERT_TRUE(RequestFocusChange(root_focuser_, child_view_ref));
  RunLoopUntil([this] { return CountReceivedFocusChains() == 1u; });  // Succeeds or times out.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 3u);

  // Disconnect the child and watch the focus chain update.
  parent_view.DetachChild(child_view_holder);
  BlockingPresent(parent_session);
  RunLoopUntil([this] { return CountReceivedFocusChains() == 2u; });  // Succeeds or times out.
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[1], parent_view_ref);
}

TEST_F(GfxFocusIntegrationTest, ViewFocuserDisconnectDoesNotKillSession) {
  root_session_->session.set_error_handler(
      [](zx_status_t) { FAIL("Client shut down unexpectedly."); });

  root_focuser_.Unbind();

  // Observe that the channel doesn't close after a blocking present.
  BlockingPresent(root_session_->session);
}

TEST_F(GfxFocusIntegrationTest, ViewRefFocused_HappyCase) {
  // Create the parent View.
  fuchsia::ui::scenic::SessionEndpoints endpoints;
  fuchsia::ui::views::FocuserPtr parent_focuser;
  fuchsia::ui::views::ViewRefFocusedPtr parent_focused_ptr;
  endpoints.set_view_focuser(parent_focuser.NewRequest());
  endpoints.set_view_ref_focused(parent_focused_ptr.NewRequest());
  scenic::Session parent_session = CreateSession(scenic(), std::move(endpoints));
  auto [parent_view_token, parent_view_holder_token] = scenic::ViewTokenPair::New();
  auto [parent_control_ref, parent_view_ref] = scenic::ViewRefPair::New();
  ViewRef parent_view_ref_copy;
  fidl::Clone(parent_view_ref, &parent_view_ref_copy);
  scenic::View parent_view(&parent_session, std::move(parent_view_token),
                           std::move(parent_control_ref), std::move(parent_view_ref_copy),
                           "parent_view");
  AttachToScene(std::move(parent_view_holder_token));
  BlockingPresent(parent_session);

  bool parent_focused = false;
  parent_focused_ptr->Watch([&parent_focused](auto update) {
    ASSERT_TRUE(update.has_focused());
    parent_focused = update.focused();
  });

  ASSERT_TRUE(RequestFocusChange(root_focuser_, parent_view_ref));

  RunLoopUntil([&parent_focused] { return parent_focused; });
}

// Scene:
//   root
//     |
//   parent
//     |
//   child
//
// 1. Set auto focus from parent to child.
// 2. Move focus to parent.
// 3. Observe focus moving directly to child.
TEST_F(GfxFocusIntegrationTest, AutoFocus_RequestFocus_Interaction) {
  EXPECT_EQ(CountReceivedFocusChains(), 0u);

  // Create the parent View.
  fuchsia::ui::scenic::SessionEndpoints endpoints;
  fuchsia::ui::views::FocuserPtr parent_focuser;
  endpoints.set_view_focuser(parent_focuser.NewRequest());
  scenic::Session parent_session = CreateSession(scenic(), std::move(endpoints));
  auto [parent_view_token, parent_view_holder_token] = scenic::ViewTokenPair::New();
  auto [parent_control_ref, parent_view_ref] = scenic::ViewRefPair::New();
  ViewRef parent_view_ref_copy;
  fidl::Clone(parent_view_ref, &parent_view_ref_copy);
  scenic::View parent_view(&parent_session, std::move(parent_view_token),
                           std::move(parent_control_ref), std::move(parent_view_ref_copy),
                           "parent_view");

  // Create the child view and connect it to the parent.
  scenic::Session child_session = CreateSession(scenic(), {});
  auto [child_view_token, child_view_holder_token] = scenic::ViewTokenPair::New();
  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  ViewRef child_view_ref_copy;
  fidl::Clone(child_view_ref, &child_view_ref_copy);
  scenic::View child_view(&child_session, std::move(child_view_token), std::move(child_control_ref),
                          std::move(child_view_ref_copy), "child_view");
  scenic::ViewHolder child_view_holder(&parent_session, std::move(child_view_holder_token),
                                       "child_holder");
  parent_view.AddChild(child_view_holder);
  BlockingPresent(child_session);
  BlockingPresent(parent_session);
  AttachToScene(std::move(parent_view_holder_token));

  SetAutoFocus(parent_focuser, child_view_ref);

  ASSERT_TRUE(RequestFocusChange(root_focuser_, parent_view_ref));
  ASSERT_TRUE(RequestFocusChange(parent_focuser, parent_view_ref));
  RunLoopUntil([this] { return CountReceivedFocusChains() == 1; });

  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 3u);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[1], parent_view_ref);
  EXPECT_VIEW_REF_MATCH(LastFocusChain()->focus_chain()[2], child_view_ref);
}

TEST_F(GfxFocusIntegrationTest, ChildView_CreatedBeforeAttachingToRoot_ShouldNotKillFocuser) {
  EXPECT_EQ(CountReceivedFocusChains(), 0u);

  // Create the parent View.
  fuchsia::ui::scenic::SessionEndpoints endpoints;
  fuchsia::ui::views::FocuserPtr parent_focuser;
  endpoints.set_view_focuser(parent_focuser.NewRequest());
  scenic::Session parent_session = CreateSession(scenic(), std::move(endpoints));
  auto [parent_view_token, parent_view_holder_token] = scenic::ViewTokenPair::New();
  auto [parent_control_ref, parent_view_ref] = scenic::ViewRefPair::New();
  ViewRef parent_view_ref_copy;
  fidl::Clone(parent_view_ref, &parent_view_ref_copy);
  scenic::View parent_view(&parent_session, std::move(parent_view_token),
                           std::move(parent_control_ref), std::move(parent_view_ref_copy),
                           "parent_view");

  // Create the child view and connect it to the parent.
  fuchsia::ui::views::FocuserPtr child_focuser;
  bool channel_alive = true;
  child_focuser.set_error_handler([&channel_alive](auto) { channel_alive = false; });
  fuchsia::ui::scenic::SessionEndpoints child_endpoints;
  child_endpoints.set_view_focuser(child_focuser.NewRequest());
  scenic::Session child_session = CreateSession(scenic(), std::move(child_endpoints));

  auto [child_view_token, child_view_holder_token] = scenic::ViewTokenPair::New();
  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  ViewRef child_view_ref_copy;
  fidl::Clone(child_view_ref, &child_view_ref_copy);
  scenic::View child_view(&child_session, std::move(child_view_token), std::move(child_control_ref),
                          std::move(child_view_ref_copy), "child_view");
  scenic::ViewHolder child_view_holder(&parent_session, std::move(child_view_holder_token),
                                       "child_holder");
  BlockingPresent(child_session);
  parent_view.AddChild(child_view_holder);
  BlockingPresent(parent_session);
  AttachToScene(std::move(parent_view_holder_token));

  // The child_focuser should not die.
  RunLoopUntilIdle();
  EXPECT_TRUE(channel_alive);
}

TEST_F(GfxFocusIntegrationTest, ViewRefFocusedDisconnectedWhenSessionDies) {
  EXPECT_TRUE(root_focused_);
  root_session_.reset();
  RunLoopUntil([this] { return !root_focused_; });  // Succeeds or times out.
  EXPECT_FALSE(root_focused_);
}

TEST_F(GfxFocusIntegrationTest, ViewRefFocusedDisconnectDoesNotKillSession) {
  root_session_->session.set_error_handler(
      [](zx_status_t) { FAIL("Client shut down unexpectedly."); });

  root_focused_.Unbind();

  // Wait "long enough" and observe that the session channel doesn't close.
  RunLoopWithTimeout(kWaitTime);
}

}  // namespace integration_tests
