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

// This test exercises the properties of a focus chain. The setup has multiple
// Views arranged in a hierarchy, and also includes a FocusChainListener.  Each
// change in focus should be accompanied by a new focus chain. The listener
// should immediately receive an updated focus chain.
//
// The geometry is not important in this test, so View surface geometries will overlap on a 9 x 9
// pixel layer.
//
// Since GFX Views have their origin coordinate at the top-left, we don't need to perform
// translation to center each View on the owning Layer.

namespace src_ui_scenic_lib_gfx_tests {

using fuchsia::ui::focus::FocusChain;
using fuchsia::ui::focus::FocusChainListener;
using fuchsia::ui::focus::FocusChainListenerRegistry;
using fuchsia::ui::focus::FocusChainListenerRegistryPtr;
using fuchsia::ui::views::ViewRef;
using scenic_impl::gfx::ViewTree;
using scenic_impl::gfx::test::SessionWrapper;
using utils::ExtractKoid;
using ViewFocuserPtr = fuchsia::ui::views::FocuserPtr;
using ViewFocuserRequest = fidl::InterfaceRequest<fuchsia::ui::views::Focuser>;

// Class fixture for TEST_F.
class FocusChainRegisterTest : public scenic_impl::gfx::test::GfxSystemTest,
                               public FocusChainListener {
 protected:
  FocusChainRegisterTest() : focus_chain_listener_(this) {}
  ~FocusChainRegisterTest() override = default;

  void SetUp() override {
    scenic_impl::gfx::test::GfxSystemTest::SetUp();

    context_provider().ConnectToPublicService<FocusChainListenerRegistry>(
        focus_chain_listener_registry_.NewRequest());
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

  void RegisterListener() {
    fidl::InterfaceHandle<FocusChainListener> listener_handle;
    focus_chain_listener_.Bind(listener_handle.NewRequest());
    focus_chain_listener_registry_->Register(std::move(listener_handle));
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

 protected:
  FocusChainListenerRegistryPtr focus_chain_listener_registry_;

 private:
  fidl::Binding<FocusChainListener> focus_chain_listener_;
  std::vector<FocusChain> observed_focus_chains_;
};

class FocusChainTest : public FocusChainRegisterTest {
 protected:
  FocusChainTest() = default;
  ~FocusChainTest() override = default;

  void SetUp() override {
    FocusChainRegisterTest::SetUp();

    EXPECT_EQ(CountReceivedFocusChains(), 0u);
    RegisterListener();
    RunLoopUntilIdle();
    // Should get an empty focus chain if registering with no scene.
    EXPECT_EQ(CountReceivedFocusChains(), 1u);
    ASSERT_TRUE(LastFocusChain());
    EXPECT_TRUE(LastFocusChain()->IsEmpty());
  }
};

// Some classes use the following two-node tree topology:
//     A
//     |
//     B
// However, don't hesitate to craft a tree topology to best suit the test.
struct TwoNodeFocusChainTest : public FocusChainTest {
  struct ClientA : public SessionWrapper {
    ClientA(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::Scene> scene;
    std::unique_ptr<scenic::ViewHolder> holder_b;
  };
  struct ClientB : public SessionWrapper {
    ClientB(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::View> view;
    zx_koid_t view_ref_koid = ZX_KOID_INVALID;
  };

  void SetUp() override {
    FocusChainTest::SetUp();

    client_a = std::make_unique<ClientA>(scenic());
    client_b = std::make_unique<ClientB>(scenic());

    auto pair_ab = scenic::ViewTokenPair::New();

    client_a->RunNow(
        [test = this, state = client_a.get(), vh_ab = std::move(pair_ab.view_holder_token)](
            scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
          const std::array<float, 3> kZero = {0, 0, 0};

          // Minimal scene.
          scenic::Compositor compositor(session);
          scenic::LayerStack layer_stack(session);
          compositor.SetLayerStack(layer_stack);

          scenic::Layer layer(session);
          layer.SetSize(9 /*px*/, 9 /*px*/);
          layer_stack.AddLayer(layer);
          scenic::Renderer renderer(session);
          layer.SetRenderer(renderer);
          state->scene = std::make_unique<scenic::Scene>(session);
          scenic::Camera camera(*state->scene);
          renderer.SetCamera(camera);

          // Add local root node to the scene, and attach the ViewHolder to the root node.
          state->scene->AddChild(*session_anchor);
          state->holder_b =
              std::make_unique<scenic::ViewHolder>(session, std::move(vh_ab), "view holder B");
          state->holder_b->SetViewProperties(kZero, {9, 9, 1}, kZero, kZero);
          session_anchor->Attach(*state->holder_b);

          test->RequestToPresent(session);
        });

    client_b->RunNow([test = this, state = client_b.get(), v_ab = std::move(pair_ab.view_token)](
                         scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
      auto pair = scenic::ViewRefPair::New();
      state->view_ref_koid = ExtractKoid(pair.view_ref);
      state->view =
          std::make_unique<scenic::View>(session, std::move(v_ab), std::move(pair.control_ref),
                                         std::move(pair.view_ref), "view B");
      state->view->AddChild(*session_anchor);

      test->RequestToPresent(session);
    });
  }

  void TearDown() override {
    client_a = nullptr;
    client_b = nullptr;
    FocusChainTest::TearDown();
  }

  std::unique_ptr<ClientA> client_a;
  std::unique_ptr<ClientB> client_b;
};

// Some classes use the following three-node tree topology:
//     A
//     |
//     B
//     |
//     C
// However, don't hesitate to craft a tree topology to best suit the test.
struct ThreeNodeFocusChainTest : public FocusChainTest {
  struct ClientA : public SessionWrapper {
    ClientA(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::Scene> scene;
    std::unique_ptr<scenic::ViewHolder> holder_b;
  };
  struct ClientB : public SessionWrapper {
    ClientB(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::View> view;
    zx_koid_t view_ref_koid = ZX_KOID_INVALID;
    std::unique_ptr<scenic::ViewHolder> holder_c;
  };
  struct ClientC : public SessionWrapper {
    ClientC(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::View> view;
    zx_koid_t view_ref_koid = ZX_KOID_INVALID;
  };

  void SetUp() override {
    FocusChainTest::SetUp();

    client_a = std::make_unique<ClientA>(scenic());
    client_b = std::make_unique<ClientB>(scenic());
    client_c = std::make_unique<ClientC>(scenic());

    auto pair_ab = scenic::ViewTokenPair::New();
    auto pair_bc = scenic::ViewTokenPair::New();

    client_a->RunNow(
        [test = this, state = client_a.get(), vh_ab = std::move(pair_ab.view_holder_token)](
            scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
          const std::array<float, 3> kZero = {0, 0, 0};

          // Minimal scene.
          scenic::Compositor compositor(session);
          scenic::LayerStack layer_stack(session);
          compositor.SetLayerStack(layer_stack);

          scenic::Layer layer(session);
          layer.SetSize(9 /*px*/, 9 /*px*/);
          layer_stack.AddLayer(layer);
          scenic::Renderer renderer(session);
          layer.SetRenderer(renderer);
          state->scene = std::make_unique<scenic::Scene>(session);
          scenic::Camera camera(*state->scene);
          renderer.SetCamera(camera);

          // Add local root node to the scene, and attach the ViewHolder to the root node.
          state->scene->AddChild(*session_anchor);
          state->holder_b =
              std::make_unique<scenic::ViewHolder>(session, std::move(vh_ab), "view holder B");
          state->holder_b->SetViewProperties(kZero, {9, 9, 1}, kZero, kZero);
          session_anchor->Attach(*state->holder_b);

          test->RequestToPresent(session);
        });

    client_b->RunNow([test = this, state = client_b.get(), v_ab = std::move(pair_ab.view_token),
                      vh_bc = std::move(pair_bc.view_holder_token)](
                         scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
      const std::array<float, 3> kZero = {0, 0, 0};

      auto pair = scenic::ViewRefPair::New();
      state->view_ref_koid = ExtractKoid(pair.view_ref);
      state->view =
          std::make_unique<scenic::View>(session, std::move(v_ab), std::move(pair.control_ref),
                                         std::move(pair.view_ref), "view B");
      state->view->AddChild(*session_anchor);

      state->holder_c =
          std::make_unique<scenic::ViewHolder>(session, std::move(vh_bc), "view holder C");
      state->holder_c->SetViewProperties(kZero, {9, 9, 1}, kZero, kZero);
      session_anchor->Attach(*state->holder_c);

      test->RequestToPresent(session);
    });

    client_c->RunNow([test = this, state = client_c.get(), v_bc = std::move(pair_bc.view_token)](
                         scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
      auto pair = scenic::ViewRefPair::New();
      state->view_ref_koid = ExtractKoid(pair.view_ref);
      state->view =
          std::make_unique<scenic::View>(session, std::move(v_bc), std::move(pair.control_ref),
                                         std::move(pair.view_ref), "view C");
      state->view->AddChild(*session_anchor);

      test->RequestToPresent(session);
    });
  }

  void TearDown() override {
    client_a = nullptr;
    client_b = nullptr;
    client_c = nullptr;
    FocusChainTest::TearDown();
  }

  std::unique_ptr<ClientA> client_a;
  std::unique_ptr<ClientB> client_b;
  std::unique_ptr<ClientC> client_c;
};

// Some classes use the following four-node tree topology:
//      A
//    /   \
//   B     C
//   |
//   D
// However, don't hesitate to craft a tree topology to best suit the test.
struct FourNodeFocusChainTest : public FocusChainTest {
  struct RootClient : public SessionWrapper {
    RootClient(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::Scene> scene;
    std::unique_ptr<scenic::ViewHolder> holder_b;
    std::unique_ptr<scenic::ViewHolder> holder_c;
  };
  struct BranchClient : public SessionWrapper {
    BranchClient(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::View> view;
    zx_koid_t view_ref_koid = ZX_KOID_INVALID;
    std::unique_ptr<scenic::ViewHolder> holder_d;
  };
  struct LeafClient : public SessionWrapper {
    LeafClient(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::View> view;
    zx_koid_t view_ref_koid = ZX_KOID_INVALID;
  };

  void SetUp() override {
    FocusChainTest::SetUp();

    client_a = std::make_unique<RootClient>(scenic());
    client_b = std::make_unique<BranchClient>(scenic());
    client_c = std::make_unique<LeafClient>(scenic());
    client_d = std::make_unique<LeafClient>(scenic());

    auto pair_ab = scenic::ViewTokenPair::New();
    auto pair_ac = scenic::ViewTokenPair::New();
    auto pair_bd = scenic::ViewTokenPair::New();

    client_a->RunNow([test = this, state = client_a.get(),
                      vh_ab = std::move(pair_ab.view_holder_token),
                      vh_ac = std::move(pair_ac.view_holder_token)](
                         scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
      const std::array<float, 3> kZero = {0, 0, 0};

      // Minimal scene.
      scenic::Compositor compositor(session);
      scenic::LayerStack layer_stack(session);
      compositor.SetLayerStack(layer_stack);

      scenic::Layer layer(session);
      layer.SetSize(9 /*px*/, 9 /*px*/);
      layer_stack.AddLayer(layer);
      scenic::Renderer renderer(session);
      layer.SetRenderer(renderer);
      state->scene = std::make_unique<scenic::Scene>(session);
      scenic::Camera camera(*state->scene);
      renderer.SetCamera(camera);

      // Add local root node to the scene, and ViewHolders to the root node.
      state->scene->AddChild(*session_anchor);

      state->holder_b =
          std::make_unique<scenic::ViewHolder>(session, std::move(vh_ab), "view holder B");
      state->holder_c =
          std::make_unique<scenic::ViewHolder>(session, std::move(vh_ac), "view holder C");
      state->holder_b->SetViewProperties(kZero, {9, 9, 1}, kZero, kZero);
      state->holder_c->SetViewProperties(kZero, {9, 9, 1}, kZero, kZero);
      session_anchor->Attach(*state->holder_b);
      session_anchor->Attach(*state->holder_c);

      test->RequestToPresent(session);
    });

    client_b->RunNow([test = this, state = client_b.get(), v_ab = std::move(pair_ab.view_token),
                      vh_bd = std::move(pair_bd.view_holder_token)](
                         scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
      const std::array<float, 3> kZero = {0, 0, 0};
      auto pair = scenic::ViewRefPair::New();
      state->view_ref_koid = ExtractKoid(pair.view_ref);
      state->view =
          std::make_unique<scenic::View>(session, std::move(v_ab), std::move(pair.control_ref),
                                         std::move(pair.view_ref), "view B");
      state->view->AddChild(*session_anchor);

      state->holder_d =
          std::make_unique<scenic::ViewHolder>(session, std::move(vh_bd), "view holder D");
      state->holder_d->SetViewProperties(kZero, {9, 9, 1}, kZero, kZero);
      session_anchor->Attach(*state->holder_d);

      test->RequestToPresent(session);
    });

    client_c->RunNow([test = this, state = client_c.get(), v_ac = std::move(pair_ac.view_token)](
                         scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
      auto pair = scenic::ViewRefPair::New();
      state->view_ref_koid = ExtractKoid(pair.view_ref);
      state->view =
          std::make_unique<scenic::View>(session, std::move(v_ac), std::move(pair.control_ref),
                                         std::move(pair.view_ref), "view C");
      state->view->AddChild(*session_anchor);

      test->RequestToPresent(session);
    });

    client_d->RunNow([test = this, state = client_d.get(), v_bd = std::move(pair_bd.view_token)](
                         scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
      auto pair = scenic::ViewRefPair::New();
      state->view_ref_koid = ExtractKoid(pair.view_ref);
      state->view =
          std::make_unique<scenic::View>(session, std::move(v_bd), std::move(pair.control_ref),
                                         std::move(pair.view_ref), "view D");
      state->view->AddChild(*session_anchor);

      test->RequestToPresent(session);
    });
  }

  void TearDown() override {
    client_a = nullptr;
    client_b = nullptr;
    client_c = nullptr;
    client_d = nullptr;
    FocusChainTest::TearDown();
  }

  std::unique_ptr<RootClient> client_a;
  std::unique_ptr<BranchClient> client_b;
  std::unique_ptr<LeafClient> client_c;
  std::unique_ptr<LeafClient> client_d;
};

TEST_F(FocusChainRegisterTest, RegisterBeforeSceneSetup_ShouldReturnEmptyFocusChain) {
  EXPECT_EQ(CountReceivedFocusChains(), 0u);  // Before registering no focus chain received.

  RegisterListener();
  RunLoopUntilIdle();

  EXPECT_EQ(CountReceivedFocusChains(), 1u);
  ASSERT_TRUE(LastFocusChain());
  EXPECT_FALSE(LastFocusChain()->has_focus_chain());
}

TEST_F(FocusChainTest, EmptySceneTransitions) {
  EXPECT_EQ(CountReceivedFocusChains(), 1u);  // Initial focus chain on register.

  struct RootClient : public SessionWrapper {
    RootClient(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}

    std::unique_ptr<scenic::Scene> scene;
  } some_session(scenic()), root_session(scenic());

  some_session.RunNow([test = this, state = &some_session](
                          scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
    // Merely creating a scene, without hooking it up to a compositor properly, should not
    // trigger a focus change.
    state->scene = std::make_unique<scenic::Scene>(session);
    state->scene->AddChild(*session_anchor);

    test->RequestToPresent(session);
  });

  EXPECT_EQ(CountReceivedFocusChains(), 1u);

  root_session.RunNow([test = this, state = &root_session](
                          scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
    // Create a scene that is hooked up to a compositor. This set of
    // commands should trigger the creation of a focus chain, with length 1.
    scenic::Compositor compositor(session);
    scenic::LayerStack layer_stack(session);
    compositor.SetLayerStack(layer_stack);

    scenic::Layer layer(session);
    layer.SetSize(9 /*px*/, 9 /*px*/);
    layer_stack.AddLayer(layer);
    scenic::Renderer renderer(session);
    layer.SetRenderer(renderer);
    state->scene = std::make_unique<scenic::Scene>(session);
    scenic::Camera camera(*state->scene);
    renderer.SetCamera(camera);

    state->scene->AddChild(*session_anchor);

    test->RequestToPresent(session);
  });

  EXPECT_EQ(CountReceivedFocusChains(), 2u);
  ASSERT_TRUE(LastFocusChain());
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 1u);
}

TEST_F(FocusChainTest, MultipleListeners) {
  EXPECT_EQ(CountReceivedFocusChains(), 1u);  // Initial focus chain on register.

  class DummyListener : public FocusChainListener {
   public:
    DummyListener() : focus_chain_listener_(this) {}
    // |fuchsia::ui::focus::FocusChainListener|
    void OnFocusChange(FocusChain focus_chain, OnFocusChangeCallback callback) override {
      ++num_focus_chains_received_;
    }

    fidl::Binding<FocusChainListener> focus_chain_listener_;
    uint64_t num_focus_chains_received_ = 0;
  };

  DummyListener listener2;
  fidl::InterfaceHandle<FocusChainListener> listener_handle;
  listener2.focus_chain_listener_.Bind(listener_handle.NewRequest());
  focus_chain_listener_registry_->Register(std::move(listener_handle));

  RunLoopUntilIdle();
  EXPECT_EQ(listener2.num_focus_chains_received_, 1u);

  struct RootClient : public SessionWrapper {
    RootClient(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}

    std::unique_ptr<scenic::Scene> scene;
  } some_session(scenic()), root_session(scenic());

  some_session.RunNow([test = this, state = &some_session](
                          scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
    // Merely creating a scene, without hooking it up to a compositor properly, should not
    // trigger a focus change.
    state->scene = std::make_unique<scenic::Scene>(session);
    state->scene->AddChild(*session_anchor);

    test->RequestToPresent(session);
  });

  EXPECT_EQ(CountReceivedFocusChains(), 1u);

  root_session.RunNow([test = this, state = &root_session](
                          scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
    // Create a scene that is hooked up to a compositor. This set of
    // commands should trigger the creation of a focus chain, with length 1.
    scenic::Compositor compositor(session);
    scenic::LayerStack layer_stack(session);
    compositor.SetLayerStack(layer_stack);

    scenic::Layer layer(session);
    layer.SetSize(9 /*px*/, 9 /*px*/);
    layer_stack.AddLayer(layer);
    scenic::Renderer renderer(session);
    layer.SetRenderer(renderer);
    state->scene = std::make_unique<scenic::Scene>(session);
    scenic::Camera camera(*state->scene);
    renderer.SetCamera(camera);

    state->scene->AddChild(*session_anchor);

    test->RequestToPresent(session);
  });

  EXPECT_EQ(CountReceivedFocusChains(), 2u);
  EXPECT_EQ(listener2.num_focus_chains_received_, 2u);
  ASSERT_TRUE(LastFocusChain());
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 1u);
}

// Registering after the scene has been setup should result in getting an initial focus chain.
TEST_F(FocusChainRegisterTest, RegisterAfterSceneSetup_ShouldReturnNonEmptyFocusChain) {
  struct RootClient : public SessionWrapper {
    RootClient(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}

    std::unique_ptr<scenic::Scene> scene;
  } root_session(scenic());

  root_session.RunNow([test = this, state = &root_session](
                          scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
    // Create a scene that is hooked up to a compositor. This set of
    // commands should trigger the creation of a focus chain, with length 1.
    scenic::Compositor compositor(session);
    scenic::LayerStack layer_stack(session);
    compositor.SetLayerStack(layer_stack);

    scenic::Layer layer(session);
    layer.SetSize(9 /*px*/, 9 /*px*/);
    layer_stack.AddLayer(layer);
    scenic::Renderer renderer(session);
    layer.SetRenderer(renderer);
    state->scene = std::make_unique<scenic::Scene>(session);
    scenic::Camera camera(*state->scene);
    renderer.SetCamera(camera);

    state->scene->AddChild(*session_anchor);

    test->RequestToPresent(session);
  });

  EXPECT_EQ(CountReceivedFocusChains(), 0u);  // Before registering no focus chain received.

  RegisterListener();
  RunLoopUntilIdle();

  EXPECT_EQ(CountReceivedFocusChains(), 1u);
  ASSERT_TRUE(LastFocusChain());
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 1u);
}

// Tree topology:
//      [ A.scene_b  A.scene_c ]
//            |           |
//            B           C
// Focus chain is determined by which scene (if any) is connected to the compositor.
// This test emulates Root Presenter's Presentation swap.
TEST_F(FocusChainTest, LayerSwapMeansSceneSwap) {
  struct MultiSceneRootClient : public SessionWrapper {
    MultiSceneRootClient(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}

    std::unique_ptr<scenic::Compositor> compositor;
    std::unique_ptr<scenic::LayerStack> layer_stack;
    std::unique_ptr<scenic::Layer> layer_b;
    std::unique_ptr<scenic::Layer> layer_c;
  } client_a(scenic());

  struct LeafClient : public SessionWrapper {
    LeafClient(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}

    std::unique_ptr<scenic::View> view;
    zx_koid_t view_ref_koid = ZX_KOID_INVALID;
  } client_b(scenic()), client_c(scenic());

  auto pair_ab = scenic::ViewTokenPair::New();
  auto pair_ac = scenic::ViewTokenPair::New();

  client_a.RunNow([test = this, state = &client_a, vh_ab = std::move(pair_ab.view_holder_token),
                   vh_ac = std::move(pair_ac.view_holder_token)](
                      scenic::Session* session, scenic::EntityNode* unused) mutable {
    const std::array<float, 3> kZero = {0, 0, 0};

    // Scene graph is set up with multiple layers, and inserts at most one in the layer stack.
    state->compositor = std::make_unique<scenic::Compositor>(session);
    state->layer_stack = std::make_unique<scenic::LayerStack>(session);
    state->compositor->SetLayerStack(*state->layer_stack);

    // Create layer_b and scene, but do not insert into layer stack yet.
    {
      state->layer_b = std::make_unique<scenic::Layer>(session);
      state->layer_b->SetSize(9 /*px*/, 9 /*px*/);
      scenic::Renderer renderer(session);
      state->layer_b->SetRenderer(renderer);
      scenic::Scene scene(session);
      scenic::Camera camera(scene);
      renderer.SetCamera(camera);

      scenic::EntityNode root_b(session);
      scene.AddChild(root_b);

      scenic::ViewHolder holder_b(session, std::move(vh_ab), "view holder B");
      root_b.Attach(holder_b);
    }

    // Create layer_c and scene, but do not insert into layer stack yet.
    {
      state->layer_c = std::make_unique<scenic::Layer>(session);
      state->layer_c->SetSize(9 /*px*/, 9 /*px*/);
      scenic::Renderer renderer(session);
      state->layer_c->SetRenderer(renderer);
      scenic::Scene scene(session);
      scenic::Camera camera(scene);
      renderer.SetCamera(camera);

      scenic::EntityNode root_c(session);
      scene.AddChild(root_c);

      scenic::ViewHolder holder_c(session, std::move(vh_ac), "view holder C");
      root_c.Attach(holder_c);
    }

    test->RequestToPresent(session);
  });

  client_b.RunNow([test = this, state = &client_b, v_ab = std::move(pair_ab.view_token)](
                      scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
    auto pair = scenic::ViewRefPair::New();
    state->view_ref_koid = ExtractKoid(pair.view_ref);
    state->view = std::make_unique<scenic::View>(
        session, std::move(v_ab), std::move(pair.control_ref), std::move(pair.view_ref), "view B");

    test->RequestToPresent(session);
  });

  client_c.RunNow([test = this, state = &client_c, v_ac = std::move(pair_ac.view_token)](
                      scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
    auto pair = scenic::ViewRefPair::New();
    state->view_ref_koid = ExtractKoid(pair.view_ref);
    state->view = std::make_unique<scenic::View>(
        session, std::move(v_ac), std::move(pair.control_ref), std::move(pair.view_ref), "view C");

    test->RequestToPresent(session);
  });

  ASSERT_FALSE(RunLoopUntilIdle());  // There should be no pending tasks.

  EXPECT_EQ(CountReceivedFocusChains(), 1u);

  // Add Layer B.
  client_a.RunNow([test = this, state = &client_a](scenic::Session* session,
                                                   scenic::EntityNode* session_anchor) mutable {
    state->layer_stack->AddLayer(*state->layer_b);
    test->RequestToPresent(session);
  });

  EXPECT_EQ(CountReceivedFocusChains(), 2u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 1u);
  const zx_koid_t scene_b = ExtractKoid(LastFocusChain()->focus_chain()[0]);

  // Layer B's focus chain extended to B.
  ViewTree::FocusChangeStatus status =
      engine()->scene_graph()->RequestFocusChange(scene_b, client_b.view_ref_koid);
  RunLoopUntilIdle();  // Process FIDL messages.

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(CountReceivedFocusChains(), 3u);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[0]), scene_b);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[1]), client_b.view_ref_koid);

  // Replace Layer B with Layer C.
  client_a.RunNow([test = this, state = &client_a](scenic::Session* session,
                                                   scenic::EntityNode* session_anchor) mutable {
    state->layer_stack->RemoveAllLayers();
    state->layer_stack->AddLayer(*state->layer_c);

    test->RequestToPresent(session);
  });

  EXPECT_EQ(CountReceivedFocusChains(), 4u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 1u);
  const zx_koid_t scene_c = ExtractKoid(LastFocusChain()->focus_chain()[0]);

  // Root is swapped out!
  EXPECT_NE(scene_b, scene_c);

  // Layer C's focus chain extended to C.
  status = engine()->scene_graph()->RequestFocusChange(scene_c, client_c.view_ref_koid);
  RunLoopUntilIdle();  // Process FIDL messages.

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(CountReceivedFocusChains(), 5u);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[0]), scene_c);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[1]), client_c.view_ref_koid);

