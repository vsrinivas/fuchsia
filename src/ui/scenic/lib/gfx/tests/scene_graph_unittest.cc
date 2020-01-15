// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/eventpair.h>

#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class SceneGraphTest : public SessionTest {
 public:
  SceneGraphTest() = default;

  void TearDown() override {
    SessionTest::TearDown();
    scene_graph_.reset();
  }

  SessionContext CreateSessionContext() override {
    SessionContext session_context = SessionTest::CreateSessionContext();
    FXL_DCHECK(!view_linker_);
    FXL_DCHECK(!scene_graph_);
    view_linker_ = std::make_unique<ViewLinker>();
    scene_graph_ = std::make_unique<SceneGraph>(context_provider_.context());
    session_context.view_linker = view_linker_.get();
    session_context.scene_graph = scene_graph_->GetWeakPtr();
    return session_context;
  }

  CommandContext CreateCommandContext() {
    return CommandContext(/*uploader=*/nullptr, /*sysmem=*/nullptr,
                          /*display_manager=*/nullptr, scene_graph_->GetWeakPtr());
  }

  SceneGraph* scene_graph() const { return scene_graph_.get(); }

 private:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<SceneGraph> scene_graph_;
  std::unique_ptr<ViewLinker> view_linker_;
};

fit::function<std::optional<glm::mat4>()> NoGlobalTransform() {
  return [] { return std::nullopt; };
}

bool ContainsCompositor(const std::vector<CompositorWeakPtr>& compositors, Compositor* compositor) {
  auto it =
      std::find_if(compositors.begin(), compositors.end(),
                   [compositor](const CompositorWeakPtr& c) { return c.get() == compositor; });
  return it != compositors.end();
};

TEST_F(SceneGraphTest, CompositorsGetAddedAndRemoved) {
  sys::testing::ComponentContextProvider context_provider;
  SceneGraph scene_graph(context_provider.context());
  ASSERT_EQ(0u, scene_graph.compositors().size());
  {
    CompositorPtr c1 = Compositor::New(session(), session()->id(), 1, scene_graph.GetWeakPtr());
    ASSERT_EQ(1u, scene_graph.compositors().size());
    ASSERT_TRUE(ContainsCompositor(scene_graph.compositors(), c1.get()));
    ASSERT_EQ(scene_graph.first_compositor().get(), c1.get());
    {
      CompositorPtr c2 = Compositor::New(session(), session()->id(), 2, scene_graph.GetWeakPtr());
      ASSERT_EQ(2u, scene_graph.compositors().size());
      ASSERT_TRUE(ContainsCompositor(scene_graph.compositors(), c1.get()));
      ASSERT_TRUE(ContainsCompositor(scene_graph.compositors(), c2.get()));
      ASSERT_EQ(scene_graph.first_compositor().get(), c1.get());
    }
    ASSERT_EQ(1u, scene_graph.compositors().size());
    ASSERT_TRUE(ContainsCompositor(scene_graph.compositors(), c1.get()));
    ASSERT_EQ(scene_graph.first_compositor().get(), c1.get());
  }
}

TEST_F(SceneGraphTest, LookupCompositor) {
  sys::testing::ComponentContextProvider context_provider;
  SceneGraph scene_graph(context_provider.context());
  CompositorPtr c1 = Compositor::New(session(), session()->id(), 1, scene_graph.GetWeakPtr());
  auto c1_weak = scene_graph.GetCompositor(c1->global_id());
  ASSERT_EQ(c1.get(), c1_weak.get());
}

TEST_F(SceneGraphTest, FirstCompositorIsStable) {
  sys::testing::ComponentContextProvider context_provider;
  SceneGraph scene_graph(context_provider.context());

  CompositorPtr c1 = Compositor::New(session(), session()->id(), 1, scene_graph.GetWeakPtr());
  ASSERT_EQ(scene_graph.first_compositor().get(), c1.get());
  {
    CompositorPtr c2 = Compositor::New(session(), session()->id(), 2, scene_graph.GetWeakPtr());
    ASSERT_EQ(scene_graph.first_compositor().get(), c1.get());
    CompositorPtr c3 = Compositor::New(session(), session()->id(), 3, scene_graph.GetWeakPtr());
    ASSERT_EQ(scene_graph.first_compositor().get(), c1.get());
    {
      CompositorPtr c4 = Compositor::New(session(), session()->id(), 4, scene_graph.GetWeakPtr());
      ASSERT_EQ(scene_graph.first_compositor().get(), c1.get());
    }
    ASSERT_EQ(scene_graph.first_compositor().get(), c1.get());
    c1 = nullptr;
    // First compositor follows order of creation.
    ASSERT_EQ(2u, scene_graph.compositors().size());
    ASSERT_EQ(scene_graph.first_compositor().get(), c2.get());
  }
}

