
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/view.h"

#include <lib/async/cpp/task.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/eventpair.h>

#include "src/ui/scenic/lib/gfx/resources/nodes/entity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/view_node.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"

namespace scenic_impl {
namespace gfx {

namespace test {

namespace {

ViewHolderPtr NewAnnotationViewHolder(
    Session* session, ViewLinker* view_linker,
    fuchsia::ui::views::ViewHolderToken annotation_view_holder_token) {
  std::ostringstream annotation_debug_name;
  annotation_debug_name << "Annotation ViewHolder [Test]";
  ViewHolderPtr annotation_view_holder = fxl::MakeRefCounted<ViewHolder>(
      session, session->id(), /* node_id */ 0U,
      /* is_annotation */ true, annotation_debug_name.str(), session->shared_error_reporter(),
      session->view_tree_updater());
  // Set hit test behavior to kSuppress so it will suppress all hit testings.
  annotation_view_holder->SetHitTestBehavior(fuchsia::ui::gfx::HitTestBehavior::kSuppress);
  // Set up link with annotation View.
  ViewLinker::ExportLink link = view_linker->CreateExport(
      annotation_view_holder.get(), std::move(annotation_view_holder_token.value),
      session->error_reporter());
  FX_CHECK(link.valid()) << "Cannot setup link with annotation View!";
  annotation_view_holder->Connect(std::move(link));
  return annotation_view_holder;
}

void VerifyViewState(const fuchsia::ui::scenic::Event& event, bool is_rendering_expected) {
  EXPECT_EQ(fuchsia::ui::scenic::Event::Tag::kGfx, event.Which());
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewStateChanged, event.gfx().Which());
  const ::fuchsia::ui::gfx::ViewState& view_state = event.gfx().view_state_changed().state;
  EXPECT_EQ(is_rendering_expected, view_state.is_rendering);
}

}  // namespace

class ViewTest : public SessionTest {
 public:
  ViewTest() {}

  void TearDown() override {
    SessionTest::TearDown();

    view_linker_.reset();
  }

  SessionContext CreateSessionContext() override {
    SessionContext session_context = SessionTest::CreateSessionContext();

    FX_DCHECK(!view_linker_);

    view_linker_ = std::make_unique<ViewLinker>();
    session_context.view_linker = view_linker_.get();

    return session_context;
  }

  std::unique_ptr<ViewLinker> view_linker_;
};

// TODO(fxbug.dev/24571): Only seems to die in debug builds.
TEST_F(ViewTest, DISABLED_CreateViewWithBadTokenDies) {
  EXPECT_DEATH_IF_SUPPORTED(Apply(scenic::NewCreateViewCmd(1, fuchsia::ui::views::ViewToken(), "")),
                            "");
  EXPECT_DEATH_IF_SUPPORTED(
      Apply(scenic::NewCreateViewHolderCmd(2, fuchsia::ui::views::ViewHolderToken(), "")), "");
}

TEST_F(ViewTest, NullableDebugName) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  constexpr ResourceId kViewHolderId = 1u;
  EXPECT_TRUE(Apply(
      scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token), fit::nullopt)));
  constexpr ResourceId kViewId = 2u;
  EXPECT_TRUE(Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), fit::nullopt)));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
}

TEST_F(ViewTest, ChildrenCanBeAddedToViewWithoutViewHolder) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_id = 1;
  EXPECT_TRUE(Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test")));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  const ResourceId node1_id = 2;
  EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(node1_id)));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  const ResourceId node2_id = 3;
  EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(node2_id)));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  auto view = FindResource<View>(view_id);
  auto node1 = FindResource<Node>(node1_id);
  auto node2 = FindResource<Node>(node2_id);
  EXPECT_TRUE(view);
  EXPECT_TRUE(node1);
  EXPECT_TRUE(node2);

  EXPECT_TRUE(Apply(scenic::NewAddChildCmd(view_id, node1_id)));
  EXPECT_TRUE(Apply(scenic::NewAddChildCmd(view_id, node2_id)));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
}