  // Remove Layer C.
  client_a.RunNow([test = this, state = &client_a](scenic::Session* session,
                                                   scenic::EntityNode* session_anchor) mutable {
    state->layer_stack->RemoveAllLayers();

    test->RequestToPresent(session);
  });

  EXPECT_EQ(CountReceivedFocusChains(), 6u);
  EXPECT_TRUE(LastFocusChain()->IsEmpty());
}

// Tree topology:
//     A
//     |
//     B
// This test is intentionally not a TwoNodeFocusChainTest because it makes assertions against
// intermediate state during tree setup.
TEST_F(FocusChainTest, OneLinkTopology) {
  auto pair_ab = scenic::ViewTokenPair::New();

  // Client "A" sets up the Scene, and a ViewHolder for "B".
  struct RootClient : public SessionWrapper {
    RootClient(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::Scene> scene;
    std::unique_ptr<scenic::ViewHolder> view_holder;
  } client_a(scenic());

  client_a.RunNow([test = this, state = &client_a, vh_ab = std::move(pair_ab.view_holder_token)](
                      scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
    const std::array<float, 3> kZero = {0, 0, 0};

    // Minimal scene.
    scenic::Compositor compositor(session);
    scenic::LayerStack layer_stack(session);
    compositor.SetLayerStack(layer_stack);

    scenic::Layer layer(session);
    layer.SetSize(9 /*px*/, 9 /*px*/);
    layer_stack.AddLayer(layer);
    scenic::Renderer renderer(session);
    layer.SetRenderer(renderer);
    state->scene = std::make_unique<scenic::Scene>(session);
    scenic::Camera camera(*state->scene);
    renderer.SetCamera(camera);

    // Add local root node to the scene, and attach the ViewHolder to the root node.
    state->scene->AddChild(*session_anchor);
    state->view_holder =
        std::make_unique<scenic::ViewHolder>(session, std::move(vh_ab), "view holder B");
    state->view_holder->SetViewProperties(kZero, {9, 9, 1}, kZero, kZero);
    session_anchor->Attach(*state->view_holder);

    test->RequestToPresent(session);
  });

  // Merely setting up a ViewHolder does not trigger a fresh focus chain, or make it longer.
  EXPECT_EQ(CountReceivedFocusChains(), 2u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 1u);

  // Client "B" sets up its own View.
  struct LeafClient : public SessionWrapper {
    LeafClient(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::View> view;
    zx_koid_t view_ref_koid = ZX_KOID_INVALID;
  } client_b(scenic());

  client_b.RunNow([test = this, state = &client_b, v_ab = std::move(pair_ab.view_token)](
                      scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
    auto pair = scenic::ViewRefPair::New();
    state->view_ref_koid = ExtractKoid(pair.view_ref);
    state->view = std::make_unique<scenic::View>(
        session, std::move(v_ab), std::move(pair.control_ref), std::move(pair.view_ref), "view B");
    state->view->AddChild(*session_anchor);

    test->RequestToPresent(session);
  });

  // Merely setting up a separate View, that is connected to the scene, does not trigger a fresh
  // focus chain, or make it longer.
  EXPECT_EQ(CountReceivedFocusChains(), 2u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 1u);
}

TEST_F(TwoNodeFocusChainTest, FocusTransferDownAllowed) {
  // Emulate a focus transfer, due to either an explicit command, or via user input.
  const zx_koid_t root = engine()->scene_graph()->view_tree().focus_chain()[0];
  ViewTree::FocusChangeStatus status =
      engine()->scene_graph()->RequestFocusChange(root, client_b->view_ref_koid);
  RunLoopUntilIdle();  // Process FIDL messages.

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(CountReceivedFocusChains(), 3u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);
}

TEST_F(TwoNodeFocusChainTest, FocusTransferNullChangeNoFidl) {
  // A View can transfer focus from itself to itself, but a null change should not trigger a new
  // focus chain.
  const zx_koid_t root = engine()->scene_graph()->view_tree().focus_chain()[0];
  ViewTree::FocusChangeStatus status = engine()->scene_graph()->RequestFocusChange(root, root);
  RunLoopUntilIdle();  // Process FIDL messages.

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(CountReceivedFocusChains(), 2u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 1u);

  // Transfer down for a similar test on Client B.
  status = engine()->scene_graph()->RequestFocusChange(root, client_b->view_ref_koid);
  RunLoopUntilIdle();  // Process FIDL messages.

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(CountReceivedFocusChains(), 3u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);

  // Transfer focus from itself to itself. No change expected.
  status =
      engine()->scene_graph()->RequestFocusChange(client_b->view_ref_koid, client_b->view_ref_koid);
  RunLoopUntilIdle();  // Process FIDL messages.

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(CountReceivedFocusChains(), 3u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);
}

TEST_F(TwoNodeFocusChainTest, FocusTransferUpwardDenied) {
  const zx_koid_t root = engine()->scene_graph()->view_tree().focus_chain()[0];
  ViewTree::FocusChangeStatus status =
      engine()->scene_graph()->RequestFocusChange(root, client_b->view_ref_koid);
  RunLoopUntilIdle();  // Process FIDL messages.

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(CountReceivedFocusChains(), 3u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);

  // Upward focus request, denied. No change expected.
  status = engine()->scene_graph()->RequestFocusChange(client_b->view_ref_koid, root);
  RunLoopUntilIdle();  // Process FIDL messages.

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kErrorRequestorNotRequestAncestor);
  EXPECT_EQ(CountReceivedFocusChains(), 3u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);
}

// Tree topology:
//         A
//      /    \
//     B      C
//     |
//     D
TEST_F(FourNodeFocusChainTest, LengthyFocusChain) {
  // Merely setting up four separate Views, that are connected to the scene, does not trigger a
  // fresh focus chain, or make it longer.
  EXPECT_EQ(CountReceivedFocusChains(), 2u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 1u);

  // Emulate a focus transfer from "A" to "D".
  const zx_koid_t root = engine()->scene_graph()->view_tree().focus_chain()[0];
  ViewTree::FocusChangeStatus status =
      engine()->scene_graph()->RequestFocusChange(root, client_d->view_ref_koid);
  RunLoopUntilIdle();  // Process FIDL messages.

  // Focus chain is now [A - B - D].
  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(CountReceivedFocusChains(), 3u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 3u);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[0]), root);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[1]), client_b->view_ref_koid);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[2]), client_d->view_ref_koid);
}