TEST_F(SceneGraphTest, RequestFocusChange) {
  // Construct ViewTree with 2 ViewRefs in a parent-child relationship.
  sys::testing::ComponentContextProvider context_provider;
  SceneGraph scene_graph(context_provider.context());
  auto parent_view_pair = scenic::ViewRefPair::New();
  const zx_koid_t parent_koid = ExtractKoid(parent_view_pair.view_ref);
  auto child_view_pair = scenic::ViewRefPair::New();
  const zx_koid_t child_koid = ExtractKoid(child_view_pair.view_ref);
  {
    ViewTreeUpdates updates;
    updates.push_back(ViewTreeNewRefNode{.view_ref = std::move(parent_view_pair.view_ref),
                                         .may_receive_focus = [] { return true; },
                                         .global_transform = NoGlobalTransform(),
                                         .session_id = 1u});
    updates.push_back(ViewTreeNewAttachNode{.koid = 1111u});
    updates.push_back(ViewTreeNewRefNode{.view_ref = std::move(child_view_pair.view_ref),
                                         .may_receive_focus = [] { return true; },
                                         .global_transform = NoGlobalTransform(),
                                         .session_id = 2u});
    updates.push_back(ViewTreeMakeGlobalRoot{.koid = parent_koid});
    updates.push_back(ViewTreeConnectToParent{.child = child_koid, .parent = 1111u});
    updates.push_back(ViewTreeConnectToParent{.child = 1111u, .parent = parent_koid});

    scene_graph.StageViewTreeUpdates(std::move(updates));
    scene_graph.ProcessViewTreeUpdates();
  }

  ASSERT_EQ(scene_graph.view_tree().focus_chain().size(), 1u);
  EXPECT_EQ(scene_graph.view_tree().focus_chain()[0], parent_koid);

  auto status = scene_graph.RequestFocusChange(parent_koid, child_koid);
  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);

  ASSERT_EQ(scene_graph.view_tree().focus_chain().size(), 2u);
  EXPECT_EQ(scene_graph.view_tree().focus_chain()[0], parent_koid);
  EXPECT_EQ(scene_graph.view_tree().focus_chain()[1], child_koid);
}

TEST_F(SceneGraphTest, RequestFocusChangeButMayNotReceiveFocus) {
  // Construct ViewTree with 2 ViewRefs in a parent-child relationship.
  sys::testing::ComponentContextProvider context_provider;
  SceneGraph scene_graph(context_provider.context());
  auto parent_view_pair = scenic::ViewRefPair::New();
  const zx_koid_t parent_koid = ExtractKoid(parent_view_pair.view_ref);
  auto child_view_pair = scenic::ViewRefPair::New();
  const zx_koid_t child_koid = ExtractKoid(child_view_pair.view_ref);
  {
    ViewTreeUpdates updates;
    updates.push_back(ViewTreeNewRefNode{.view_ref = std::move(parent_view_pair.view_ref),
                                         .may_receive_focus = [] { return true; },
                                         .global_transform = NoGlobalTransform(),
                                         .session_id = 1u});
    updates.push_back(ViewTreeNewAttachNode{.koid = 1111u});
    updates.push_back(ViewTreeNewRefNode{.view_ref = std::move(child_view_pair.view_ref),
                                         .may_receive_focus = [] { return false; },  // Different!
                                         .global_transform = NoGlobalTransform(),
                                         .session_id = 2u});
    updates.push_back(ViewTreeMakeGlobalRoot{.koid = parent_koid});
    updates.push_back(ViewTreeConnectToParent{.child = child_koid, .parent = 1111u});
    updates.push_back(ViewTreeConnectToParent{.child = 1111u, .parent = parent_koid});

    scene_graph.StageViewTreeUpdates(std::move(updates));
    scene_graph.ProcessViewTreeUpdates();
  }

  ASSERT_EQ(scene_graph.view_tree().focus_chain().size(), 1u);
  EXPECT_EQ(scene_graph.view_tree().focus_chain()[0], parent_koid);

  auto status = scene_graph.RequestFocusChange(parent_koid, child_koid);
  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kErrorRequestCannotReceiveFocus);

  ASSERT_EQ(scene_graph.view_tree().focus_chain().size(), 1u);
  EXPECT_EQ(scene_graph.view_tree().focus_chain()[0], parent_koid);
}