TEST_F(ViewTest, ExportsViewHolderViaCmd) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1;
  EXPECT_TRUE(
      Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token), "Test")));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  EXPECT_TRUE(view_holder);
  EXPECT_EQ(nullptr, view_holder->view());
  EXPECT_EQ(1u, session()->GetMappedResourceCount());
  EXPECT_EQ(1u, view_linker_->ExportCount());
  EXPECT_EQ(1u, view_linker_->UnresolvedExportCount());
  EXPECT_EQ(0u, view_linker_->ImportCount());
  EXPECT_EQ(0u, view_linker_->UnresolvedImportCount());
}

TEST_F(ViewTest, ImportsViewViaCmd) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_id = 1;
  EXPECT_TRUE(Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test")));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  auto view = FindResource<View>(view_id);
  EXPECT_TRUE(view);
  EXPECT_EQ(nullptr, view->view_holder());
  EXPECT_EQ(1u, session()->GetMappedResourceCount());
  EXPECT_EQ(0u, view_linker_->ExportCount());
  EXPECT_EQ(0u, view_linker_->UnresolvedExportCount());
  EXPECT_EQ(1u, view_linker_->ImportCount());
  EXPECT_EQ(1u, view_linker_->UnresolvedImportCount());
}

TEST_F(ViewTest, PairedViewAndHolderAreLinked) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1u;
  EXPECT_TRUE(Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                                   "Holder [Test]")));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  EXPECT_TRUE(view_holder);
  EXPECT_EQ(nullptr, view_holder->view());
  EXPECT_EQ(1u, session()->GetMappedResourceCount());
  EXPECT_EQ(1u, view_linker_->ExportCount());
  EXPECT_EQ(1u, view_linker_->UnresolvedExportCount());
  EXPECT_EQ(0u, view_linker_->ImportCount());
  EXPECT_EQ(0u, view_linker_->UnresolvedImportCount());

  const ResourceId view_id = 2u;
  EXPECT_TRUE(Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test")));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  auto view = FindResource<View>(view_id);
  EXPECT_TRUE(view);
  EXPECT_EQ(view.get(), view_holder->view());
  EXPECT_EQ(view_holder.get(), view->view_holder());
  EXPECT_EQ(2u, session()->GetMappedResourceCount());
  EXPECT_EQ(1u, view_linker_->ExportCount());
  EXPECT_EQ(0u, view_linker_->UnresolvedExportCount());
  EXPECT_EQ(1u, view_linker_->ImportCount());
  EXPECT_EQ(0u, view_linker_->UnresolvedImportCount());

  EXPECT_NE(0u, events().size());
  const fuchsia::ui::scenic::Event& event = events()[0];
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewConnected, event.gfx().Which());
}

TEST_F(ViewTest, ExportViewHolderWithDeadHandleFails) {
  fuchsia::ui::views::ViewHolderToken view_holder_token_out;
  {
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
    view_holder_token_out.value = zx::eventpair(view_holder_token.value.get());
    // view_holder_token dies now.
  }

  const ResourceId view_holder_id = 1;
  EXPECT_FALSE(Apply(
      scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token_out), "Test")));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(1);  // Dead handles cause a session error.

  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  EXPECT_FALSE(view_holder);
  EXPECT_EQ(0u, session()->GetMappedResourceCount());
  EXPECT_EQ(0u, view_linker_->ExportCount());
  EXPECT_EQ(0u, view_linker_->UnresolvedExportCount());
  EXPECT_EQ(0u, view_linker_->ImportCount());
  EXPECT_EQ(0u, view_linker_->UnresolvedImportCount());
}

TEST_F(ViewTest, ViewHolderDestroyedBeforeView) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  uint32_t next_event_id = events().size();

  // Destroy the ViewHolder and disconnect the link.
  Apply(scenic::NewReleaseResourceCmd(view_holder_id));

  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
  const fuchsia::ui::scenic::Event& event = events()[next_event_id];
  EXPECT_EQ(fuchsia::ui::gfx::Event::Tag::kViewHolderDisconnected, event.gfx().Which());
}

