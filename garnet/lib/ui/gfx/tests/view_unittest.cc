
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/view.h"
#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"
#include "garnet/lib/ui/gfx/resources/view_holder.h"

#include <lib/async/cpp/task.h>
#include <lib/zx/eventpair.h>

#include "garnet/lib/ui/gfx/tests/session_test.h"
#include "garnet/lib/ui/gfx/tests/util.h"
#include "lib/ui/scenic/cpp/commands.h"

namespace scenic_impl {
namespace gfx {

namespace test {

class ViewTest : public SessionTest {
 public:
  ViewTest() {}

  std::unique_ptr<SessionForTest> CreateSession() override {
    SessionContext session_context = CreateBarebonesSessionContext();

    view_linker_ = std::make_unique<ViewLinker>();
    session_context.view_linker = view_linker_.get();

    return std::make_unique<SessionForTest>(1, std::move(session_context), this,
                                            error_reporter());
  }

  std::unique_ptr<ViewLinker> view_linker_;
};

// TODO(ES-179): Only seems to die in debug builds.
TEST_F(ViewTest, DISABLED_CreateViewWithBadTokenDies) {
  EXPECT_DEATH_IF_SUPPORTED(
      Apply(scenic::NewCreateViewCmd(1, zx::eventpair(), "")), "");
  EXPECT_DEATH_IF_SUPPORTED(
      Apply(scenic::NewCreateViewHolderCmd(2, zx::eventpair(), "")), "");
}

TEST_F(ViewTest, Children) {
  zx::eventpair view_holder_token, view_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &view_holder_token, &view_token));

  const ResourceId view_id = 1;
  EXPECT_TRUE(
      Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test")));
  EXPECT_ERROR_COUNT(0);

  const ResourceId node1_id = 2;
  EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(node1_id)));
  EXPECT_ERROR_COUNT(0);

  const ResourceId node2_id = 3;
  EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(node2_id)));
  EXPECT_ERROR_COUNT(0);

  auto view = FindResource<View>(view_id);
  auto node1 = FindResource<Node>(node1_id);
  auto node2 = FindResource<Node>(node2_id);
  EXPECT_TRUE(view);
  EXPECT_TRUE(node1);
  EXPECT_TRUE(node2);

  EXPECT_TRUE(Apply(scenic::NewAddChildCmd(view_id, node1_id)));
  EXPECT_ERROR_COUNT(0);

  const std::unordered_set<NodePtr>& children = view->children();
  auto child_iter = children.begin();
  std::equal_to<ResourcePtr> equal_to;
  std::hash<ResourcePtr> hash;
  EXPECT_EQ(children.size(), 1u);
  EXPECT_EQ(*child_iter, node1);
  EXPECT_TRUE(equal_to(*child_iter, node1));
  EXPECT_EQ(hash(*child_iter), hash(node1));

  EXPECT_TRUE(Apply(scenic::NewAddChildCmd(view_id, node2_id)));
  EXPECT_ERROR_COUNT(0);

  child_iter = children.begin();  // Iterator was invalidated before.
  EXPECT_EQ(children.size(), 2u);
  if (*child_iter == node1) {
    child_iter++;
  }
  EXPECT_EQ(*child_iter, node2);
  EXPECT_TRUE(equal_to(*child_iter, node2));
  EXPECT_EQ(hash(*child_iter), hash(node2));
}

TEST_F(ViewTest, ExportsViewHolderViaCmd) {
  zx::eventpair view_holder_token, view_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &view_holder_token, &view_token));

  const ResourceId view_holder_id = 1;
  EXPECT_TRUE(Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "Test")));
  EXPECT_ERROR_COUNT(0);

  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  EXPECT_TRUE(view_holder);
  EXPECT_EQ(nullptr, view_holder->view());
  EXPECT_EQ(1u, session_->GetMappedResourceCount());
  EXPECT_EQ(1u, view_linker_->ExportCount());
  EXPECT_EQ(1u, view_linker_->UnresolvedExportCount());
  EXPECT_EQ(0u, view_linker_->ImportCount());
  EXPECT_EQ(0u, view_linker_->UnresolvedImportCount());
}

