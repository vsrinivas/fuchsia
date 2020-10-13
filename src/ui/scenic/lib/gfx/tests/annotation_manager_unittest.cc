// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/annotation_manager.h"

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/eventpair.h>

#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/tests/view_tree_session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

// Test fixture which tests creating and handling of Annotation ViewHolders and
// Views.
//
// We use the ViewTreeSessionTest which supports handling multiple Sessions. The
// class internal session_ is used for setting up the main Scene defined in
// SetUpScene() which contains ViewHolders of client Views. For each other
// client View, a separate Session is created and registered in test body.
//
class AnnotationManagerTest : public ViewTreeSessionTest {
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
    kAnnotationViewId = 30001,
    kAnnotationShapeId
  };

  AnnotationManagerTest() = default;

  void SetUp() override {
    ViewTreeSessionTest::SetUp();
    SetUpScene();
    constexpr auto kAnnotationSessionId = 0U;

    // In GfxSystem, ViewTree updates in the annotation manager session are
    // manually staged by calling |StageViewTreeUpdates()|.
    // Here we create the |annotation_session| but not register it to better
    // simulate GfxSystem's behavior.
    auto annotation_session = CreateSession();
    annotation_manager_ = std::make_unique<AnnotationManager>(
        scene_graph_->GetWeakPtr(), view_linker_.get(), std::move(annotation_session));
  }

  void TearDown() override {
    ViewTreeSessionTest::TearDown();
    scene_graph_.reset();
  }

  bool Apply(fuchsia::ui::gfx::Command command) {
    bool result = ViewTreeSessionTest::Apply(std::move(command));
    StageAndUpdateViewTree(scene_graph_.get());
    return result;
  }

  SessionContext CreateSessionContext() override {
    SessionContext session_context = ViewTreeSessionTest::CreateSessionContext();
    FX_DCHECK(!view_linker_);
    FX_DCHECK(!scene_graph_);
    view_linker_ = std::make_unique<ViewLinker>();
    scene_graph_ = std::make_unique<SceneGraph>(context_provider_.context());
    session_context.view_linker = view_linker_.get();
    session_context.scene_graph = scene_graph_->GetWeakPtr();
    return session_context;
  }

  CommandContext CreateCommandContext() { return {.scene_graph = scene_graph_->GetWeakPtr()}; }

  SceneGraph* scene_graph() const { return scene_graph_.get(); }
  AnnotationManager* annotation_manager() const { return annotation_manager_.get(); }

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

  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<SceneGraph> scene_graph_;
  std::unique_ptr<ViewLinker> view_linker_;
  std::unique_ptr<AnnotationManager> annotation_manager_;
};

TEST_F(AnnotationManagerTest, SuccessfulLookup) {
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
  //                                               ||
  //                                               V
  //                                            Annotation
  //                                            ViewHolder
  //
  // We should be able to create an annotation ViewHolder for View2 given
  // |view2_ref|.

  // Create Views.
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view2_token, view_holder2_token] = scenic::ViewTokenPair::New();
  auto [view2_ctrl_ref, view2_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view2_ref_for_creation;
  view2_ref.Clone(&view2_ref_for_creation);

  auto session_view1 = CreateAndRegisterSession();
  auto session_view2 = CreateAndRegisterSession();
  CommandContext cmds = CreateCommandContext();
  session_view1->ApplyCommand(&cmds,
                              scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), "view 1"));
  session_view2->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView2Id, std::move(view2_token), std::move(view2_ctrl_ref),
                                      std::move(view2_ref_for_creation), "view 2"));

  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder2Id, std::move(view_holder2_token), "holder 2"));

  // Attach ViewHolder1 and ViewHolder2 to scene.
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder2Id));

  // Lookup View1 and View2 in the ResourceMap of their Sessions to verify that
  // it is created successfully.
  ViewPtr view1_ptr = session_view1->resources()->FindResource<View>(kView1Id);
  ViewPtr view2_ptr = session_view2->resources()->FindResource<View>(kView2Id);
  EXPECT_TRUE(view1_ptr);
  EXPECT_TRUE(view2_ptr && view2_ptr->GetViewNode());

  // Create Annotation ViewHolder for View2 only.
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();
  fuchsia::ui::views::ViewRef view2_ref_for_lookup;
  view2_ref.Clone(&view2_ref_for_lookup);

  bool created = false;
  bool handler_removed = false;
  constexpr AnnotationHandlerId kAnnotationHandlerId = 0;
  annotation_manager()->RegisterHandler(kAnnotationHandlerId,
                                        [&handler_removed](auto) { handler_removed = true; });
  annotation_manager()->RequestCreate(kAnnotationHandlerId, std::move(view2_ref_for_lookup),
                                      std::move(annotation_view_holder_token),
                                      [&created]() { created = true; });

  EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 0U);
  EXPECT_EQ(view2_ptr->annotation_view_holders().size(), 0U);
  EXPECT_EQ(view2_ptr->GetViewNode()->children().size(), 0U);

  EXPECT_FALSE(created);
  EXPECT_FALSE(handler_removed);
  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();
  ASSERT_TRUE(created);
  EXPECT_FALSE(handler_removed);

  EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 0U);
  EXPECT_EQ(view2_ptr->annotation_view_holders().size(), 1U);

  fxl::WeakPtr<ViewHolder> annotation_view_holder_weak_ptr =
      (*view2_ptr->annotation_view_holders().begin())->GetWeakPtr();
  EXPECT_EQ(view2_ptr->GetViewNode()->children().size(), 1U);
  EXPECT_EQ(view2_ptr->GetViewNode()->children().front().get(),
            annotation_view_holder_weak_ptr.get());
  EXPECT_EQ(annotation_view_holder_weak_ptr->parent(), view2_ptr->GetViewNode());
}