TEST_F(ViewTest, ViewDestroyedBeforeViewHolder) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  uint32_t next_event_id = events().size();

  // Destroy the ViewHolder and disconnect the link.
  Apply(scenic::NewReleaseResourceCmd(view_id));

  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
  const fuchsia::ui::scenic::Event& event = events()[next_event_id];
  EXPECT_EQ(fuchsia::ui::gfx::Event::Tag::kViewDisconnected, event.gfx().Which());
}

TEST_F(ViewTest, ViewAndViewHolderConnectedEvents) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));

  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
  bool view_holder_connected_event = false;
  bool view_connected_event = false;
  for (const fuchsia::ui::scenic::Event& event : events()) {
    if (event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewHolderConnected) {
      view_holder_connected_event = true;
    } else if (event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewConnected) {
      view_connected_event = true;
    }
  }
  EXPECT_TRUE(view_holder_connected_event);
  EXPECT_TRUE(view_connected_event);
}

TEST_F(ViewTest, ViewHolderConnectsToScene) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  auto view = FindResource<View>(view_id);
  uint32_t next_event_id = events().size();

  // Create a Scene and connect the ViewHolder to the Scene.
  const ResourceId scene_id = 3u;
  Apply(scenic::NewCreateSceneCmd(scene_id));
  auto scene = FindResource<Scene>(scene_id);
  EXPECT_TRUE(scene);
  Apply(scenic::NewAddChildCmd(scene_id, view_holder_id));

  // Verify the scene was successfully set.
  const fuchsia::ui::scenic::Event& event = events()[next_event_id];
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewAttachedToScene, event.gfx().Which());
}

TEST_F(ViewTest, ViewHolderDetachedAndReleased) {
  // Create ViewHolder and View.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
  auto view = FindResource<View>(view_id);

  // Create a Scene and connect the ViewHolder to the Scene.
  const ResourceId scene_id = 3u;
  Apply(scenic::NewCreateSceneCmd(scene_id));
  auto scene = FindResource<Scene>(scene_id);
  EXPECT_TRUE(scene);
  EXPECT_TRUE(Apply(scenic::NewAddChildCmd(scene_id, view_holder_id)));
  // Create child node for the View.
  const ResourceId node1_id = 4u;
  Apply(scenic::NewCreateEntityNodeCmd(node1_id));
  EXPECT_TRUE(Apply(scenic::NewAddChildCmd(view_id, node1_id)));
  auto node1 = FindResource<Node>(node1_id);
  EXPECT_TRUE(node1);
  auto view_node = view->GetViewNode();
  EXPECT_EQ(1u, view_node->children().size());
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  // Detach the ViewHolder from the scene graph.
  EXPECT_TRUE(Apply(scenic::NewDetachCmd(view_holder_id)));
  {
    auto view_holder = FindResource<ViewHolder>(view_holder_id);
    // The view holder is still in the ResourceMap so it should still be
    // connected to the view.
    EXPECT_EQ(1u, view_holder->children().size());
    // The view is detached from the scene but still attached to the ViewHolder.
    bool detached_from_scene_event = false;
    for (const fuchsia::ui::scenic::Event& event : events()) {
      detached_from_scene_event |=
          (event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewDetachedFromScene);
    }
    EXPECT_TRUE(detached_from_scene_event);
  }  // view_holder out of scope, release reference.

  // Now, release the ViewHolder resource. Its link should be destroyed.
  uint32_t next_event_id = events().size();
  EXPECT_TRUE(Apply(scenic::NewReleaseResourceCmd(view_holder_id)));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
  bool view_holder_disconnected_event = false;
  for (uint32_t i = next_event_id; i < events().size(); ++i) {
    if (events()[i].gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewHolderDisconnected) {
      view_holder_disconnected_event = true;
      break;
    }
  }
  EXPECT_TRUE(view_holder_disconnected_event);
  // The View's subtree should still be attached to the ViewNode.
  EXPECT_EQ(1u, view_node->children().size());
  EXPECT_FALSE(view_node->parent());
}