TEST_F(ViewTest, ImportsViewViaCmd) {
  zx::eventpair view_holder_token, view_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &view_holder_token, &view_token));

  const ResourceId view_id = 1;
  EXPECT_TRUE(
      Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test")));
  EXPECT_ERROR_COUNT(0);

  auto view = FindResource<View>(view_id);
  EXPECT_TRUE(view);
  EXPECT_EQ(nullptr, view->view_holder());
  EXPECT_EQ(1u, session_->GetMappedResourceCount());
  EXPECT_EQ(0u, view_linker_->ExportCount());
  EXPECT_EQ(0u, view_linker_->UnresolvedExportCount());
  EXPECT_EQ(1u, view_linker_->ImportCount());
  EXPECT_EQ(1u, view_linker_->UnresolvedImportCount());
}

TEST_F(ViewTest, PairedViewAndHolderAreLinked) {
  zx::eventpair view_holder_token, view_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &view_holder_token, &view_token));

  const ResourceId view_holder_id = 1u;
  EXPECT_TRUE(Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "Holder [Test]")));
  EXPECT_ERROR_COUNT(0);

  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  EXPECT_TRUE(view_holder);
  EXPECT_EQ(nullptr, view_holder->view());
  EXPECT_EQ(1u, session_->GetMappedResourceCount());
  EXPECT_EQ(1u, view_linker_->ExportCount());
  EXPECT_EQ(1u, view_linker_->UnresolvedExportCount());
  EXPECT_EQ(0u, view_linker_->ImportCount());
  EXPECT_EQ(0u, view_linker_->UnresolvedImportCount());

  const ResourceId view_id = 2u;
  EXPECT_TRUE(
      Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test")));
  EXPECT_ERROR_COUNT(0);

  auto view = FindResource<View>(view_id);
  EXPECT_TRUE(view);
  EXPECT_EQ(view.get(), view_holder->view());
  EXPECT_EQ(view_holder.get(), view->view_holder());
  EXPECT_EQ(2u, session_->GetMappedResourceCount());
  EXPECT_EQ(1u, view_linker_->ExportCount());
  EXPECT_EQ(0u, view_linker_->UnresolvedExportCount());
  EXPECT_EQ(1u, view_linker_->ImportCount());
  EXPECT_EQ(0u, view_linker_->UnresolvedImportCount());

  EXPECT_NE(0u, events_.size());
  const fuchsia::ui::scenic::Event& event = events_[0];
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewConnected,
            event.gfx().Which());
}

TEST_F(ViewTest, ExportViewHolderWithDeadHandleFails) {
  zx::eventpair view_holder_token_out, view_token;
  {
    zx::eventpair view_holder_token;
    EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &view_holder_token, &view_token));
    view_holder_token_out = zx::eventpair{view_holder_token.get()};
    // view_holder_token dies now.
  }

  const ResourceId view_holder_id = 1;
  EXPECT_FALSE(Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token_out), "Test")));
  EXPECT_ERROR_COUNT(1);  // Dead handles cause a session error.

  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  EXPECT_FALSE(view_holder);
  EXPECT_EQ(0u, session_->GetMappedResourceCount());
  EXPECT_EQ(0u, view_linker_->ExportCount());
  EXPECT_EQ(0u, view_linker_->UnresolvedExportCount());
  EXPECT_EQ(0u, view_linker_->ImportCount());
  EXPECT_EQ(0u, view_linker_->UnresolvedImportCount());
}

TEST_F(ViewTest, ViewHolderDestroyedBeforeView) {
  // Create ViewHolder and View.
  zx::eventpair view_holder_token, view_token;
  zx::eventpair::create(0, &view_holder_token, &view_token);
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  events_.clear();

  // Destroy the ViewHolder and disconnect the link.
  Apply(scenic::NewReleaseResourceCmd(view_holder_id));

  EXPECT_ERROR_COUNT(0);
  EXPECT_EQ(1u, events_.size());
  const fuchsia::ui::scenic::Event& event = events_[0];
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewHolderDisconnected,
            event.gfx().Which());
}

TEST_F(ViewTest, ViewDestroyedBeforeViewHolder) {
  // Create ViewHolder and View.
  zx::eventpair view_holder_token, view_token;
  zx::eventpair::create(0, &view_holder_token, &view_token);
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  events_.clear();

  // Destroy the ViewHolder and disconnect the link.
  Apply(scenic::NewReleaseResourceCmd(view_id));

  EXPECT_ERROR_COUNT(0);
  const fuchsia::ui::scenic::Event& event = events_[0];
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewDisconnected,
            event.gfx().Which());
}