TEST_F(AnnotationManagerTest, InvalidAndNonExistentViewRef) {
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
  // We should not create an annotation ViewHolder if the |client_view_ref|
  // doesn't refer to any existing View, or the |client_view_ref| is invalid.

  // Create Views.
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view2_token, view_holder2_token] = scenic::ViewTokenPair::New();
  auto session_view1 = CreateAndRegisterSession();
  auto session_view2 = CreateAndRegisterSession();
  CommandContext cmds = CreateCommandContext();
  session_view1->ApplyCommand(&cmds,
                              scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), "view 1"));
  session_view2->ApplyCommand(&cmds,
                              scenic::NewCreateViewCmd(kView2Id, std::move(view2_token), "view 2"));

  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder2Id, std::move(view_holder2_token), "holder 2"));

  // Attach ViewHolder1 and ViewHolder2 to scene.
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder2Id));

  // Lookup View1 and View2 in the ResourceMap of their Sessions to verify that
  // it is created successfully.
  ViewPtr view1_ptr = session_view1->resources()->FindResource<View>(kView1Id);
  ViewPtr view2_ptr = session_view2->resources()->FindResource<View>(kView2Id);
  EXPECT_TRUE(view1_ptr);
  EXPECT_TRUE(view2_ptr);

  bool handler_removed = false;
  zx_status_t epitaph = ZX_OK;
  constexpr AnnotationHandlerId kAnnotationHandlerId = 0;
  annotation_manager()->RegisterHandler(kAnnotationHandlerId,
                                        [&handler_removed, &epitaph](zx_status_t handler_epitaph) {
                                          handler_removed = true;
                                          epitaph = handler_epitaph;
                                        });

  // Create Annotation ViewHolder using an new created ViewRef.
  {
    auto [annotation_view_ctrl_ref, annotation_view_ref] = scenic::ViewRefPair::New();
    auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();

    bool created = false;
    annotation_manager()->RequestCreate(kAnnotationHandlerId, std::move(annotation_view_ref),
                                        std::move(annotation_view_holder_token),
                                        [&created]() { created = true; });

    EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 0U);
    EXPECT_EQ(view2_ptr->annotation_view_holders().size(), 0U);

    EXPECT_FALSE(created);
    EXPECT_FALSE(handler_removed);
    annotation_manager()->FulfillCreateRequests();
    annotation_manager()->StageViewTreeUpdates();
    scene_graph()->ProcessViewTreeUpdates();
    ASSERT_FALSE(created);
    ASSERT_FALSE(handler_removed);

    EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 0U);
    EXPECT_EQ(view2_ptr->annotation_view_holders().size(), 0U);
  }

  // Create Annotation ViewHolder using an empty ViewRef.
  {
    auto annotation_view_ref = fuchsia::ui::views::ViewRef::New();
    auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();
    bool created = false;
    annotation_manager()->RequestCreate(kAnnotationHandlerId, std::move(*annotation_view_ref),
                                        std::move(annotation_view_holder_token),
                                        [&created]() { created = true; });

    EXPECT_FALSE(created);
    EXPECT_FALSE(handler_removed);
    annotation_manager()->FulfillCreateRequests();
    annotation_manager()->StageViewTreeUpdates();
    scene_graph()->ProcessViewTreeUpdates();
    ASSERT_FALSE(created);
    ASSERT_TRUE(handler_removed);
    ASSERT_EQ(epitaph, ZX_ERR_INVALID_ARGS);
  }
}

TEST_F(AnnotationManagerTest, NotFoundIfSessionDies) {
  // Consider the following Resource Graph:
  //
  //                                  Scene
  //                                    |
  //                               EntityNode
  //                                    |
  //                                    v
  //                                ViewHolder1
  //                                 .`    |
  //                               .`      v
  //                            View1 ==> ViewNode1
  //                              ||
  //                              V
  //                           Annotation
  //                           ViewHolder
  //
  // If we send a Annotation ViewHolder create request before View1 is
  // actually created, the request should be deferred until View1
  // exists.  If View1's session dies while the request is deferred, the
  // callback of the request should not be executed because no new annotation
  // ViewHolder is created.

  // Create Views.
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view1_ctrl_ref, view1_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view1_ref_for_creation;
  view1_ref.Clone(&view1_ref_for_creation);

  auto session_view1 = CreateAndRegisterSession();
  CommandContext cmds = CreateCommandContext();

  // Create ViewHolder1 and attach it to scene.
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));

  // Try creating Annotation ViewHolder for View1.
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();
  fuchsia::ui::views::ViewRef view1_ref_for_lookup;
  view1_ref.Clone(&view1_ref_for_lookup);

  bool created = false;
  bool handler_removed = false;
  zx_status_t handler_status = ZX_OK;
  constexpr AnnotationHandlerId kAnnotationHandlerId = 0;
  annotation_manager()->RegisterHandler(kAnnotationHandlerId,
                                        [&handler_status, &handler_removed](zx_status_t status) {
                                          handler_status = status;
                                          handler_removed = true;
                                        });
  annotation_manager()->RequestCreate(kAnnotationHandlerId, std::move(view1_ref_for_lookup),
                                      std::move(annotation_view_holder_token),
                                      [&created]() { created = true; });

  // If the View doesn't exist in ViewTree yet, the Annotation View creation
  // request is defered until View is created, but the handler (and the
  // request) should be still alive.
  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();
  ASSERT_FALSE(created);
  EXPECT_EQ(handler_status, ZX_OK);
  EXPECT_FALSE(handler_removed);

  // Now we create View1.
  session_view1->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), std::move(view1_ctrl_ref),
                                      std::move(view1_ref_for_creation), "view 1"));
  StageAndUpdateViewTree(scene_graph());

  // Lookup View1 in the ResourceMap of Sessions to verify that it is created successfully.
  // NOTE: The pointer must be temporary, so as not to keep the View alive after the Session is
  // destroyed below.
  {
    ViewPtr view1_ptr = session_view1->resources()->FindResource<View>(kView1Id);
    EXPECT_TRUE(view1_ptr && view1_ptr->GetViewNode());

    EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 0U);
    EXPECT_EQ(view1_ptr->GetViewNode()->children().size(), 0U);
  }

  // Destroy View1's session.
  session_view1.reset();

  // Try fulfilling the request again after View1 is created but the session is
  // dead.
  // The callback won't be triggered because there is no new Annotation
  // ViewHolder created; and the AnnotationRegistryHandler should be still
  // alive as this is not considered as a fatal error.
  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();
  EXPECT_FALSE(created);
  EXPECT_EQ(handler_status, ZX_OK);
  EXPECT_FALSE(handler_removed);
}