TEST_F(ViewTest, ViewHolderChildrenReleasedFromSceneGraphWhenViewDestroyed) {
  // Create ViewHolder and View.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  auto view = FindResource<View>(view_id);
  // Create child nodes for the View.
  const ResourceId node1_id = 3u;
  EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(node1_id)));
  const ResourceId node2_id = 4u;
  EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(node2_id)));
  // Add children
  EXPECT_TRUE(Apply(scenic::NewAddChildCmd(view_id, node1_id)));
  EXPECT_TRUE(Apply(scenic::NewAddChildCmd(view_id, node2_id)));

  view = FindResource<View>(view_id);
  auto node1 = FindResource<Node>(node1_id);
  auto node2 = FindResource<Node>(node2_id);
  EXPECT_TRUE(view);
  EXPECT_TRUE(node1);
  EXPECT_TRUE(node2);

  // Release the View
  Apply(scenic::NewReleaseResourceCmd(view_id));

  view = FindResource<View>(view_id);
  node1 = FindResource<Node>(node1_id);
  node2 = FindResource<Node>(node2_id);
  EXPECT_FALSE(view);
  // The child nodes are still part of the ResourcMap, and should not be
  // destroyed.
  EXPECT_TRUE(node1);
  EXPECT_TRUE(node2);
  // The nodes should not be parented.
  EXPECT_FALSE(node1->parent());
  EXPECT_FALSE(node1->scene());
  EXPECT_FALSE(node2->parent());
  // The view holder should not have any children.
  EXPECT_EQ(0u, view_holder->children().size());
}

TEST_F(ViewTest, ViewNodeChildAddedToViewHolder) {
  // Create ViewHolder and View.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  auto view = FindResource<View>(view_id);

  auto view_node = view->GetViewNode();
  EXPECT_TRUE(view->GetViewNode());
  EXPECT_EQ(1u, view_holder->children().size());
  EXPECT_EQ(view_node->global_id(), view_holder->children()[0]->global_id());
}

TEST_F(ViewTest, ViewHolderCannotAddArbitraryChildNodes) {
  // Create ViewHolder.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  // Create an EntityNode.
  const ResourceId node_id = 2u;
  EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(node_id)));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  // Attempt to add the node as a child of the ViewHolder.
  EXPECT_FALSE(Apply(scenic::NewAddChildCmd(view_holder_id, node_id)));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(1);
}

TEST_F(ViewTest, ViewNodePairedToView) {
  // Create View.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
  auto view = FindResource<View>(view_id);

  auto view_node = view->GetViewNode();
  EXPECT_NE(nullptr, view_node);

  EXPECT_EQ(view->global_id(), view_node->GetView()->global_id());
  EXPECT_EQ(view->id(), view_node->GetView()->id());
  EXPECT_EQ(view->global_id(), view_node->FindOwningView()->global_id());
}

TEST_F(ViewTest, ViewNodeNotInResourceMap) {
  // Create ViewHolder and View.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  auto view = FindResource<View>(view_id);

  EXPECT_NE(nullptr, view->GetViewNode());
  EXPECT_EQ(nullptr, FindResource<ViewNode>(view->GetViewNode()->id()).get());
  EXPECT_SCENIC_SESSION_ERROR_COUNT(1);
}

TEST_F(ViewTest, ViewHolderGrandchildGetsSceneRefreshed) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId kViewHolderId = 1u;
  Apply(scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token), "ViewHolder"));
  const ResourceId kViewId = 2u;
  Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "View"));

  // Create a parent node for the ViewHolder.
  const ResourceId kEntityNodeId = 3u;
  Apply(scenic::NewCreateEntityNodeCmd(kEntityNodeId));
  Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolderId));

  // Create a scene node.
  const ResourceId kSceneId = 4u;
  Apply(scenic::NewCreateSceneCmd(kSceneId));
  auto scene = FindResource<Scene>(kSceneId);
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  // Set the ViewHolder's parent as the child of the scene.
  Apply(scenic::NewAddChildCmd(kSceneId, kEntityNodeId));

  // Verify scene was set on ViewHolder
  const fuchsia::ui::scenic::Event& event = events().back();
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewAttachedToScene, event.gfx().Which());
}

