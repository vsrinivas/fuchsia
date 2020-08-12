// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/focus/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/gfx/tests/gfx_test.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"
#include "src/ui/scenic/lib/scheduling/id.h"
#include "src/ui/scenic/lib/utils/helpers.h"

// This test exercises the focus transfer functionality of fuchsia.ui.views.Focuser. In particular,
// a request may be performed at various points along the resource lifecycle timeline of both
// requestor and requestee. We use the FocusChainListener as the introspection mechanism for
// checking whether a request has been honored or denied.
//
// Policy exercises are tested elsewhere.
//
// The geometry is not important in this test, so surface geometries will overlap on a 5 x 5 pixel
// layer.  We use the following two-node tree topology:
//    parent
//      |
//    child
namespace src_ui_scenic_lib_gfx_tests {

using fuchsia::ui::focus::FocusChain;
using fuchsia::ui::focus::FocusChainListener;
using fuchsia::ui::focus::FocusChainListenerRegistry;
using fuchsia::ui::focus::FocusChainListenerRegistryPtr;
using fuchsia::ui::views::ViewRef;
using scenic_impl::gfx::ViewTree;
using scenic_impl::gfx::test::SessionWrapper;
using ViewFocuserPtr = fuchsia::ui::views::FocuserPtr;
using ViewFocuserRequest = fidl::InterfaceRequest<fuchsia::ui::views::Focuser>;

// Class fixture for TEST_F.
class FocusTransferTest : public scenic_impl::gfx::test::GfxSystemTest, public FocusChainListener {
 protected:
  struct ParentClient : public SessionWrapper {
    ParentClient(scenic_impl::Scenic* scenic, ViewFocuserRequest view_focuser_request)
        : SessionWrapper(scenic, std::move(view_focuser_request)) {}
    std::unique_ptr<scenic::Compositor> compositor;
    std::unique_ptr<scenic::Renderer> renderer;
    std::unique_ptr<scenic::Scene> scene;  // Implicitly has the root ViewRef.
    std::unique_ptr<scenic::Camera> camera;
    std::unique_ptr<scenic::ViewHolder> holder_child;
  };

  struct ChildClient : public SessionWrapper {
    ChildClient(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::View> view;
  };

  FocusTransferTest() : focus_chain_listener_(this) {}
  ~FocusTransferTest() override = default;

  void SetUp() override {
    scenic_impl::gfx::test::GfxSystemTest::SetUp();

    context_provider().ConnectToPublicService<FocusChainListenerRegistry>(
        focus_chain_listener_registry_.NewRequest());

    fidl::InterfaceHandle<FocusChainListener> listener_handle;
    focus_chain_listener_.Bind(listener_handle.NewRequest());
    focus_chain_listener_registry_->Register(std::move(listener_handle));

    RunLoopUntilIdle();
  }

  void TearDown() override {
    focus_chain_listener_.Close(ZX_OK);
    focus_chain_listener_registry_.Unbind();
    GfxSystemTest::TearDown();
  }

  void RequestToPresent(scenic::Session* session) {
    session->Present(/*presentation time*/ 0, [](auto) {});
    RunLoopFor(zx::msec(20));  // "Good enough" deadline to ensure session update gets scheduled.
  }