TEST_F(AnnotationManagerTest, HandlerAliveIfSessionDies) {
  // Consider the following Resource Graph:
  //
  //                                  Scene
  //                                    |
  //                               EntityNode
  //                                    |
  //                       ------------------------------
  //                       |                            |
  //                       v                            v
  //                 ViewHolder1                   ViewHolder2
  //                  .`    |                       .`    |
  //                .`      v                     .`      v
  //             View1 ==> ViewNode1           View2 ==> ViewNode2
  //               ||                            ||
  //               V                             V
  //            Annotation                    Annotation
  //            ViewHolder                    ViewHolder
  //
  // If we send a Annotation ViewHolder create request before View1 is
  // actually created, the request should be deferred until View1
  // exists.  If View1's session dies while the request is deferred, the
  // handler should be still alive and be able to handle other annotation
  // ViewHolder creation requests.

  // Create Views.
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view1_ctrl_ref, view1_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view1_ref_for_creation;
  view1_ref.Clone(&view1_ref_for_creation);

  auto [view2_token, view_holder2_token] = scenic::ViewTokenPair::New();
  auto [view2_ctrl_ref, view2_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view2_ref_for_creation;
  view2_ref.Clone(&view2_ref_for_creation);

  auto session_view1 = CreateAndRegisterSession();
  auto session_view2 = CreateAndRegisterSession();
  CommandContext cmds = CreateCommandContext();

  // Create ViewHolders and attach it to scene.
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));

  Apply(scenic::NewCreateViewHolderCmd(kViewHolder2Id, std::move(view_holder2_token), "holder 2"));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder2Id));

  // Try creating Annotation ViewHolder for View1.
  auto [annotation_view1_token, annotation_view_holder1_token] = scenic::ViewTokenPair::New();
  fuchsia::ui::views::ViewRef view1_ref_for_lookup;
  view1_ref.Clone(&view1_ref_for_lookup);

  // Try creating Annotation ViewHolder for View2.
  auto [annotation_view2_token, annotation_view_holder2_token] = scenic::ViewTokenPair::New();
  fuchsia::ui::views::ViewRef view2_ref_for_lookup;
  view2_ref.Clone(&view2_ref_for_lookup);

  bool annotation_view1_created = false;
  bool annotation_view2_created = false;
  bool handler_removed = false;
  zx_status_t handler_status = ZX_OK;
  constexpr AnnotationHandlerId kAnnotationHandlerId = 0;
  annotation_manager()->RegisterHandler(kAnnotationHandlerId,
                                        [&handler_status, &handler_removed](zx_status_t status) {
                                          handler_status = status;
                                          handler_removed = true;
                                        });
  annotation_manager()->RequestCreate(
      kAnnotationHandlerId, std::move(view1_ref_for_lookup),
      std::move(annotation_view_holder1_token),
      [&annotation_view1_created]() { annotation_view1_created = true; });

  // If the View doesn't exist in ViewTree yet, the Annotation View creation
  // request is defered until View is created, but the handler (and the
  // request) should be still alive.
  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();
  ASSERT_FALSE(annotation_view1_created);
  EXPECT_EQ(handler_status, ZX_OK);
  EXPECT_FALSE(handler_removed);

  // Now we create View1.
  session_view1->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), std::move(view1_ctrl_ref),
                                      std::move(view1_ref_for_creation), "view 1"));
  StageAndUpdateViewTree(scene_graph());

  // Lookup View1 in the ResourceMap of Sessions to verify that it is created successfully.
  // NOTE: The pointer must be temporary, so as not to keep the View alive after the Session is
  // destroyed below.
  {
    ViewPtr view1_ptr = session_view1->resources()->FindResource<View>(kView1Id);
    EXPECT_TRUE(view1_ptr && view1_ptr->GetViewNode());

    EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 0U);
    EXPECT_EQ(view1_ptr->GetViewNode()->children().size(), 0U);
  }

  // Destroy View1's session.
  session_view1.reset();

  // Try fulfilling the request again after View1 is created but the session is
  // dead.
  // The callback won't be triggered because there is no new Annotation
  // ViewHolder created; and the AnnotationRegistryHandler should be still
  // alive as this is not considered as a fatal error.
  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();
  EXPECT_FALSE(annotation_view1_created);
  EXPECT_EQ(handler_status, ZX_OK);
  EXPECT_FALSE(handler_removed);

  // Try creating another annotation ViewHolder to verify that the handler is
  // still alive.
  annotation_manager()->RequestCreate(
      kAnnotationHandlerId, std::move(view2_ref_for_lookup),
      std::move(annotation_view_holder2_token),
      [&annotation_view2_created]() { annotation_view2_created = true; });

  // Now we create View2.
  session_view2->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView2Id, std::move(view2_token), std::move(view2_ctrl_ref),
                                      std::move(view2_ref_for_creation), "view 2"));
  StageAndUpdateViewTree(scene_graph());

  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();
  EXPECT_TRUE(annotation_view2_created);
  EXPECT_EQ(handler_status, ZX_OK);
  EXPECT_FALSE(handler_removed);
}