TEST_F(FourNodeFocusChainTest, SiblingTransferRequestsDenied) {
  // Setup: Transfer to "D".
  const zx_koid_t root = engine()->scene_graph()->view_tree().focus_chain()[0];
  ViewTree::FocusChangeStatus status =
      engine()->scene_graph()->RequestFocusChange(root, client_d->view_ref_koid);
  RunLoopUntilIdle();  // Process FIDL messages.

  // Transfer request from "D" to "C" denied.
  status =
      engine()->scene_graph()->RequestFocusChange(client_d->view_ref_koid, client_c->view_ref_koid);
  RunLoopUntilIdle();  // Process FIDL messages.

  // No change in focus chain.
  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kErrorRequestorNotRequestAncestor);
  EXPECT_EQ(CountReceivedFocusChains(), 3u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 3u);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[0]), root);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[1]), client_b->view_ref_koid);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[2]), client_d->view_ref_koid);

  // Transfer request from "C" to "C" denied.
  status =
      engine()->scene_graph()->RequestFocusChange(client_c->view_ref_koid, client_c->view_ref_koid);
  RunLoopUntilIdle();  // Process FIDL messages.

  // No change in focus chain.
  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kErrorRequestorNotAuthorized);
  EXPECT_EQ(CountReceivedFocusChains(), 3u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 3u);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[0]), root);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[1]), client_b->view_ref_koid);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[2]), client_d->view_ref_koid);
}