class SceneGraphViewLookupTest : public SceneGraphTest {
 public:
  enum : uint32_t {
    kCompositorId = 20001,
    kLayerStackId,
    kLayerId,
    kSceneId,
    kCameraId,
    kRendererId,
    kEntityNodeId,
    kViewHolder1Id,
    kView1Id,
    kViewHolder2Id,
    kView2Id,
  };

  SceneGraphViewLookupTest() = default;

  void SetUp() override {
    SceneGraphTest::SetUp();
    SetUpScene();
  }

 private:
  void SetUpScene() {
    // Create the following Resource Graph:
    //
    // Compositor --> LayerStack --> Layer --> Renderer --> Camera --> Scene
    //                                                                  |
    //                                                                  v
    //                                                            EntityNode
    Apply(scenic::NewCreateCompositorCmd(kCompositorId));
    Apply(scenic::NewCreateLayerStackCmd(kLayerStackId));
    Apply(scenic::NewSetLayerStackCmd(kCompositorId, kLayerStackId));
    Apply(scenic::NewCreateLayerCmd(kLayerId));
    Apply(scenic::NewSetSizeCmd(kLayerId, {1024, 768}));
    Apply(scenic::NewAddLayerCmd(kLayerStackId, kLayerId));
    Apply(scenic::NewCreateSceneCmd(kSceneId));
    Apply(scenic::NewCreateCameraCmd(kCameraId, kSceneId));
    Apply(scenic::NewCreateRendererCmd(kRendererId));
    Apply(scenic::NewSetCameraCmd(kRendererId, kCameraId));
    Apply(scenic::NewSetRendererCmd(kLayerId, kRendererId));
    Apply(scenic::NewCreateEntityNodeCmd(kEntityNodeId));
    Apply(scenic::NewAddChildCmd(kSceneId, kEntityNodeId));
  }
};

TEST_F(SceneGraphViewLookupTest, SuccessfulLookup) {
  // Consider the following Resource Graph:
  //
  //                                  Scene
  //                                    |
  //                               EntityNode
  //                 /------------------|----------------\
  //                 |                                   |
  //                 v                                   v
  //             ViewHolder1                         ViewHolder2
  //              .`    |                             .`    |
  //            .`      v                           .`      v
  //        View1 ==> ViewNode1                   View2 ==> ViewNode2
  //
  // We should be able to locate View2 from |view2_ref|.
  //
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view2_token, view_holder2_token] = scenic::ViewTokenPair::New();
  auto [view2_ctrl_ref, view2_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view2_ref_for_creation;
  view2_ref.Clone(&view2_ref_for_creation);

  // Create Views.
  auto session_view1 = CreateSession();
  auto session_view2 = CreateSession();

  CommandContext cmds = CreateCommandContext();
  session_view1->ApplyCommand(&cmds,
                              scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), "view 1"));
  session_view2->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView2Id, std::move(view2_token), std::move(view2_ctrl_ref),
                                      std::move(view2_ref_for_creation), "view 2"));
  cmds.Flush();

  // Create other nodes.
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder2Id, std::move(view_holder2_token), "holder 2"));

  // Attach ViewHolder1 and ViewHolder2 to scene.
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder2Id));

  // Lookup View2 in session_view2's ResourceMap to verify that it is created.
  ViewPtr view2_ptr = session_view2->resources()->FindResource<View>(kView2Id);
  EXPECT_TRUE(view2_ptr);

  // Lookup View2 using View2's ViewRef.
  ViewPtr view_found = scene_graph()->LookupViewByViewRef(std::move(view2_ref));
  EXPECT_TRUE(view_found == view2_ptr);
}

TEST_F(SceneGraphViewLookupTest, LookupInvalidViewRef) {
  // Consider the following Resource Graph:
  //
  //                                  Scene
  //                                    |
  //                               EntityNode
  //                                   |
  //                                   v
  //                               ViewHolder1
  //                                .`    |
  //                              .`      v
  //                          View1 ==> ViewNode1
  //
  // If we provide an invalid ViewRef to LookupViewByViewRef() function, we
  // should get nullptr.
  //
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view1_ctrl_ref, view1_ref] = scenic::ViewRefPair::New();

  // Create Views.
  auto session_view1 = CreateSession();
  CommandContext cmds = CreateCommandContext();
  session_view1->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), std::move(view1_ctrl_ref),
                                      std::move(view1_ref), "view 1"));
  cmds.Flush();

  // Create other nodes.
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));

  // Attach ViewHolder1 to scene.
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));

  // Lookup View using an invalid ViewRef object. Should return nullptr.
  fuchsia::ui::views::ViewRef view_ref_invalid;
  ViewPtr view_found = scene_graph()->LookupViewByViewRef(std::move(view_ref_invalid));
  EXPECT_FALSE(view_found);
}