TEST_F(ViewTest, ViewLinksAfterViewHolderConnectsToScene) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  auto view_holder = FindResource<ViewHolder>(view_holder_id);

  // Create a Scene and connect the ViewHolder to the Scene.
  const ResourceId scene_id = 3u;
  Apply(scenic::NewCreateSceneCmd(scene_id));
  auto scene = FindResource<Scene>(scene_id);
  EXPECT_TRUE(scene);
  Apply(scenic::NewAddChildCmd(scene_id, view_holder_id));
  EXPECT_EQ(0u, events().size());

  // Link the View to the ViewHolder.
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  auto view = FindResource<View>(view_id);
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  // Verify the connect event was emitted before the scene attached event.
  EXPECT_EQ(4u, events().size());
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
  const fuchsia::ui::scenic::Event& event = events()[0];
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewConnected, event.gfx().Which());

  bool view_attached_to_scene_event = false;
  for (const fuchsia::ui::scenic::Event& event : events()) {
    if (event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewAttachedToScene) {
      view_attached_to_scene_event = true;
    }
  }
  EXPECT_TRUE(view_attached_to_scene_event);
}

TEST_F(ViewTest, ViewStateChangeNotifiesViewHolder) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  // Verify View and ViewHolder are linked.
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  auto view = FindResource<View>(view_id);
  EXPECT_EQ(view.get(), view_holder->view());
  uint32_t next_event_id = events().size();

  // Trigger a change in the ViewState. Mark as rendering.
  view->SignalRender();

  // Verify that one ViewState change event was enqueued.
  RunLoopUntilIdle();
  EXPECT_LT(next_event_id, events().size());
  const fuchsia::ui::scenic::Event& event = events()[next_event_id];
  VerifyViewState(event, true);
}

TEST_F(ViewTest, RenderStateAcrossManyFrames) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  // Verify View and ViewHolder are linked.
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  auto view = FindResource<View>(view_id);
  EXPECT_EQ(view.get(), view_holder->view());
  uint32_t next_event_id = events().size();

  // Trigger a change in the ViewState. Mark as rendering.
  view->SignalRender();
  RunLoopUntilIdle();

  // Signal render for subsequent frames. No change in rendering state,
  // should not enqueue another event.
  view->SignalRender();
  view->SignalRender();
  RunLoopUntilIdle();

  // Verify that one ViewState change event was enqueued.
  EXPECT_LT(next_event_id, events().size());
  const fuchsia::ui::scenic::Event& event = events()[next_event_id];
  VerifyViewState(event, true);
}

TEST_F(ViewTest, RenderStateFalseWhenViewDisconnects) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  {
    auto view = FindResource<View>(view_id);
    // Verify resources are mapped and linked.
    EXPECT_EQ(2u, session()->GetMappedResourceCount());
    // Mark the view as rendering.
    view->SignalRender();
    RunLoopUntilIdle();
  }  // Exit scope should destroy the view and disconnect the link.

  uint32_t next_event_id = events().size();
  Apply(scenic::NewReleaseResourceCmd(view_id));

  EXPECT_LT(next_event_id, events().size());
  const fuchsia::ui::scenic::Event& event = events()[next_event_id];
  VerifyViewState(event, false);

  const fuchsia::ui::scenic::Event& event2 = events().back();
  EXPECT_EQ(fuchsia::ui::scenic::Event::Tag::kGfx, event2.Which());
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewDisconnected, event2.gfx().Which());
}