TEST_F(AnnotationManagerTest, DelayCreateIfNotFound) {
  // Consider the following Resource Graph:
  //
  //                                  Scene
  //                                    |
  //                               EntityNode
  //                                    |
  //                                    v
  //                                ViewHolder1
  //                                 .`    |
  //                               .`      v
  //                            View1 ==> ViewNode1
  //                              ||
  //                              V
  //                           Annotation
  //                           ViewHolder
  //
  // If we send a Annotation ViewHolder create request before View1 is
  // actually created, the request should be deferred until View1
  // exists. The AnnotationHandler and the request should be still
  // alive during this period.

  // Create Views.
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view1_ctrl_ref, view1_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view1_ref_for_creation;
  view1_ref.Clone(&view1_ref_for_creation);

  auto session_view1 = CreateAndRegisterSession();
  CommandContext cmds = CreateCommandContext();

  // Create ViewHolder1 and attach it to scene.
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));

  // Try creating Annotation ViewHolder for View1.
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();
  fuchsia::ui::views::ViewRef view1_ref_for_lookup;
  view1_ref.Clone(&view1_ref_for_lookup);

  bool created = false;
  bool handler_removed = false;
  constexpr AnnotationHandlerId kAnnotationHandlerId = 0;
  annotation_manager()->RegisterHandler(kAnnotationHandlerId,
                                        [&handler_removed](auto) { handler_removed = true; });
  annotation_manager()->RequestCreate(kAnnotationHandlerId, std::move(view1_ref_for_lookup),
                                      std::move(annotation_view_holder_token),
                                      [&created]() { created = true; });

  // If the View doesn't exist in ViewTree yet, the Annotation View creation
  // request is defered until View is created, but the handler (and the
  // request) should be still alive.
  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();
  ASSERT_FALSE(created);
  EXPECT_FALSE(handler_removed);

  // Now we create View1.
  session_view1->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), std::move(view1_ctrl_ref),
                                      std::move(view1_ref_for_creation), "view 1"));
  StageAndUpdateViewTree(scene_graph());

  // Lookup View1 in the ResourceMap of Sessions to verify that it is created successfully.
  ViewPtr view1_ptr = session_view1->resources()->FindResource<View>(kView1Id);
  EXPECT_TRUE(view1_ptr && view1_ptr->GetViewNode());

  EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 0U);
  EXPECT_EQ(view1_ptr->GetViewNode()->children().size(), 0U);

  // Try fulfilling the request again after View1 is created. This time it
  // should succeed.
  EXPECT_FALSE(created);
  EXPECT_FALSE(handler_removed);
  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();
  ASSERT_TRUE(created);
  EXPECT_FALSE(handler_removed);

  EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 1U);

  fxl::WeakPtr<ViewHolder> annotation_view_holder_weak_ptr =
      (*view1_ptr->annotation_view_holders().begin())->GetWeakPtr();
  EXPECT_EQ(view1_ptr->GetViewNode()->children().size(), 1U);
  EXPECT_EQ(view1_ptr->GetViewNode()->children().front().get(),
            annotation_view_holder_weak_ptr.get());
  EXPECT_EQ(annotation_view_holder_weak_ptr->parent(), view1_ptr->GetViewNode());
}

TEST_F(AnnotationManagerTest, LinkerTest_AnnotationViewCreatedFirst) {
  // Consider the following Resource Graph:
  //
  //      Scene -----> EntityNode ----------\
  //                                        v
  //                                    ViewHolder1
  //    = = = = = = = = = = = = = = = =  .` = =| = = = = = = = = = = = =
  //    . Session_View1                .`      v                       .
  //    .                            View1 ==> ViewNode1               .
  //    .                             ||                               .
  //    .                             V                                .
  //    .                          Annotation                          .
  //    .                          ViewHolder ------\                  .
  //    .                              .`            \                 .
  //    = = = = = = = = = = = = = =  .` = = = = = = = \ = = = = = = = =
  //    . Session_Annotation       .`                 V                .
  //    .                     Annotation View ==> Annotation ViewNode  .
  //    .                                                              .
  //    = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =
  //
  // No matter if we create Annotation ViewHolder first or create Annotation
  // View first, the ViewHolder should always be able to link with the
  // Annotation View.
  //
  // In this test case we first create Annotation View, then use the Annotation
  // API to create Annotation ViewHolder, and verify if they are linked with
  // each other.

  // Create View1 and ViewHolder1 and attach it to the scene1.
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view1_ctrl_ref, view1_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view1_ref_for_creation;
  view1_ref.Clone(&view1_ref_for_creation);

  auto session_view1 = CreateAndRegisterSession();
  CommandContext cmds = CreateCommandContext();
  session_view1->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), std::move(view1_ctrl_ref),
                                      std::move(view1_ref_for_creation), "view 1"));
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));

  // Lookup View1 in the ResourceMap to verify that it is created successfully.
  ViewPtr view1_ptr = session_view1->resources()->FindResource<View>(kView1Id);
  EXPECT_TRUE(view1_ptr);

  // Create Annotation View.
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();

  auto session_annotation = CreateAndRegisterSession();
  session_annotation->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kAnnotationViewId, std::move(annotation_view_token),
                                      "annotation view"));

  // Lookup Annotation View in the ResourceMap to verify that it is created
  // successfully.
  ViewPtr annotation_view_ptr =
      session_annotation->resources()->FindResource<View>(kAnnotationViewId);
  EXPECT_TRUE(annotation_view_ptr);

  // Create Annotation ViewHolder.
  bool created = false;
  constexpr AnnotationHandlerId kAnnotationHandlerId = 0;
  annotation_manager()->RegisterHandler(kAnnotationHandlerId, [](auto) {});
  annotation_manager()->RequestCreate(kAnnotationHandlerId, std::move(view1_ref),
                                      std::move(annotation_view_holder_token),
                                      [&created]() { created = true; });

  EXPECT_FALSE(created);
  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();
  ASSERT_TRUE(created);

  // Lookup Annotation ViewHolder in View1.
  EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 1U);
  ViewHolderPtr annotation_view_holder_ptr = *(view1_ptr->annotation_view_holders().begin());
  EXPECT_TRUE(annotation_view_holder_ptr);

  EXPECT_EQ(annotation_view_holder_ptr->view(), annotation_view_ptr.get());
  EXPECT_EQ(annotation_view_ptr->view_holder(), annotation_view_holder_ptr.get());
  EXPECT_TRUE(annotation_view_ptr->GetViewNode());
  EXPECT_EQ(annotation_view_ptr->GetViewNode()->parent(), annotation_view_holder_ptr.get());

  annotation_view_holder_ptr = nullptr;
  annotation_view_ptr = nullptr;
}