TEST_F(ViewTest, ViewHolderConnectsToScene) {
  // Create ViewHolder and View.
  zx::eventpair view_holder_token, view_token;
  zx::eventpair::create(0, &view_holder_token, &view_token);
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_ERROR_COUNT(0);
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  auto view = FindResource<View>(view_id);
  events_.clear();

  // Create a Scene and connect the ViewHolder to the Scene.
  const ResourceId scene_id = 3u;
  Apply(scenic::NewCreateSceneCmd(scene_id));
  auto scene = FindResource<Scene>(scene_id);
  EXPECT_TRUE(scene);
  Apply(scenic::NewAddChildCmd(scene_id, view_holder_id));

  // Verify the scene was successfully set.
  EXPECT_EQ(1u, events_.size());
  const fuchsia::ui::scenic::Event& event = events_[0];
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewAttachedToScene,
            event.gfx().Which());
}

TEST_F(ViewTest, ViewHolderGrandchildGetsSceneRefreshed) {
  zx::eventpair view_holder_token, view_token;
  zx::eventpair::create(0, &view_holder_token, &view_token);
  const ResourceId kViewHolderId = 1u;
  Apply(scenic::NewCreateViewHolderCmd(
      kViewHolderId, std::move(view_holder_token), "ViewHolder"));
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
  EXPECT_ERROR_COUNT(0);

  // Set the ViewHolder's parent as the child of the scene.
  Apply(scenic::NewAddChildCmd(kSceneId, kEntityNodeId));

  // Verify scene was set on ViewHolder
  const fuchsia::ui::scenic::Event& event = events_.back();
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewAttachedToScene,
            event.gfx().Which());
}

TEST_F(ViewTest, ViewLinksAfterViewHolderConnectsToScene) {
  // Create ViewHolder and View.
  zx::eventpair view_holder_token, view_token;
  zx::eventpair::create(0, &view_holder_token, &view_token);
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "Holder [Test]"));
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  // Create a Scene and connect the ViewHolder to the Scene.
  const ResourceId scene_id = 3u;
  Apply(scenic::NewCreateSceneCmd(scene_id));
  auto scene = FindResource<Scene>(scene_id);
  EXPECT_TRUE(scene);
  Apply(scenic::NewAddChildCmd(scene_id, view_holder_id));
  EXPECT_EQ(0u, events_.size());

  // Link the View to the ViewHolder.
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  auto view = FindResource<View>(view_id);
  EXPECT_ERROR_COUNT(0);

  // Verify the connect event was emitted before the scene attached event.
  EXPECT_EQ(3u, events_.size());
  const fuchsia::ui::scenic::Event& event = events_[0];
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewConnected,
            event.gfx().Which());
  const fuchsia::ui::scenic::Event& event2 = events_[1];
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewAttachedToScene,
            event2.gfx().Which());
}

void VerifyViewState(const fuchsia::ui::scenic::Event& event,
                     bool is_rendering_expected) {
  EXPECT_EQ(fuchsia::ui::scenic::Event::Tag::kGfx, event.Which());
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewStateChanged,
            event.gfx().Which());
  const ::fuchsia::ui::gfx::ViewState& view_state =
      event.gfx().view_state_changed().state;
  EXPECT_EQ(is_rendering_expected, view_state.is_rendering);
}

TEST_F(ViewTest, ViewStateChangeNotifiesViewHolder) {
  // Create ViewHolder and View.
  zx::eventpair view_holder_token, view_token;
  zx::eventpair::create(0, &view_holder_token, &view_token);
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_ERROR_COUNT(0);
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  auto view = FindResource<View>(view_id);
  // Verify View and ViewHolder are linked.
  EXPECT_EQ(view.get(), view_holder->view());
  // Clear View/ViewHolder connected events from the session.
  events_.clear();

  // Trigger a change in the ViewState. Mark as rendering.
  view->SignalRender();

  // Verify that one ViewState change event was enqueued.
  RunLoopUntilIdle();
  EXPECT_EQ(1u, events_.size());
  const fuchsia::ui::scenic::Event& event = events_[0];
  VerifyViewState(event, true);
}

TEST_F(ViewTest, RenderStateAcrossManyFrames) {
  // Create ViewHolder and View.
  zx::eventpair view_holder_token, view_token;
  zx::eventpair::create(0, &view_holder_token, &view_token);
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_ERROR_COUNT(0);
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  auto view = FindResource<View>(view_id);
  // Verify View and ViewHolder are linked.
  EXPECT_EQ(view.get(), view_holder->view());
  // Clear View/ViewHolder connected events from the session.
  events_.clear();

  // Trigger a change in the ViewState. Mark as rendering.
  view->SignalRender();
  RunLoopUntilIdle();
  // Signal render for subsequent frames. No change in rendering state,
  // should not enqueue another event.
  view->SignalRender();
  view->SignalRender();
  RunLoopUntilIdle();

  // Verify that one ViewState change event was enqueued.
  EXPECT_EQ(1u, events_.size());
  const fuchsia::ui::scenic::Event& event = events_[0];
  VerifyViewState(event, true);
}