  bool RequestFocusChange(ViewFocuserPtr* view_focuser_ptr, const ViewRef& target) {
    ViewRef clone;
    fidl::Clone(target, &clone);

    bool request_processed = false;
    bool request_honored = false;
    (*view_focuser_ptr)
        ->RequestFocus(std::move(clone), [&request_processed, &request_honored](auto result) {
          request_processed = true;
          if (!result.is_err()) {
            request_honored = true;
          }
        });
    RunLoopUntilIdle();
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

 private:
  FocusChainListenerRegistryPtr focus_chain_listener_registry_;
  fidl::Binding<FocusChainListener> focus_chain_listener_;

  std::vector<FocusChain> observed_focus_chains_;
};

TEST_F(FocusTransferTest, RequestValidity_NoRequestorNoRequest) {
  ViewFocuserPtr parent_focuser;
  ParentClient parent_client(scenic(), parent_focuser.NewRequest());

  auto child_refs = scenic::ViewRefPair::New();  // child view's view ref pair

  ViewRef target;
  fidl::Clone(child_refs.view_ref, &target);

  //
  // Action: Initial setup with no scene.
  // Expect, with focus change request: no focus change, no focus chain
  //
  parent_client.RunNow([test = this, state = &parent_client](scenic::Session* session,
                                                             scenic::EntityNode* session_anchor) {
    // Start setting up scene, but don't actually create the scene yet.
    state->compositor = std::make_unique<scenic::Compositor>(session);
    scenic::LayerStack layer_stack(session);
    state->compositor->SetLayerStack(layer_stack);

    scenic::Layer layer(session);
    layer.SetSize(5 /*px*/, 5 /*px*/);
    layer_stack.AddLayer(layer);
    state->renderer = std::make_unique<scenic::Renderer>(session);
    layer.SetRenderer(*state->renderer);

    test->RequestToPresent(session);
  });

  EXPECT_FALSE(RequestFocusChange(&parent_focuser, target));
}

TEST_F(FocusTransferTest, RequestValidity_RequestorCreatedNoRequest) {
  ViewFocuserPtr parent_focuser;
  ParentClient parent_client(scenic(), parent_focuser.NewRequest());

  auto child_refs = scenic::ViewRefPair::New();  // child view's view ref pair

  ViewRef target;
  fidl::Clone(child_refs.view_ref, &target);

  parent_client.RunNow([test = this, state = &parent_client](scenic::Session* session,
                                                             scenic::EntityNode* session_anchor) {
    state->compositor = std::make_unique<scenic::Compositor>(session);
    scenic::LayerStack layer_stack(session);
    state->compositor->SetLayerStack(layer_stack);

    scenic::Layer layer(session);
    layer.SetSize(5 /*px*/, 5 /*px*/);
    layer_stack.AddLayer(layer);
    state->renderer = std::make_unique<scenic::Renderer>(session);
    layer.SetRenderer(*state->renderer);

    // Create scene
    state->scene = std::make_unique<scenic::Scene>(session);

    test->RequestToPresent(session);
  });

  EXPECT_FALSE(RequestFocusChange(&parent_focuser, target));
}

TEST_F(FocusTransferTest, RequestValidity_RequestorConnectedNoRequest) {
  ViewFocuserPtr parent_focuser;
  ParentClient parent_client(scenic(), parent_focuser.NewRequest());

  auto child_refs = scenic::ViewRefPair::New();  // child view's view ref pair

  ViewRef target;
  fidl::Clone(child_refs.view_ref, &target);

  parent_client.RunNow([test = this, state = &parent_client](scenic::Session* session,
                                                             scenic::EntityNode* session_anchor) {
    state->compositor = std::make_unique<scenic::Compositor>(session);
    scenic::LayerStack layer_stack(session);
    state->compositor->SetLayerStack(layer_stack);

    scenic::Layer layer(session);
    layer.SetSize(5 /*px*/, 5 /*px*/);
    layer_stack.AddLayer(layer);
    state->renderer = std::make_unique<scenic::Renderer>(session);
    layer.SetRenderer(*state->renderer);

    // Create scene
    state->scene = std::make_unique<scenic::Scene>(session);

    // Connect scene
    scenic::Camera camera(*state->scene);
    state->renderer->SetCamera(camera);

    state->scene->AddChild(*session_anchor);

    test->RequestToPresent(session);
  });

  EXPECT_EQ(CountReceivedFocusChains(),
            2u);  // The initial on register focus chain + a lifecycle event tied to scene creation.

  EXPECT_FALSE(RequestFocusChange(&parent_focuser, target));
  EXPECT_EQ(CountReceivedFocusChains(), 2u);
}

TEST_F(FocusTransferTest, RequestValidity_RequestorConnectedRequestCreated) {
  ViewFocuserPtr parent_focuser;
  ParentClient parent_client(scenic(), parent_focuser.NewRequest());
  ChildClient child_client(scenic());

  auto token_pair = scenic::ViewTokenPair::New();  // parent-child view tokens
  auto child_refs = scenic::ViewRefPair::New();    // child view's view ref pair

  ViewRef target;
  fidl::Clone(child_refs.view_ref, &target);

  parent_client.RunNow([test = this, state = &parent_client](scenic::Session* session,
                                                             scenic::EntityNode* session_anchor) {
    state->compositor = std::make_unique<scenic::Compositor>(session);
    scenic::LayerStack layer_stack(session);
    state->compositor->SetLayerStack(layer_stack);

    scenic::Layer layer(session);
    layer.SetSize(5 /*px*/, 5 /*px*/);
    layer_stack.AddLayer(layer);
    state->renderer = std::make_unique<scenic::Renderer>(session);
    layer.SetRenderer(*state->renderer);

    // Create scene
    state->scene = std::make_unique<scenic::Scene>(session);

    // Connect scene
    scenic::Camera camera(*state->scene);
    state->renderer->SetCamera(camera);

    state->scene->AddChild(*session_anchor);

    test->RequestToPresent(session);
  });

  //
  // Action: Create child view, the target, but don't connect it to Scene via view holder.
  // Expect, with focus change request: no focus change, no focus chain.
  //
  child_client.RunNow(
      [test = this, state = &child_client, child_token = std::move(token_pair.view_token),
       control_ref = std::move(child_refs.control_ref), view_ref = std::move(child_refs.view_ref)](
          scenic::Session* session, scenic::EntityNode*) mutable {
        state->view =
            std::make_unique<scenic::View>(session, std::move(child_token), std::move(control_ref),
                                           std::move(view_ref), "child view");
        test->RequestToPresent(session);
      });

  EXPECT_FALSE(RequestFocusChange(&parent_focuser, target));
  EXPECT_EQ(CountReceivedFocusChains(), 2u);
}

TEST_F(FocusTransferTest, RequestValidity_RequestorConnectedRequestCreatedViewholderCreated) {
  ViewFocuserPtr parent_focuser;
  ParentClient parent_client(scenic(), parent_focuser.NewRequest());
  ChildClient child_client(scenic());

  auto token_pair = scenic::ViewTokenPair::New();  // parent-child view tokens
  auto child_refs = scenic::ViewRefPair::New();    // child view's view ref pair

  ViewRef target;
  fidl::Clone(child_refs.view_ref, &target);

  parent_client.RunNow([test = this, state = &parent_client](scenic::Session* session,
                                                             scenic::EntityNode* session_anchor) {
    state->compositor = std::make_unique<scenic::Compositor>(session);
    scenic::LayerStack layer_stack(session);
    state->compositor->SetLayerStack(layer_stack);

    scenic::Layer layer(session);
    layer.SetSize(5 /*px*/, 5 /*px*/);
    layer_stack.AddLayer(layer);
    state->renderer = std::make_unique<scenic::Renderer>(session);
    layer.SetRenderer(*state->renderer);

    // Create scene
    state->scene = std::make_unique<scenic::Scene>(session);

    // Connect scene
    scenic::Camera camera(*state->scene);
    state->renderer->SetCamera(camera);

    state->scene->AddChild(*session_anchor);

    test->RequestToPresent(session);
  });

  child_client.RunNow(
      [test = this, state = &child_client, child_token = std::move(token_pair.view_token),
       control_ref = std::move(child_refs.control_ref), view_ref = std::move(child_refs.view_ref)](
          scenic::Session* session, scenic::EntityNode*) mutable {
        state->view =
            std::make_unique<scenic::View>(session, std::move(child_token), std::move(control_ref),
                                           std::move(view_ref), "child view");
        test->RequestToPresent(session);
      });

  //
  // Action: Create view holder, but don't connect it to Scene.
  // Expect, with focus change request: no focus change, no focus chain.
  //
  parent_client.RunNow(
      [test = this, state = &parent_client, parent_token = std::move(token_pair.view_holder_token)](
          scenic::Session* session, scenic::EntityNode*) mutable {
        const std::array<float, 3> kZero = {0, 0, 0};
        state->holder_child =
            std::make_unique<scenic::ViewHolder>(session, std::move(parent_token), "child holder");
        state->holder_child->SetViewProperties(kZero, {5, 5, 1}, kZero, kZero);

        test->RequestToPresent(session);
      });

  EXPECT_FALSE(RequestFocusChange(&parent_focuser, target));
  EXPECT_EQ(CountReceivedFocusChains(), 2u);
}

TEST_F(FocusTransferTest, RequestValidity_RequestorConnectedRequestConnected) {
  ViewFocuserPtr parent_focuser;
  ParentClient parent_client(scenic(), parent_focuser.NewRequest());
  ChildClient child_client(scenic());

  auto token_pair = scenic::ViewTokenPair::New();  // parent-child view tokens
  auto child_refs = scenic::ViewRefPair::New();    // child view's view ref pair

  ViewRef target;
  fidl::Clone(child_refs.view_ref, &target);

  parent_client.RunNow([test = this, state = &parent_client](scenic::Session* session,
                                                             scenic::EntityNode* session_anchor) {
    state->compositor = std::make_unique<scenic::Compositor>(session);
    scenic::LayerStack layer_stack(session);
    state->compositor->SetLayerStack(layer_stack);

    scenic::Layer layer(session);
    layer.SetSize(5 /*px*/, 5 /*px*/);
    layer_stack.AddLayer(layer);
    state->renderer = std::make_unique<scenic::Renderer>(session);
    layer.SetRenderer(*state->renderer);

    // Created scene
    state->scene = std::make_unique<scenic::Scene>(session);

    // Connect scene
    scenic::Camera camera(*state->scene);
    state->renderer->SetCamera(camera);

    state->scene->AddChild(*session_anchor);

    test->RequestToPresent(session);
  });

  child_client.RunNow(
      [test = this, state = &child_client, child_token = std::move(token_pair.view_token),
       control_ref = std::move(child_refs.control_ref), view_ref = std::move(child_refs.view_ref)](
          scenic::Session* session, scenic::EntityNode*) mutable {
        state->view =
            std::make_unique<scenic::View>(session, std::move(child_token), std::move(control_ref),
                                           std::move(view_ref), "child view");
        test->RequestToPresent(session);
      });

  parent_client.RunNow(
      [test = this, state = &parent_client, parent_token = std::move(token_pair.view_holder_token)](
          scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
        const std::array<float, 3> kZero = {0, 0, 0};
        state->holder_child =
            std::make_unique<scenic::ViewHolder>(session, std::move(parent_token), "child holder");
        state->holder_child->SetViewProperties(kZero, {5, 5, 1}, kZero, kZero);

        //
        // Action: Connect view holder to Scene.
        // Expect, with focus change request: focus change, with new focus chain.
        //
        session_anchor->Attach(*state->holder_child);

        test->RequestToPresent(session);
      });

  // TODO(fxbug.dev/42737): Remove when session update logic guarantees view tree updates in every
  // session.
  child_client.RunNow([test = this](scenic::Session* session, scenic::EntityNode* session_anchor) {
    test->RequestToPresent(session);
  });

  EXPECT_TRUE(RequestFocusChange(&parent_focuser, target));
  ASSERT_EQ(CountReceivedFocusChains(), 3u);

  ASSERT_TRUE(LastFocusChain()->has_focus_chain());
  ASSERT_EQ(LastFocusChain()->focus_chain().size(), 2u);
  EXPECT_EQ(utils::ExtractKoid(LastFocusChain()->focus_chain()[1]), utils::ExtractKoid(target));
}

TEST_F(FocusTransferTest, ViewFocuserDisconnectedWhenSessionDies) {
  ViewFocuserPtr parent_focuser;
  ASSERT_FALSE(parent_focuser);
  {
    // Scope limits client lifetime.
    ParentClient parent_client(scenic(), parent_focuser.NewRequest());
    RunLoopUntilIdle();
    ASSERT_TRUE(parent_focuser);
  }
  RunLoopUntilIdle();

  // Client death guarantees focuser disconnect.
  ASSERT_FALSE(parent_focuser);
}

TEST_F(FocusTransferTest, ViewFocuserDisconnectDoesNotKillSession) {
  ViewFocuserPtr parent_focuser;
  ASSERT_FALSE(parent_focuser);

  ParentClient parent_client(scenic(), parent_focuser.NewRequest());
  parent_client.session()->set_error_handler(
      [](zx_status_t) { GTEST_FAIL() << "Client shut down unexpectedly."; });
  RunLoopUntilIdle();
  ASSERT_TRUE(parent_focuser);

  parent_focuser.Unbind();
  ASSERT_FALSE(parent_focuser);

  parent_client.RunNow([test = this](scenic::Session* session, scenic::EntityNode* session_anchor) {
    test->RequestToPresent(session);
  });
}

}  // namespace src_ui_scenic_lib_gfx_tests