TEST_F(ViewTest, ViewHolderRenderWaitClearedWhenViewDestroyed) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));

  // Verify resources are mapped and linked.
  EXPECT_EQ(2u, session()->GetMappedResourceCount());
  uint32_t next_event_id = events().size();
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  // Destroy the view. The link between View and ViewHolder should be
  // disconnected.
  Apply(scenic::NewReleaseResourceCmd(view_id));
  EXPECT_EQ(1u, session()->GetMappedResourceCount());

  EXPECT_LT(next_event_id, events().size());
  const fuchsia::ui::scenic::Event& event = events().back();
  EXPECT_EQ(fuchsia::ui::scenic::Event::Tag::kGfx, event.Which());
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewDisconnected, event.gfx().Which());
}

TEST_F(ViewTest, RenderSignalDoesntCrashWhenViewHolderDestroyed) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));

  // Destroy the ViewHolder and disconnect the link.
  Apply(scenic::NewReleaseResourceCmd(view_holder_id));
  uint32_t event_size = events().size();

  // Mark the view as rendering.
  auto view = FindResource<View>(view_id);
  view->SignalRender();
  RunLoopUntilIdle();
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  // No additional render state events should have been posted.
  EXPECT_EQ(event_size, events().size());
}

TEST_F(ViewTest, RenderStateFalseWhenViewHolderDisconnectsFromScene) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 2u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  const ResourceId view_id = 1u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  auto view = FindResource<View>(view_id);

  // Make sure that the ViewHolder is connected to the Scene and the View is
  // rendering.
  const ResourceId scene_id = 3u;
  Apply(scenic::NewCreateSceneCmd(scene_id));
  auto scene = FindResource<Scene>(scene_id);
  Apply(scenic::NewAddChildCmd(scene_id, view_holder_id));
  view->SignalRender();
  RunLoopUntilIdle();

  uint32_t next_event_id = events().size();

  // Detach ViewHolder from the scene.
  view_holder->Detach(session()->error_reporter());

  // The "stopped rendering" event should have emitted before the "detached from
  // scene" event.
  EXPECT_LT(next_event_id, events().size());
  const fuchsia::ui::scenic::Event& event = events()[next_event_id];
  VerifyViewState(event, false);
  const fuchsia::ui::scenic::Event& event2 = events().back();
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewDetachedFromScene, event2.gfx().Which());
}

// When a ViewHolder receives a SetViewPropertiesCmd with invalid bounding box,
// it should still set the properties, but ignore the bounding box arguments.
TEST_F(ViewTest, ViewPropertiesWithInvalidBoundingBox) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  auto view_holder = FindResource<ViewHolder>(view_holder_id);

  // Initialize ViewProperties for the ViewHolder.
  Apply(scenic::NewSetViewPropertiesCmd(view_holder_id, fuchsia::ui::gfx::ViewProperties{
                                                            .bounding_box =
                                                                {
                                                                    .min = {0, 0, 0},
                                                                    .max = {100, 100, 100},
                                                                },
                                                            .inset_from_min = {0, 0, 0},
                                                            .inset_from_max = {0, 0, 0},
                                                            .focus_change = true,
                                                            .downward_input = true,
                                                        }));
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  // Set ViewProperties with an invalid bounding box.
  Apply(scenic::NewSetViewPropertiesCmd(view_holder_id, fuchsia::ui::gfx::ViewProperties{
                                                            .bounding_box =
                                                                {
                                                                    .min = {1, 1, 1},
                                                                    .max = {1, 1, 1},
                                                                },
                                                            .inset_from_min = {0, 0, 0},
                                                            .inset_from_max = {0, 0, 0},
                                                            .focus_change = true,
                                                            .downward_input = true,
                                                        }));

  EXPECT_SCENIC_SESSION_ERROR_COUNT(1);
  ExpectErrorAt(0,
                "ViewProperties has invalid or uninitialized bounding box: min = 1,1,1 "
                "max = 1,1,1 inset_from_min = 0,0,0 inset_from_max = 0,0,0.");

  // Create a Scene and connect the ViewHolder to the Scene.
  const ResourceId scene_id = 3u;
  Apply(scenic::NewCreateSceneCmd(scene_id));
  auto scene = FindResource<Scene>(scene_id);
  EXPECT_TRUE(scene);
  Apply(scenic::NewAddChildCmd(scene_id, view_holder_id));
  EXPECT_EQ(0u, events().size());

  // Link the View to the ViewHolder.
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  auto view = FindResource<View>(view_id);
  // No new errors.
  EXPECT_SCENIC_SESSION_ERROR_COUNT(1);

  // Verify the ViewPropertiesChanged event was emitted.
  bool view_properties_changed_event = false;
  for (const fuchsia::ui::scenic::Event& event : events()) {
    if (event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged) {
      view_properties_changed_event = true;
    }
  }
  EXPECT_TRUE(view_properties_changed_event);
}