TEST_F(AnnotationManagerTest, LinkerTest_AnnotationViewHolderCreatedFirst) {
  // Consider the following Resource Graph:
  //
  //      Scene -----> EntityNode ----------\
  //                                        v
  //                                    ViewHolder1
  //    = = = = = = = = = = = = = = = =  .` = =| = = = = = = = = = = = =
  //    . Session_View1                .`      v                       .
  //    .                            View1 ==> ViewNode1               .
  //    .                             ||                               .
  //    .                             V                                .
  //    .                          Annotation                          .
  //    .                          ViewHolder ------\                  .
  //    .                              .`            \                 .
  //    = = = = = = = = = = = = = =  .` = = = = = = = \ = = = = = = = =
  //    . Session_Annotation       .`                 V                .
  //    .                     Annotation View ==> Annotation ViewNode  .
  //    .                                                              .
  //    = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =
  //
  // No matter if we create Annotation ViewHolder first or create Annotation
  // View first, the ViewHolder should always be able to link with the
  // Annotation View.
  //
  // In this test case we first create Annotation ViewHolder using the
  // Annotation API, then we create Annotation View in Annotation session,
  // and verify if they are linked with each other correctly.

  // Create View1 and ViewHolder1 and attach it to the scene1.
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view1_ctrl_ref, view1_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view1_ref_for_creation;
  view1_ref.Clone(&view1_ref_for_creation);

  auto session_view1 = CreateAndRegisterSession();
  CommandContext cmds = CreateCommandContext();
  session_view1->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), std::move(view1_ctrl_ref),
                                      std::move(view1_ref_for_creation), "view 1"));
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));

  // Lookup View1 in the ResourceMap to verify that it is created successfully.
  ViewPtr view1_ptr = session_view1->resources()->FindResource<View>(kView1Id);
  EXPECT_TRUE(view1_ptr);

  // Create Annotation View Holder.
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();
  bool created = false;
  constexpr AnnotationHandlerId kAnnotationHandlerId = 0;
  annotation_manager()->RegisterHandler(kAnnotationHandlerId, [](auto) {});
  annotation_manager()->RequestCreate(kAnnotationHandlerId, std::move(view1_ref),
                                      std::move(annotation_view_holder_token),
                                      [&created]() { created = true; });

  // Create Annotation View.
  auto session_annotation = CreateAndRegisterSession();
  session_annotation->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kAnnotationViewId, std::move(annotation_view_token),
                                      "annotation view"));

  // Lookup Annotation View in the ResourceMap to verify that it is created
  // successfully.
  ViewPtr annotation_view_ptr =
      session_annotation->resources()->FindResource<View>(kAnnotationViewId);
  EXPECT_TRUE(annotation_view_ptr);

  EXPECT_FALSE(created);
  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();
  ASSERT_TRUE(created);

  // Lookup Annotation ViewHolder in View1.
  EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 1U);
  ViewHolderPtr annotation_view_holder_ptr = *(view1_ptr->annotation_view_holders().begin());
  EXPECT_TRUE(annotation_view_holder_ptr);

  EXPECT_EQ(annotation_view_holder_ptr->view(), annotation_view_ptr.get());
  EXPECT_EQ(annotation_view_ptr->view_holder(), annotation_view_holder_ptr.get());

  annotation_view_holder_ptr = nullptr;
  annotation_view_ptr = nullptr;
}

TEST_F(AnnotationManagerTest, RemoveAnnotationView) {
  // Consider the following Resource Graph:
  //
  //      Scene -----> EntityNode ----------\
  //                                        v
  //                                    ViewHolder1
  //    = = = = = = = = = = = = = = = =  .` = =| = = = = = = = = = = = =
  //    . Session_View1                .`      v                       .
  //    .                            View1 ==> ViewNode1               .
  //    .                             ||                               .
  //    .                             V                                .
  //    .                          Annotation                          .
  //    .                          ViewHolder ------\                  .
  //    .                              .`            \                 .
  //    = = = = = = = = = = = = = =  .` = = = = = = = \ = = = = = = = =
  //    . Session_Annotation       .`                 V                .
  //    .                     Annotation View ==> Annotation ViewNode  .
  //    .                                                              .
  //    = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =
  //
  // If the Annotation View is removed from the ResourceMap, the Annotation
  // ViewHolder will be automatically removed from the View1 as well.
  //

  // Create View1 and ViewHolder1 and attach it to the scene1.
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view1_ctrl_ref, view1_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view1_ref_for_creation;
  view1_ref.Clone(&view1_ref_for_creation);

  auto session_view1 = CreateAndRegisterSession();
  CommandContext cmds = CreateCommandContext();
  session_view1->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), std::move(view1_ctrl_ref),
                                      std::move(view1_ref_for_creation), "view 1"));
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));

  // Lookup View1 in the ResourceMap to verify that it is created successfully.
  ViewPtr view1_ptr = session_view1->resources()->FindResource<View>(kView1Id);
  EXPECT_TRUE(view1_ptr);
  EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 0U);

  // Create Annotation View Holder.
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();
  constexpr AnnotationHandlerId kAnnotationHandlerId = 0;
  annotation_manager()->RegisterHandler(kAnnotationHandlerId, [](auto) {});
  annotation_manager()->RequestCreate(kAnnotationHandlerId, std::move(view1_ref),
                                      std::move(annotation_view_holder_token), []() {});
  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();

  // Create Annotation View.
  auto session_annotation = CreateAndRegisterSession();
  session_annotation->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kAnnotationViewId, std::move(annotation_view_token),
                                      "annotation view"));

  // Lookup Annotation View in the ResourceMap to verify that it is created
  // successfully.
  fxl::WeakPtr<View> annotation_view_weak_ptr =
      session_annotation->resources()->FindResource<View>(kAnnotationViewId)->GetWeakPtr();
  EXPECT_TRUE(annotation_view_weak_ptr);

  EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 1U);
  fxl::WeakPtr<ViewHolder> annotation_view_holder_weak_ptr =
      (*view1_ptr->annotation_view_holders().begin())->GetWeakPtr();
  EXPECT_TRUE(annotation_view_holder_weak_ptr);

  // Destroy Annotation View.
  session_annotation->ApplyCommand(&cmds, scenic::NewReleaseResourceCmd(kAnnotationViewId));
  EXPECT_FALSE(annotation_view_weak_ptr);
  EXPECT_FALSE(annotation_view_holder_weak_ptr);
  EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 0U);
}