TEST_F(ViewTest, RenderStateFalseWhenViewDisconnects) {
  // Create ViewHolder and View.
  zx::eventpair view_holder_token, view_token;
  zx::eventpair::create(0, &view_holder_token, &view_token);
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "Holder [Test]"));
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_ERROR_COUNT(0);
  {
    auto view = FindResource<View>(view_id);
    // Verify resources are mapped and linked.
    EXPECT_EQ(2u, session_->GetMappedResourceCount());
    // Mark the view as rendering.
    view->SignalRender();
    RunLoopUntilIdle();
    events_.clear();
  }  // Exit scope should destroy the view and disconnect the link.
  Apply(scenic::NewReleaseResourceCmd(view_id));

  EXPECT_EQ(2u, events_.size());
  const fuchsia::ui::scenic::Event& event = events_[0];
  VerifyViewState(event, false);

  const fuchsia::ui::scenic::Event& event2 = events_.back();
  EXPECT_EQ(fuchsia::ui::scenic::Event::Tag::kGfx, event2.Which());
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewDisconnected,
            event2.gfx().Which());
}

TEST_F(ViewTest, ViewHolderRenderWaitClearedWhenViewDestroyed) {
  // Create ViewHolder and View.
  zx::eventpair view_holder_token, view_token;
  zx::eventpair::create(0, &view_holder_token, &view_token);
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "Holder [Test]"));
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  // Verify resources are mapped and linked.
  EXPECT_EQ(2u, session_->GetMappedResourceCount());
  events_.clear();
  EXPECT_ERROR_COUNT(0);

  // Destroy the view. The link between View and ViewHolder should be
  // disconnected.
  Apply(scenic::NewReleaseResourceCmd(view_id));
  EXPECT_EQ(1u, session_->GetMappedResourceCount());

  EXPECT_EQ(1u, events_.size());
  const fuchsia::ui::scenic::Event& event = events_.back();
  EXPECT_EQ(fuchsia::ui::scenic::Event::Tag::kGfx, event.Which());
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewDisconnected,
            event.gfx().Which());
}

TEST_F(ViewTest, RenderSignalDoesntCrashWhenViewHolderDestroyed) {
  // Create ViewHolder and View.
  zx::eventpair view_holder_token, view_token;
  zx::eventpair::create(0, &view_holder_token, &view_token);
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  // Destroy the ViewHolder and disconnect the link.
  Apply(scenic::NewReleaseResourceCmd(view_holder_id));
  events_.clear();

  // Mark the view as rendering.
  auto view = FindResource<View>(view_id);
  view->SignalRender();
  RunLoopUntilIdle();

  EXPECT_ERROR_COUNT(0);
  // No additional render state events should have been posted.
  EXPECT_EQ(0u, events_.size());
}

TEST_F(ViewTest, RenderStateFalseWhenViewHolderDisconnectsFromScene) {
  // Create ViewHolder and View.
  zx::eventpair view_holder_token, view_token;
  zx::eventpair::create(0, &view_holder_token, &view_token);
  const ResourceId view_holder_id = 1u;
  Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "Holder [Test]"));
  const ResourceId view_id = 2u;
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "Test"));
  EXPECT_ERROR_COUNT(0);
  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  auto view = FindResource<View>(view_id);
  events_.clear();
  // Make sure that the ViewHolder is connected to the Scene and the View is
  // rendering.
  const ResourceId scene_id = 3u;
  Apply(scenic::NewCreateSceneCmd(scene_id));
  auto scene = FindResource<Scene>(scene_id);
  Apply(scenic::NewAddChildCmd(scene_id, view_holder_id));
  view->SignalRender();
  RunLoopUntilIdle();
  events_.clear();

  // Detach ViewHolder from the scene.
  view_holder->Detach();

  EXPECT_EQ(2u, events_.size());
  // The "stopped rendering" event should have emitted before the "detached from
  // scene" event.
  const fuchsia::ui::scenic::Event& event = events_[0];
  VerifyViewState(event, false);
  const fuchsia::ui::scenic::Event& event2 = events_.back();
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kViewDetachedFromScene,
            event2.gfx().Which());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