// Tree topology:
//     A
//     |
//     B
//     |
//     C
TEST_F(ThreeNodeFocusChainTest, ViewDestructionShortensFocusChain) {
  // Start with a transfer of focus, from "A" to "C". Focus chain is length 3.
  zx_koid_t root = engine()->scene_graph()->view_tree().focus_chain()[0];
  ViewTree::FocusChangeStatus status =
      engine()->scene_graph()->RequestFocusChange(root, client_c->view_ref_koid);
  RunLoopUntilIdle();  // Process FIDL messages.

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(CountReceivedFocusChains(), 3u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 3u);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[0]), root);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[1]), client_b->view_ref_koid);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[2]), client_c->view_ref_koid);

  // Client "C" destroys its view.
  client_c->RunNow([test = this, state = client_c.get()](scenic::Session* session,
                                                         scenic::EntityNode* session_anchor) {
    state->view = nullptr;
    test->RequestToPresent(session);
  });

  // Focus chain is now length 2.
  EXPECT_EQ(CountReceivedFocusChains(), 4u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[0]), root);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[1]), client_b->view_ref_koid);

  // Client "B" destroys its view.
  client_b->RunNow([test = this, state = client_b.get()](scenic::Session* session,
                                                         scenic::EntityNode* session_anchor) {
    state->view = nullptr;
    test->RequestToPresent(session);
  });

  // Focus chain is now length 1.
  EXPECT_EQ(CountReceivedFocusChains(), 5u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 1u);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[0]), root);

  // Client "A" destroys its scene.
  client_a->RunNow([test = this, state = client_a.get()](scenic::Session* session,
                                                         scenic::EntityNode* session_anchor) {
    state->scene = nullptr;
    test->RequestToPresent(session);
  });

  // Focus chain is now empty.
  EXPECT_EQ(CountReceivedFocusChains(), 6u);
  EXPECT_TRUE(LastFocusChain()->IsEmpty());
}