TEST_F(AnnotationManagerTest, RemoveClientView) {
  // Consider the following Resource Graph:
  //
  //      Scene -----> EntityNode ----------\
  //                                        v
  //                                    ViewHolder1
  //    = = = = = = = = = = = = = = = =  .` = =| = = = = = = = = = = = =
  //    . Session_View1                .`      v                       .
  //    .                            View1 ==> ViewNode1               .
  //    .                             ||                               .
  //    .                             V                                .
  //    .                          Annotation                          .
  //    .                          ViewHolder ------\                  .
  //    .                              .`            \                 .
  //    = = = = = = = = = = = = = =  .` = = = = = = = \ = = = = = = = =
  //    . Session_Annotation       .`                 V                .
  //    .                     Annotation View ==> Annotation ViewNode  .
  //    .                                                              .
  //    = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =
  //
  // If the Client View (View1) is removed from the ResourceMap, the Annotation
  // ViewHolder will be removed, and the link between Annotation ViewHolder
  // and Annotation View will be destroyed. The Annotation View is still
  // available, and Session_Annotation will receive an ViewHolderDisconnected
  // event so that it could delete the Annotation View and all related
  // resources.
  //

  // Create View1 and ViewHolder1 and attach it to the scene1.
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view1_ctrl_ref, view1_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view1_ref_for_creation;
  view1_ref.Clone(&view1_ref_for_creation);

  auto session_view1 = CreateAndRegisterSession();
  CommandContext cmds = CreateCommandContext();
  session_view1->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), std::move(view1_ctrl_ref),
                                      std::move(view1_ref_for_creation), "view 1"));
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));

  // Lookup View1 in the ResourceMap to verify that it is created successfully.
  fxl::WeakPtr<View> view1_weak_ptr =
      session_view1->resources()->FindResource<View>(kView1Id)->GetWeakPtr();
  EXPECT_TRUE(view1_weak_ptr);
  EXPECT_EQ(view1_weak_ptr->annotation_view_holders().size(), 0U);

  // Create Annotation View Holder.
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();
  constexpr AnnotationHandlerId kAnnotationHandlerId = 0;
  annotation_manager()->RegisterHandler(kAnnotationHandlerId, [](auto) {});
  annotation_manager()->RequestCreate(kAnnotationHandlerId, std::move(view1_ref),
                                      std::move(annotation_view_holder_token), []() {});
  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();

  // Create Annotation View.
  auto session_annotation = CreateAndRegisterSession();
  session_annotation->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kAnnotationViewId, std::move(annotation_view_token),
                                      "annotation view"));

  // Lookup Annotation View in the ResourceMap to verify that it is created
  // successfully.
  fxl::WeakPtr<View> annotation_view_weak_ptr =
      session_annotation->resources()->FindResource<View>(kAnnotationViewId)->GetWeakPtr();
  EXPECT_TRUE(annotation_view_weak_ptr);

  EXPECT_EQ(view1_weak_ptr->annotation_view_holders().size(), 1U);
  fxl::WeakPtr<ViewHolder> annotation_view_holder_weak_ptr =
      (*view1_weak_ptr->annotation_view_holders().begin())->GetWeakPtr();
  EXPECT_TRUE(annotation_view_holder_weak_ptr);

  // Destroy Client View.
  ClearEvents();
  session_view1->ApplyCommand(&cmds, scenic::NewReleaseResourceCmd(kView1Id));
  EXPECT_FALSE(view1_weak_ptr);
  EXPECT_FALSE(annotation_view_holder_weak_ptr);
  EXPECT_TRUE(annotation_view_weak_ptr);

  // There should be only one ViewHolderDisconnected events.
  bool annotation_view_holder_disconnected = false;
  size_t view_holder_disconnected_events_count = 0U;
  for (const auto& scenic_event : events()) {
    if (scenic_event.is_gfx() && scenic_event.gfx().is_view_holder_disconnected()) {
      ++view_holder_disconnected_events_count;
      if (scenic_event.gfx().view_holder_disconnected().view_id == kAnnotationViewId) {
        annotation_view_holder_disconnected = true;
      }
    }
  }
  EXPECT_TRUE(annotation_view_holder_disconnected);
  EXPECT_EQ(view_holder_disconnected_events_count, 1U);
}

TEST_F(AnnotationManagerTest, RemoveClientViewHolder) {
  // Consider the following Resource Graph:
  //
  //      Scene -----> EntityNode ----------\
  //                                        v
  //                                    ViewHolder1
  //    = = = = = = = = = = = = = = = =  .` = =| = = = = = = = = = = = =
  //    . Session_View1                .`      v                       .
  //    .                            View1 ==> ViewNode1               .
  //    .                             ||                               .
  //    .                             V                                .
  //    .                          Annotation                          .
  //    .                          ViewHolder ------\                  .
  //    .                              .`            \                 .
  //    = = = = = = = = = = = = = =  .` = = = = = = = \ = = = = = = = =
  //    . Session_Annotation       .`                 V                .
  //    .                     Annotation View ==> Annotation ViewNode  .
  //    .                                                              .
  //    = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =
  //
  // If the client View is detached from the SceneGraph (e.g. the ViewHolder
  // is released), the Annotation ViewHolder will be still a child of the
  // client ViewNode, but it will be removed from the SceneGraph.

  // Create View1 and ViewHolder1 and attach it to the scene1.
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view1_ctrl_ref, view1_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view1_ref_for_creation;
  view1_ref.Clone(&view1_ref_for_creation);

  auto session_view1 = CreateAndRegisterSession();
  CommandContext cmds = CreateCommandContext();
  session_view1->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), std::move(view1_ctrl_ref),
                                      std::move(view1_ref_for_creation), "view 111"));
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token),
                                       "holder "
                                       "111"));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));

  // Lookup View1 in the ResourceMap to verify that it is created successfully.
  fxl::WeakPtr<View> view1_weak_ptr =
      session_view1->resources()->FindResource<View>(kView1Id)->GetWeakPtr();
  EXPECT_TRUE(view1_weak_ptr);
  EXPECT_EQ(view1_weak_ptr->annotation_view_holders().size(), 0U);

  // Create Annotation View Holder.
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();
  constexpr AnnotationHandlerId kAnnotationHandlerId = 0;
  annotation_manager()->RegisterHandler(kAnnotationHandlerId, [](auto) {});
  annotation_manager()->RequestCreate(kAnnotationHandlerId, std::move(view1_ref),
                                      std::move(annotation_view_holder_token), []() {});
  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();

  // Create Annotation View.
  auto session_annotation = CreateAndRegisterSession();
  session_annotation->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kAnnotationViewId, std::move(annotation_view_token),
                                      "annotation view 111"));
  StageAndUpdateViewTree(scene_graph());

  // Lookup Annotation View in the ResourceMap to verify that it is created
  // successfully.
  fxl::WeakPtr<View> annotation_view_weak_ptr =
      session_annotation->resources()->FindResource<View>(kAnnotationViewId)->GetWeakPtr();
  EXPECT_TRUE(annotation_view_weak_ptr);

  EXPECT_EQ(view1_weak_ptr->annotation_view_holders().size(), 1U);
  fxl::WeakPtr<ViewHolder> annotation_view_holder_weak_ptr =
      (*view1_weak_ptr->annotation_view_holders().begin())->GetWeakPtr();
  EXPECT_TRUE(annotation_view_holder_weak_ptr);

  // Destroy client ViewHolder.
  ClearEvents();
  Apply(scenic::NewDetachCmd(kViewHolder1Id));
  Apply(scenic::NewReleaseResourceCmd(kViewHolder1Id));

  // Annotation ViewHolder should be still a child of client View.
  EXPECT_TRUE(view1_weak_ptr && annotation_view_weak_ptr && annotation_view_holder_weak_ptr);
  EXPECT_EQ(view1_weak_ptr->annotation_view_holders().size(), 1U);
  EXPECT_EQ(view1_weak_ptr->annotation_view_holders().begin()->get(),
            annotation_view_holder_weak_ptr.get());
  EXPECT_EQ(annotation_view_holder_weak_ptr->view(), annotation_view_weak_ptr.get());
  EXPECT_EQ(annotation_view_holder_weak_ptr.get(), annotation_view_weak_ptr->view_holder());

  // There should be only one ViewHolderDisconnected events.
  bool client_view_holder_disconnected = false;
  size_t view_holder_disconnected_events_count = 0U;
  for (const auto& scenic_event : events()) {
    if (scenic_event.is_gfx() && scenic_event.gfx().is_view_holder_disconnected()) {
      ++view_holder_disconnected_events_count;
      if (scenic_event.gfx().view_holder_disconnected().view_id == kView1Id) {
        client_view_holder_disconnected = true;
      }
    }
  }
  EXPECT_TRUE(client_view_holder_disconnected);
  EXPECT_EQ(view_holder_disconnected_events_count, 1U);
}