TEST_F(ViewTest, AnnotationViewReceivesViewPropertiesChangedEvent) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  auto view_holder = FindResource<ViewHolder>(view_holder_id)->GetWeakPtr();

  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  auto view = FindResource<View>(view_id)->GetWeakPtr();

  auto session_annotation = CreateSession();
  auto cmd_ctx = CreateCommandContext();
  const ResourceId annotation_view_id = 3u;
  session_annotation->ApplyCommand(
      &cmd_ctx,
      scenic::NewCreateViewCmd(annotation_view_id, std::move(annotation_view_token), "Annotation"));
  auto annotation_view =
      session_annotation->resources()->FindResource<View>(annotation_view_id)->GetWeakPtr();

  // Create Annotation ViewHolder.
  EXPECT_TRUE(view->AddAnnotationViewHolder(NewAnnotationViewHolder(
      session_annotation.get(), view_linker_.get(), std::move(annotation_view_holder_token))));

  // Set ViewProperties.
  uint32_t event_size = events().size();
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {0, 0, 0}, .max = {100, 200, 300}};
  fuchsia::ui::gfx::ViewProperties view_properties = {
      .bounding_box = bounding_box,
      .inset_from_min = {1, 2, 3},
      .inset_from_max = {4, 5, 6},
      .focus_change = true,
      .downward_input = true,
  };
  view_holder->SetViewProperties(view_properties, session()->error_reporter());

  RunLoopUntilIdle();
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  EXPECT_EQ(event_size + 2, events().size());
  const auto& event_1 = events()[events().size() - 2];
  const auto& event_2 = events()[events().size() - 1];
  EXPECT_TRUE(event_1.is_gfx() && event_1.gfx().is_view_properties_changed());
  EXPECT_TRUE(event_2.is_gfx() && event_2.gfx().is_view_properties_changed());

  auto [event_view, event_annotation] =
      event_1.gfx().view_properties_changed().view_id == view_id
          ? std::make_pair(event_1.gfx().view_properties_changed(),
                           event_2.gfx().view_properties_changed())
          : std::make_pair(event_2.gfx().view_properties_changed(),
                           event_1.gfx().view_properties_changed());

  EXPECT_EQ(event_view.view_id, view_id);
  EXPECT_EQ(event_annotation.view_id, annotation_view_id);

  EXPECT_TRUE(fidl::Equals(event_view.properties, view_properties));
  EXPECT_FALSE(event_annotation.properties.focus_change);
  event_annotation.properties.focus_change = view_properties.focus_change;
  EXPECT_TRUE(fidl::Equals(event_annotation.properties, view_properties));
}