// Tree topology:
//     A
//     |
//     B
//     |
//     C
TEST_F(ThreeNodeFocusChainTest, ViewHolderDestructionShortensFocusChain) {
  // Start with a transfer of focus, from "A" to "C". Focus chain is length 3.
  const zx_koid_t root = engine()->scene_graph()->view_tree().focus_chain()[0];
  ViewTree::FocusChangeStatus status =
      engine()->scene_graph()->RequestFocusChange(root, client_c->view_ref_koid);
  RunLoopUntilIdle();  // Process FIDL messages.

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(CountReceivedFocusChains(), 3u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 3u);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[0]), root);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[1]), client_b->view_ref_koid);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[2]), client_c->view_ref_koid);

  // Client "B" detaches and destroys its view holder.
  client_b->RunNow([test = this, state = client_b.get()](scenic::Session* session,
                                                         scenic::EntityNode* session_anchor) {
    session_anchor->DetachChildren();
    state->holder_c = nullptr;
    test->RequestToPresent(session);
  });

  // Focus chain is now length 2.
  EXPECT_EQ(CountReceivedFocusChains(), 4u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[0]), root);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[1]), client_b->view_ref_koid);

  // Client "A" destroys its scene.
  client_a->RunNow([test = this, state = client_a.get()](scenic::Session* session,
                                                         scenic::EntityNode* session_anchor) {
    state->scene = nullptr;
    test->RequestToPresent(session);
  });

  // Focus chain is now empty.
  EXPECT_EQ(CountReceivedFocusChains(), 5u);
  EXPECT_TRUE(LastFocusChain()->IsEmpty());
}