TEST_F(AnnotationManagerTest, ViewPropertiesPropagation) {
  // Consider the following Resource Graph:
  //
  //      Scene -----> EntityNode ----------\
  //                                        v
  //                                    ViewHolder1
  //    = = = = = = = = = = = = = = = =  .` = =| = = = = = = = = = = = =
  //    . Session_View1                .`      v                       .
  //    .                            View1 ==> ViewNode1               .
  //    .                             ||                               .
  //    .                             V                                .
  //    .                          Annotation                          .
  //    .                          ViewHolder ------\                  .
  //    .                              .`            \                 .
  //    = = = = = = = = = = = = = =  .` = = = = = = = \ = = = = = = = =
  //    . Session_Annotation       .`                 V                .
  //    .                     Annotation View ==> Annotation ViewNode  .
  //    .                                                              .
  //    = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =
  //
  // When the Annotation ViewHolder is created, it should have the same
  // ViewProperties (except for focus_change = false) as the client ViewHolder.
  //
  // When client ViewHolder chnages its ViewProperties, the same properties
  // should be propagated to the annotation ViewHolder as well.
  //

  // Create View1 and ViewHolder1 and attach it to the scene1.
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view1_ctrl_ref, view1_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view1_ref_for_creation;
  view1_ref.Clone(&view1_ref_for_creation);

  auto session_view1 = CreateAndRegisterSession();
  CommandContext cmds = CreateCommandContext();
  session_view1->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), std::move(view1_ctrl_ref),
                                      std::move(view1_ref_for_creation), "view 1"));
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));

  // Set up initial View properties.
  fuchsia::ui::gfx::ViewProperties view_properties = {.bounding_box =
                                                          {
                                                              .min = {0, 0, 0},
                                                              .max = {600, 400, 1},
                                                          },
                                                      .inset_from_min = {10, 10, 0},
                                                      .inset_from_max = {10, 10, 0},
                                                      .focus_change = true,
                                                      .downward_input = true};
  Apply(scenic::NewSetViewPropertiesCmd(kViewHolder1Id, view_properties));

  // Lookup View1 in the ResourceMap to verify that it is created successfully.
  ViewPtr view1_ptr = session_view1->resources()->FindResource<View>(kView1Id);
  EXPECT_TRUE(view1_ptr);
  EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 0U);

  // Create Annotation View Holder.
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();
  constexpr AnnotationHandlerId kAnnotationHandlerId = 0;
  annotation_manager()->RegisterHandler(kAnnotationHandlerId, [](auto) {});
  annotation_manager()->RequestCreate(kAnnotationHandlerId, std::move(view1_ref),
                                      std::move(annotation_view_holder_token), []() {});
  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();

  // Create Annotation View.
  ClearEvents();
  auto session_annotation = CreateAndRegisterSession();
  session_annotation->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kAnnotationViewId, std::move(annotation_view_token),
                                      "annotation view"));

  // Verify that Annotation ViewHolder is created correctly.
  EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 1U);
  fxl::WeakPtr<ViewHolder> annotation_view_holder_weak_ptr =
      (*view1_ptr->annotation_view_holders().begin())->GetWeakPtr();
  EXPECT_TRUE(annotation_view_holder_weak_ptr);

  // Verify the Annotation ViewHolder has correct properties.
  auto annotation_view_holder_properties = annotation_view_holder_weak_ptr->GetViewProperties();

  EXPECT_TRUE(
      fidl::Equals(annotation_view_holder_properties.bounding_box, view_properties.bounding_box));
  EXPECT_TRUE(fidl::Equals(annotation_view_holder_properties.inset_from_min,
                           view_properties.inset_from_min));
  EXPECT_TRUE(fidl::Equals(annotation_view_holder_properties.inset_from_max,
                           view_properties.inset_from_max));
  EXPECT_EQ(annotation_view_holder_properties.focus_change, false);

  // Verify that the session receives ViewPropertiesChangedEvent when creating
  // the Annotation View.
  auto view_properties_changed_event =
      std::find_if(events().begin(), events().end(), [](const fuchsia::ui::scenic::Event& event) {
        return event.is_gfx() && event.gfx().is_view_properties_changed() &&
               event.gfx().view_properties_changed().view_id == kAnnotationViewId;
      });
  ASSERT_NE(view_properties_changed_event, events().end());
  EXPECT_TRUE(
      fidl::Equals(view_properties_changed_event->gfx().view_properties_changed().properties,
                   annotation_view_holder_properties));

  // Modify the ViewProperties of View1.
  ClearEvents();
  view_properties.bounding_box = {.min = {0, 0, 0}, .max = {300, 200, 50}};
  view_properties.inset_from_min = {20, 20, 0};
  view_properties.inset_from_max = {20, 20, 0};
  Apply(scenic::NewSetViewPropertiesCmd(kViewHolder1Id, view_properties));

  // Verify the Annotation ViewHolder has correct properties.
  annotation_view_holder_properties = annotation_view_holder_weak_ptr->GetViewProperties();

  EXPECT_TRUE(
      fidl::Equals(annotation_view_holder_properties.bounding_box, view_properties.bounding_box));
  EXPECT_TRUE(fidl::Equals(annotation_view_holder_properties.inset_from_min,
                           view_properties.inset_from_min));
  EXPECT_TRUE(fidl::Equals(annotation_view_holder_properties.inset_from_max,
                           view_properties.inset_from_max));
  EXPECT_EQ(annotation_view_holder_properties.focus_change, false);

  // Verify that the session receives ViewPropertiesChangedEvent when updating
  // the ViewProperties of ViewHolder1.
  view_properties_changed_event =
      std::find_if(events().begin(), events().end(), [](const fuchsia::ui::scenic::Event& event) {
        return event.is_gfx() && event.gfx().is_view_properties_changed() &&
               event.gfx().view_properties_changed().view_id == kAnnotationViewId;
      });
  ASSERT_NE(view_properties_changed_event, events().end());
  EXPECT_TRUE(
      fidl::Equals(view_properties_changed_event->gfx().view_properties_changed().properties,
                   annotation_view_holder_properties));
}