TEST_F(ViewTest, AnnotationViewReceivesViewAttachedToSceneEvent) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  auto view_holder = FindResource<ViewHolder>(view_holder_id)->GetWeakPtr();

  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  auto view = FindResource<View>(view_id)->GetWeakPtr();

  auto session_annotation = CreateSession();
  auto cmd_ctx = CreateCommandContext();
  const ResourceId annotation_view_id = 3u;
  session_annotation->ApplyCommand(
      &cmd_ctx,
      scenic::NewCreateViewCmd(annotation_view_id, std::move(annotation_view_token), "Annotation"));
  auto annotation_view =
      session_annotation->resources()->FindResource<View>(annotation_view_id)->GetWeakPtr();

  // Create Annotation ViewHolder.
  EXPECT_TRUE(view->AddAnnotationViewHolder(NewAnnotationViewHolder(
      session_annotation.get(), view_linker_.get(), std::move(annotation_view_holder_token))));

  // Create a Scene and connect the ViewHolder to the Scene.
  const size_t event_size = events().size();
  const ResourceId scene_id = 4u;
  Apply(scenic::NewCreateSceneCmd(scene_id));
  auto scene = FindResource<Scene>(scene_id);
  EXPECT_TRUE(scene);
  Apply(scenic::NewAddChildCmd(scene_id, view_holder_id));

  RunLoopUntilIdle();
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  EXPECT_EQ(event_size + 2, events().size());
  const auto& event_1 = events()[events().size() - 2];
  const auto& event_2 = events()[events().size() - 1];
  EXPECT_TRUE(event_1.is_gfx() && event_1.gfx().is_view_attached_to_scene());
  EXPECT_TRUE(event_2.is_gfx() && event_2.gfx().is_view_attached_to_scene());

  const uint32_t event_1_view_id = event_1.gfx().view_attached_to_scene().view_id;
  const uint32_t event_2_view_id = event_2.gfx().view_attached_to_scene().view_id;

  EXPECT_TRUE((event_1_view_id == view_id && event_2_view_id == annotation_view_id) ||
              (event_2_view_id == view_id && event_1_view_id == annotation_view_id));
}

TEST_F(ViewTest, AnnotationViewReceivesViewDetachedFromSceneEvent) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();

  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token),
                                       "Holder [Test]"));
  auto view_holder = FindResource<ViewHolder>(view_holder_id)->GetWeakPtr();

  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  auto view = FindResource<View>(view_id)->GetWeakPtr();

  auto session_annotation = CreateSession();
  auto cmd_ctx = CreateCommandContext();
  const ResourceId annotation_view_id = 3u;
  session_annotation->ApplyCommand(
      &cmd_ctx,
      scenic::NewCreateViewCmd(annotation_view_id, std::move(annotation_view_token), "Annotation"));
  auto annotation_view =
      session_annotation->resources()->FindResource<View>(annotation_view_id)->GetWeakPtr();

  // Create Annotation ViewHolder.
  EXPECT_TRUE(view->AddAnnotationViewHolder(NewAnnotationViewHolder(
      session_annotation.get(), view_linker_.get(), std::move(annotation_view_holder_token))));

  // Create a Scene and connect the ViewHolder to the Scene.
  const ResourceId scene_id = 4u;
  Apply(scenic::NewCreateSceneCmd(scene_id));
  auto scene = FindResource<Scene>(scene_id);
  EXPECT_TRUE(scene);
  Apply(scenic::NewAddChildCmd(scene_id, view_holder_id));

  // Detach the ViewHolder from the scene graph.
  const size_t event_size = events().size();
  EXPECT_TRUE(Apply(scenic::NewDetachCmd(view_holder_id)));

  RunLoopUntilIdle();
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);

  EXPECT_EQ(event_size + 2, events().size());
  const auto& event_1 = events()[events().size() - 2];
  const auto& event_2 = events()[events().size() - 1];
  EXPECT_TRUE(event_1.is_gfx() && event_1.gfx().is_view_detached_from_scene());
  EXPECT_TRUE(event_2.is_gfx() && event_2.gfx().is_view_detached_from_scene());

  const uint32_t event_1_view_id = event_1.gfx().view_detached_from_scene().view_id;
  const uint32_t event_2_view_id = event_2.gfx().view_detached_from_scene().view_id;

  EXPECT_TRUE((event_1_view_id == view_id && event_2_view_id == annotation_view_id) ||
              (event_2_view_id == view_id && event_1_view_id == annotation_view_id));
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