TEST_F(SceneGraphViewLookupTest, CannotFindUnattachedView) {
  // Consider the following Resource Graph:
  //
  //               Scene
  //                 |
  //             EntityNode
  //                 |
  //                 v
  //             ViewHolder1                         ViewHolder2
  //              .`    |                             .`    |
  //            .`      v                           .`      v
  //        View1 ==> ViewNode1                   View2 ==> ViewNode2
  //
  // We can locate View2 but we should not be able to find it in the Scene
  // Graph because it is not attached to any Scene.
  //
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view2_token, view_holder2_token] = scenic::ViewTokenPair::New();
  auto [view1_ctrl_ref, view1_ref] = scenic::ViewRefPair::New();
  auto [view2_ctrl_ref, view2_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view2_ref_for_creation;
  view2_ref.Clone(&view2_ref_for_creation);

  // Create Views.
  auto session_view1 = CreateSession();
  auto session_view2 = CreateSession();

  CommandContext cmds = CreateCommandContext();
  session_view1->ApplyCommand(&cmds,
                              scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), "view 1"));
  session_view2->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView2Id, std::move(view2_token), std::move(view2_ctrl_ref),
                                      std::move(view2_ref_for_creation), "view 2"));
  cmds.Flush();

  // Create other nodes.
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder2Id, std::move(view_holder2_token), "holder 2"));

  // Here we attach only ViewHolder1 to scene.
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));

  // Lookup View2 in session_view2's ResourceMap to verify that it is created.
  ViewPtr view2_ptr = session_view2->resources()->FindResource<View>(kView2Id);
  EXPECT_TRUE(view2_ptr);

  // Lookup View2 using View2's ViewRef; we should not find it.
  ViewPtr view_found = scene_graph()->LookupViewByViewRef(std::move(view2_ref));
  EXPECT_FALSE(view_found);
}

TEST_F(SceneGraphViewLookupTest, CannotFindDestroyedView) {
  // Consider the following Resource Graph:
  //
  //               Scene
  //                 |
  //             EntityNode
  //                 |
  //                 v
  //             ViewHolder1
  //              .`    |
  //            .`      v
  //        View1 ==> ViewNode1
  //
  // We first create these resources and then we destroy the View. Then
  // we should not be able to find it in the Scene Graph.
  //
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view1_ctrl_ref, view1_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view1_ref_for_creation;
  view1_ref.Clone(&view1_ref_for_creation);

  // Create Views.
  auto session_view1 = CreateSession();

  CommandContext cmds = CreateCommandContext();
  session_view1->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), std::move(view1_ctrl_ref),
                                      std::move(view1_ref_for_creation), "view 1"));
  cmds.Flush();

  // Create other nodes.
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));

  // Here we attach only ViewHolder1 to scene.
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));

  {
    // Lookup View1 in session_view1's ResourceMap to verify that it is created.
    ViewPtr view1_ptr = session_view1->resources()->FindResource<View>(kView1Id);
    EXPECT_TRUE(view1_ptr);

    // Verify that we can now lookup View1 using View1's ViewRef.
    fuchsia::ui::views::ViewRef view1_ref_for_lookup;
    view1_ref.Clone(&view1_ref_for_lookup);
    ViewPtr view_found = scene_graph()->LookupViewByViewRef(std::move(view1_ref_for_lookup));
    EXPECT_TRUE(view_found);
  }

  // --------------------------------------------------------------------------
  // Destroy the View.
  session_view1->ApplyCommand(&cmds, scenic::NewReleaseResourceCmd(kView1Id));
  cmds.Flush();

  {
    // Lookup View1 in session_view1's ResourceMap to verify that it is destroyed.
    ViewPtr view1_ptr = session_view1->resources()->FindResource<View>(kView1Id);
    EXPECT_FALSE(view1_ptr);

    // Verify that now we cannot lookup View1 using View1's ViewRef since it is
    // already destroyed.
    ViewPtr view_found = scene_graph()->LookupViewByViewRef(std::move(view1_ref));
    EXPECT_FALSE(view_found);
  }
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