TEST_F(AnnotationManagerTest, GlobalTransformPropagation) {
  // Consider the following Resource Graph:
  //
  //      Scene -----> EntityNode ----------\
  //                                        v
  //                                    ViewHolder1
  //    = = = = = = = = = = = = = = = =  .` = =| = = = = = = = = = = = =
  //    . Session_View1                .`      v                       .
  //    .                            View1 ==> ViewNode1               .
  //    .                             ||                               .
  //    .                             V                                .
  //    .                          Annotation                          .
  //    .                          ViewHolder ------\                  .
  //    .                              .`            \                 .
  //    = = = = = = = = = = = = = =  .` = = = = = = = \ = = = = = = = =
  //    . Session_Annotation       .`                 V                .
  //    .                     Annotation View ==> Annotation ViewNode  .
  //    .                                                              .
  //    = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =
  //
  // When the Annotation ViewHolder is created, it should have the same global
  // transformation (including translation and rotation) as the client
  // ViewHolder.
  //
  // When client ViewHolder's transformation matrix changes, the same change
  // should be made to the annotation ViewHolder as well.
  //

  // Create View1 and ViewHolder1 and attach it to the scene1.
  auto [view1_token, view_holder1_token] = scenic::ViewTokenPair::New();
  auto [view1_ctrl_ref, view1_ref] = scenic::ViewRefPair::New();
  fuchsia::ui::views::ViewRef view1_ref_for_creation;
  view1_ref.Clone(&view1_ref_for_creation);

  auto session_view1 = CreateAndRegisterSession();
  CommandContext cmds = CreateCommandContext();
  session_view1->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kView1Id, std::move(view1_token), std::move(view1_ctrl_ref),
                                      std::move(view1_ref_for_creation), "view 1"));
  Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder1_token), "holder 1"));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolder1Id));

  // Set up initial View translation and rotation.
  std::array<float, 3> translation = {100, 200, 0};
  glm::quat glm_quat = glm::angleAxis(1.0f, glm::vec3(0, 0, 1));
  std::array<float, 4> quaternion = {glm_quat.x, glm_quat.y, glm_quat.z, glm_quat.w};

  Apply(scenic::NewSetTranslationCmd(kViewHolder1Id, translation));
  Apply(scenic::NewSetRotationCmd(kViewHolder1Id, quaternion));

  // Lookup View1 in the ResourceMap to verify that it is created successfully.
  ViewPtr view1_ptr = session_view1->resources()->FindResource<View>(kView1Id);
  EXPECT_TRUE(view1_ptr);
  EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 0U);

  // Create Annotation View Holder.
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();
  constexpr AnnotationHandlerId kAnnotationHandlerId = 0;
  annotation_manager()->RegisterHandler(kAnnotationHandlerId, [](auto) {});
  annotation_manager()->RequestCreate(kAnnotationHandlerId, std::move(view1_ref),
                                      std::move(annotation_view_holder_token), []() {});
  annotation_manager()->FulfillCreateRequests();
  annotation_manager()->StageViewTreeUpdates();
  scene_graph()->ProcessViewTreeUpdates();

  // Create Annotation View.
  auto session_annotation = CreateAndRegisterSession();
  session_annotation->ApplyCommand(
      &cmds, scenic::NewCreateViewCmd(kAnnotationViewId, std::move(annotation_view_token),
                                      "annotation view"));

  // Verify that Annotation ViewHolder is created correctly.
  EXPECT_EQ(view1_ptr->annotation_view_holders().size(), 1U);
  fxl::WeakPtr<ViewHolder> annotation_view_holder_weak_ptr =
      (*view1_ptr->annotation_view_holders().begin())->GetWeakPtr();
  EXPECT_TRUE(annotation_view_holder_weak_ptr);

  // Verify the Annotation ViewHolder has correct transform matrix.
  EXPECT_EQ(view1_ptr->view_holder()->GetGlobalTransform(),
            annotation_view_holder_weak_ptr->GetGlobalTransform());

  // Modify the translation and rotation of ViewHolder1.
  translation = {-100, -200, 0};
  glm_quat = glm::angleAxis(2.0f, glm::vec3(0, 1, 0));
  quaternion = {glm_quat.x, glm_quat.y, glm_quat.z, glm_quat.w};

  Apply(scenic::NewSetTranslationCmd(kViewHolder1Id, translation));
  Apply(scenic::NewSetRotationCmd(kViewHolder1Id, quaternion));

  // Verify the Annotation ViewHolder has correct transform matrix.
  EXPECT_EQ(view1_ptr->view_holder()->GetGlobalTransform(),
            annotation_view_holder_weak_ptr->GetGlobalTransform());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