TEST_F(ThreeNodeFocusChainTest, ViewHolderDisconnectShortensFocusChain) {
  // Start with a transfer of focus, from "A" to "C". Focus chain is length 3.
  const zx_koid_t root = engine()->scene_graph()->view_tree().focus_chain()[0];
  ViewTree::FocusChangeStatus status =
      engine()->scene_graph()->RequestFocusChange(root, client_c->view_ref_koid);
  RunLoopUntilIdle();  // Process FIDL messages.

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(CountReceivedFocusChains(), 3u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 3u);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[0]), root);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[1]), client_b->view_ref_koid);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[2]), client_c->view_ref_koid);

  // Disconnect (but don't destroy) "B"'s view holder for "C".
  client_b->RunNow([test = this, state = client_b.get()](scenic::Session* session,
                                                         scenic::EntityNode* session_anchor) {
    session_anchor->DetachChildren();
    test->RequestToPresent(session);
  });

  EXPECT_EQ(CountReceivedFocusChains(), 4u);
  EXPECT_EQ(LastFocusChain()->focus_chain().size(), 2u);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[0]), root);
  EXPECT_EQ(ExtractKoid(LastFocusChain()->focus_chain()[1]), client_b->view_ref_koid);
}

TEST_F(FocusChainTest, LateViewConnectTriggersViewTreeUpdate) {
  struct ParentClient : public SessionWrapper {
    ParentClient(scenic_impl::Scenic* scenic, ViewFocuserRequest view_focuser_request)
        : SessionWrapper(scenic, std::move(view_focuser_request)) {}
    std::unique_ptr<scenic::Compositor> compositor;
    std::unique_ptr<scenic::ViewHolder> holder_child;
  };
  struct ChildClient : public SessionWrapper {
    ChildClient(scenic_impl::Scenic* scenic) : SessionWrapper(scenic) {}
    std::unique_ptr<scenic::View> view;
  };

  ViewFocuserPtr parent_focuser;
  ParentClient parent_client(scenic(), parent_focuser.NewRequest());
  ChildClient child_client(scenic());

  auto token_pair = scenic::ViewTokenPair::New();  // parent-child view tokens
  auto child_refs = scenic::ViewRefPair::New();    // child view's view ref pair

  ViewRef target;
  fidl::Clone(child_refs.view_ref, &target);

  parent_client.RunNow([test = this, state = &parent_client](scenic::Session* session,
                                                             scenic::EntityNode* session_anchor) {
    // Minimal scene, but without a ViewHolder.
    state->compositor = std::make_unique<scenic::Compositor>(session);
    scenic::LayerStack layer_stack(session);
    state->compositor->SetLayerStack(layer_stack);

    scenic::Layer layer(session);
    layer.SetSize(9 /*px*/, 9 /*px*/);
    layer_stack.AddLayer(layer);
    scenic::Renderer renderer(session);
    layer.SetRenderer(renderer);
    scenic::Scene scene(session);
    scenic::Camera camera(scene);
    renderer.SetCamera(camera);

    scene.AddChild(*session_anchor);

    test->RequestToPresent(session);
  });

  EXPECT_EQ(CountReceivedFocusChains(), 2u);

  child_client.RunNow(
      [test = this, state = &child_client, child_token = std::move(token_pair.view_token),
       control_ref = std::move(child_refs.control_ref), view_ref = std::move(child_refs.view_ref)](
          scenic::Session* session, scenic::EntityNode*) mutable {
        state->view =
            std::make_unique<scenic::View>(session, std::move(child_token), std::move(control_ref),
                                           std::move(view_ref), "child view");
        test->RequestToPresent(session);
      });

  EXPECT_EQ(CountReceivedFocusChains(), 2u);

  parent_client.RunNow(
      [test = this, state = &parent_client, parent_token = std::move(token_pair.view_holder_token)](
          scenic::Session* session, scenic::EntityNode* session_anchor) mutable {
        const std::array<float, 3> kZero = {0, 0, 0};
        state->holder_child =
            std::make_unique<scenic::ViewHolder>(session, std::move(parent_token), "child holder");
        state->holder_child->SetViewProperties(kZero, {9, 9, 1}, kZero, kZero);

        session_anchor->Attach(*state->holder_child);

        test->RequestToPresent(session);
      });

  // TODO(fxbug.dev/42737): Remove when session update logic guarantees view tree updates in every
  // session.
  child_client.RunNow([test = this](scenic::Session* session, scenic::EntityNode* session_anchor) {
    test->RequestToPresent(session);
  });

  EXPECT_TRUE(RequestFocusChange(&parent_focuser, target));
  EXPECT_EQ(CountReceivedFocusChains(), 3u);
}

}  // namespace src_ui_scenic_lib_gfx_tests
