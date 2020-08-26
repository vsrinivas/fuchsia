// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/view_tree.h"

#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/gfx/id.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"
#include "src/ui/scenic/lib/scheduling/id.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace lib_ui_gfx_engine_tests {

using fuchsia::ui::focus::FocusChain;
using fuchsia::ui::views::ViewRef;
using scenic_impl::EventReporterWeakPtr;
using scenic_impl::gfx::ViewHolderPtr;
using scenic_impl::gfx::ViewTree;
using utils::ExtractKoid;

const scenic_impl::SessionId kOne = 1u, kTwo = 2u, kThree = 3u, kFour = 4u, kFive = 5u;

scenic_impl::gfx::ViewTreeNewRefNode ViewTreeNewRefNodeTemplate(ViewRef view_ref,
                                                                scheduling::SessionId session_id) {
  EventReporterWeakPtr no_reporter{};
  return {.view_ref = std::move(view_ref),
          .event_reporter = no_reporter,
          .may_receive_focus = [] { return true; },
          .is_input_suppressed = [] { return false; },
          .global_transform = [] { return std::nullopt; },
          .hit_test = [](auto...) {},
          .add_annotation_view_holder = [](auto) {},
          .session_id = session_id};
}

TEST(ViewTreeLifecycle, EmptyScene) {
  ViewTree tree{};

  EXPECT_TRUE(tree.focus_chain().empty());

  EXPECT_TRUE(tree.CloneFocusChain().IsEmpty());

  EXPECT_TRUE(tree.IsStateValid());
}

TEST(ViewTreeLifecycle, SceneCreateThenDestroy) {
  ViewTree tree{};

  // Create a scene node.
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair.view_ref), kOne));
  tree.MakeGlobalRoot(koid);

  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], koid);

  FocusChain clone = tree.CloneFocusChain();
  EXPECT_FALSE(clone.IsEmpty());
  ASSERT_EQ(clone.focus_chain().size(), 1u);

  const ViewRef& root = clone.focus_chain()[0];
  EXPECT_EQ(koid, ExtractKoid(root));

  // Destroy the scene node.
  tree.DeleteNode(koid);

  EXPECT_TRUE(tree.focus_chain().empty());
  EXPECT_TRUE(tree.CloneFocusChain().IsEmpty());

  EXPECT_TRUE(tree.IsStateValid());
}

TEST(ViewTreeLifecycle, SceneCreateThenReplace) {
  ViewTree tree{};

  // Create a scene node.
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid);

  // Replace it with another scene node.
  scenic::ViewRefPair pair_b = scenic::ViewRefPair::New();
  zx_koid_t scene_koid_b = ExtractKoid(pair_b.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair_b.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid_b);

  EXPECT_EQ(tree.focus_chain().size(), 1u);
  FocusChain clone = tree.CloneFocusChain();
  ASSERT_EQ(clone.focus_chain().size(), 1u);

  const ViewRef& root = clone.focus_chain()[0];
  EXPECT_EQ(scene_koid_b, ExtractKoid(root));

  EXPECT_TRUE(tree.IsStateValid());
}

TEST(ViewTreeLifecycle, ConnectedSceneWithFocusTransfer) {
  ViewTree tree{};

  // Create a scene node.
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid);

  // Create an attach node for view 1, connect to scene.
  zx_koid_t attach_1_koid = 1111u;
  tree.NewAttachNode(attach_1_koid);
  tree.ConnectToParent(attach_1_koid, scene_koid);

  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Create a view node, attach it.
  scenic::ViewRefPair pair_1 = scenic::ViewRefPair::New();
  zx_koid_t view_1_koid = ExtractKoid(pair_1.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair_1.view_ref), kTwo));
  tree.ConnectToParent(view_1_koid, attach_1_koid);

  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Create a attach node for view 2, connect to scene.
  zx_koid_t attach_2_koid = 2222u;
  tree.NewAttachNode(attach_2_koid);
  tree.ConnectToParent(attach_2_koid, scene_koid);

  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Create a view node, attach it.
  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_2_koid = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair_2.view_ref), kThree));
  tree.ConnectToParent(view_2_koid, attach_2_koid);

  // Transfer focus: scene to view 2.
  ViewTree::FocusChangeStatus status = tree.RequestFocusChange(scene_koid, view_2_koid);

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  ASSERT_EQ(tree.focus_chain().size(), 2u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_2_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Destroy view 2.
  tree.DeleteNode(view_2_koid);

  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Transfer focus, scene to child 1
  status = tree.RequestFocusChange(scene_koid, view_1_koid);

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  ASSERT_EQ(tree.focus_chain().size(), 2u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_1_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Destroy attach 1.
  tree.DeleteNode(attach_1_koid);

  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_TRUE(tree.IsStateValid());
}

TEST(ViewTreeLifecycle, SlowlyDestroyedScene) {
  ViewTree tree{};

  // Create a scene, attach 1, view 1, attach 2, view 2 in one deep hierarchy..
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid);

  zx_koid_t attach_1_koid = 1111u;
  tree.NewAttachNode(attach_1_koid);
  tree.ConnectToParent(attach_1_koid, scene_koid);

  scenic::ViewRefPair pair_1 = scenic::ViewRefPair::New();
  zx_koid_t view_1_koid = ExtractKoid(pair_1.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair_1.view_ref), kTwo));
  tree.ConnectToParent(view_1_koid, attach_1_koid);

  zx_koid_t attach_2_koid = 2222u;
  tree.NewAttachNode(attach_2_koid);
  tree.ConnectToParent(attach_2_koid, view_1_koid);

  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_2_koid = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair_2.view_ref), kThree));
  tree.ConnectToParent(view_2_koid, attach_2_koid);

  EXPECT_TRUE(tree.IsStateValid());

  // Transfer focus to view 2.
  ViewTree::FocusChangeStatus status = tree.RequestFocusChange(scene_koid, view_2_koid);

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  ASSERT_EQ(tree.focus_chain().size(), 3u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_1_koid);
  EXPECT_EQ(tree.focus_chain()[2], view_2_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Destroy view 2.
  tree.DeleteNode(view_2_koid);

  ASSERT_EQ(tree.focus_chain().size(), 2u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_1_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Destroy view 1.
  tree.DeleteNode(view_1_koid);

  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Destroy scene.
  tree.DeleteNode(scene_koid);

  EXPECT_EQ(tree.focus_chain().size(), 0u);
  EXPECT_TRUE(tree.IsStateValid());
}

TEST(ViewTreeLifecycle, SlowlyDisconnectedScene) {
  ViewTree tree{};

  // Create a scene, attach 1, view 1, attach 2, view 2 in one deep hierarchy.
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid);

  zx_koid_t attach_1_koid = 1111u;
  tree.NewAttachNode(attach_1_koid);
  tree.ConnectToParent(attach_1_koid, scene_koid);

  scenic::ViewRefPair pair_1 = scenic::ViewRefPair::New();
  zx_koid_t view_1_koid = ExtractKoid(pair_1.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair_1.view_ref), kTwo));
  tree.ConnectToParent(view_1_koid, attach_1_koid);

  zx_koid_t attach_2_koid = 2222u;
  tree.NewAttachNode(attach_2_koid);
  tree.ConnectToParent(attach_2_koid, view_1_koid);

  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_2_koid = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair_2.view_ref), kThree));
  tree.ConnectToParent(view_2_koid, attach_2_koid);

  EXPECT_TRUE(tree.IsStateValid());

  // Transfer focus to view 2.
  ViewTree::FocusChangeStatus status = tree.RequestFocusChange(scene_koid, view_2_koid);

  EXPECT_EQ(status, ViewTree::FocusChangeStatus::kAccept);
  ASSERT_EQ(tree.focus_chain().size(), 3u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_1_koid);
  EXPECT_EQ(tree.focus_chain()[2], view_2_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Disconnect view 2.
  tree.DisconnectFromParent(view_2_koid);

  ASSERT_EQ(tree.focus_chain().size(), 2u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_1_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Disconnect view 1.
  tree.DisconnectFromParent(view_1_koid);

  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_TRUE(tree.IsStateValid());
}

// Exercise focus release policy: when a focused RefNode becomes detached, we transfer focus up the
// focus chain to the lowest ancestor that has the "may receive focus" property.
// Tree topology:
//   Nodes:            scene - a_1 - v_1 - a_2 - v_2 - a_3 - v_3
//   Focus-receivable: yes           no          no          yes
// In this test, we start with the focus chain [scene, v_1, v_2, v_3]. When v_3 gets disconnected,
// the focus chain becomes [scene], bypassing the unfocusable nodes v_1 and v_2.
TEST(ViewTreeLifecycle, ReleaseBypassesUnfocusableNodes) {
  ViewTree tree{};

  // Tree setup
  scenic::ViewRefPair scene_pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(scene_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(scene_pair.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid);

  zx_koid_t attach_koid_1 = 1111u;
  tree.NewAttachNode(attach_koid_1);
  tree.ConnectToParent(attach_koid_1, scene_koid);

  scenic::ViewRefPair view_pair_1 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_1 = ExtractKoid(view_pair_1.view_ref);
  {
    auto new_node = ViewTreeNewRefNodeTemplate(std::move(view_pair_1.view_ref), kTwo);
    new_node.may_receive_focus = [] { return false; };
    tree.NewRefNode(std::move(new_node));
    tree.ConnectToParent(view_koid_1, attach_koid_1);
  }

  zx_koid_t attach_koid_2 = 2222u;
  tree.NewAttachNode(attach_koid_2);
  tree.ConnectToParent(attach_koid_2, view_koid_1);

  scenic::ViewRefPair view_pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_2 = ExtractKoid(view_pair_2.view_ref);
  {
    auto new_node = ViewTreeNewRefNodeTemplate(std::move(view_pair_2.view_ref), kThree);
    new_node.may_receive_focus = [] { return false; };
    tree.NewRefNode(std::move(new_node));
    tree.ConnectToParent(view_koid_2, attach_koid_2);
  }

  zx_koid_t attach_koid_3 = 3333u;
  tree.NewAttachNode(attach_koid_3);
  tree.ConnectToParent(attach_koid_3, view_koid_2);

  scenic::ViewRefPair view_pair_3 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_3 = ExtractKoid(view_pair_3.view_ref);

  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair_3.view_ref), kFour));
  tree.ConnectToParent(view_koid_3, attach_koid_3);

  ASSERT_EQ(tree.RequestFocusChange(scene_koid, view_koid_3), ViewTree::FocusChangeStatus::kAccept);
  ASSERT_EQ(tree.focus_chain().size(), 4u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_koid_1);
  EXPECT_EQ(tree.focus_chain()[2], view_koid_2);
  EXPECT_EQ(tree.focus_chain()[3], view_koid_3);

  // Detach view_koid_3 and read the focus chain.
  tree.DisconnectFromParent(view_koid_3);

  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
}

TEST(ViewTreePrimitive, NewRefNode) {
  ViewTree tree{};

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair.view_ref), kOne));

  EXPECT_TRUE(tree.IsTracked(view_koid));
}

TEST(ViewTreePrimitive, NewAttachNode) {
  ViewTree tree{};

  zx_koid_t attach_koid = 1111u;
  tree.NewAttachNode(attach_koid);

  EXPECT_TRUE(tree.IsTracked(attach_koid));
}

TEST(ViewTreePrimitive, DeleteNode) {
  ViewTree tree{};

  scenic::ViewRefPair scene_pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(scene_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(scene_pair.view_ref), kOne));

  zx_koid_t attach_koid = 1111u;
  tree.NewAttachNode(attach_koid);

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair.view_ref), kTwo));

  tree.DeleteNode(scene_koid);
  tree.DeleteNode(attach_koid);
  tree.DeleteNode(view_koid);

  EXPECT_FALSE(tree.IsTracked(scene_koid));
  EXPECT_FALSE(tree.IsTracked(attach_koid));
  EXPECT_FALSE(tree.IsTracked(view_koid));
}

TEST(ViewTreePrimitive, MakeGlobalRoot) {
  ViewTree tree{};

  tree.MakeGlobalRoot(ZX_KOID_INVALID);

  EXPECT_TRUE(tree.focus_chain().empty());

  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid);

  EXPECT_FALSE(tree.focus_chain().empty());
  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);

  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t scene_koid_2 = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair_2.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid_2);

  EXPECT_FALSE(tree.focus_chain().empty());
  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid_2);

  tree.MakeGlobalRoot(ZX_KOID_INVALID);

  EXPECT_TRUE(tree.focus_chain().empty());
}

// Perform descendant checks on the following view tree.
// Note how a_3/v_3 is disconnected from the scene.
//         scene
//        /    \
//      a_1    a_2
//       |      |
//      v_1    v_2
//              X
//             a_3
//              |
//             v_3
TEST(ViewTreePrimitive, IsDescendant) {
  ViewTree tree{};

  // Tree setup
  scenic::ViewRefPair scene_pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(scene_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(scene_pair.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid);

  // Koid is not descendant of itself.
  EXPECT_FALSE(tree.IsDescendant(scene_koid, scene_koid));

  zx_koid_t attach_koid_1 = 1111u;
  tree.NewAttachNode(attach_koid_1);
  tree.ConnectToParent(attach_koid_1, scene_koid);
  EXPECT_TRUE(tree.IsDescendant(attach_koid_1, scene_koid));

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid_1 = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair.view_ref), kTwo));
  tree.ConnectToParent(view_koid_1, attach_koid_1);
  // Should be descendent of scene (root), but not of itself or its descendant.
  EXPECT_TRUE(tree.IsDescendant(attach_koid_1, scene_koid));
  EXPECT_FALSE(tree.IsDescendant(attach_koid_1, attach_koid_1));
  EXPECT_FALSE(tree.IsDescendant(attach_koid_1, view_koid_1));
  EXPECT_TRUE(tree.IsDescendant(view_koid_1, attach_koid_1));

  zx_koid_t attach_koid_2 = 2222u;
  tree.NewAttachNode(attach_koid_2);
  tree.ConnectToParent(attach_koid_2, scene_koid);

  scenic::ViewRefPair view_pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_2 = ExtractKoid(view_pair_2.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair_2.view_ref), kThree));
  tree.ConnectToParent(view_koid_2, attach_koid_2);

  // Should be descendant of a_2 and scene, but not of a_1.
  EXPECT_TRUE(tree.IsDescendant(view_koid_2, scene_koid));
  EXPECT_TRUE(tree.IsDescendant(view_koid_2, attach_koid_2));
  EXPECT_FALSE(tree.IsDescendant(view_koid_2, attach_koid_1));
  tree.DisconnectFromParent(view_koid_2);
  // After disconnect it shouldn't be the descendant of anything.
  EXPECT_FALSE(tree.IsDescendant(view_koid_2, scene_koid));
  EXPECT_FALSE(tree.IsDescendant(view_koid_2, attach_koid_2));
  EXPECT_FALSE(tree.IsDescendant(view_koid_2, attach_koid_1));

  zx_koid_t attach_koid_3 = 3u;
  tree.NewAttachNode(attach_koid_3);
  // Do not connect to anything!

  scenic::ViewRefPair view_pair_3 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_3 = ExtractKoid(view_pair_3.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair_3.view_ref), kFive));
  tree.ConnectToParent(view_koid_3, attach_koid_3);

  EXPECT_TRUE(tree.IsDescendant(view_koid_3, attach_koid_3));
  EXPECT_FALSE(tree.IsDescendant(view_koid_3, scene_koid));
}

TEST(ViewTreePrimitive, IsConnected) {
  ViewTree tree{};

  // New scene, connected to scene by definition.
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid);

  EXPECT_TRUE(tree.IsConnectedToScene(scene_koid));

  // Replacement scene considered connected, old scene disconnected.
  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t scene_koid_2 = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair_2.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid_2);

  EXPECT_FALSE(tree.IsConnectedToScene(scene_koid));
  EXPECT_TRUE(tree.IsConnectedToScene(scene_koid_2));

  // New nodes not automatically connected.
  zx_koid_t attach = 1111u;
  tree.NewAttachNode(attach);

  EXPECT_FALSE(tree.IsConnectedToScene(attach));

  // Connect operation properly connects to scene.
  tree.ConnectToParent(attach, scene_koid_2);

  EXPECT_TRUE(tree.IsConnectedToScene(attach));

  // Disconnect operation really does disconnect.
  tree.DisconnectFromParent(attach);

  EXPECT_FALSE(tree.IsConnectedToScene(attach));
}

TEST(ViewTreePrimitive, IsRefNode) {
  ViewTree tree{};

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair.view_ref), kOne));

  EXPECT_TRUE(tree.IsRefNode(view_koid));

  zx_koid_t attach_koid = 1111u;
  tree.NewAttachNode(attach_koid);

  EXPECT_FALSE(tree.IsRefNode(attach_koid));
}

TEST(ViewTreePrimitive, AddAnnotationHolder) {
  ViewTree tree{};

  {
    scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
    zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);

    bool is_called = false;
    auto new_node = ViewTreeNewRefNodeTemplate(std::move(view_pair.view_ref), kOne);
    new_node.add_annotation_view_holder = [&is_called](auto) { is_called = true; };
    tree.NewRefNode(std::move(new_node));
    EXPECT_EQ(ZX_OK, tree.AddAnnotationViewHolder(view_koid, ViewHolderPtr()));
    EXPECT_TRUE(is_called);
  }

  {
    scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
    zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
    tree.NewAttachNode(view_koid);
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, tree.AddAnnotationViewHolder(view_koid, ViewHolderPtr()));
  }

  {
    scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
    zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, tree.AddAnnotationViewHolder(view_koid, ViewHolderPtr()));
  }
}

TEST(ViewTreePrimitive, MayReceiveFocus) {
  ViewTree tree{};

  {
    scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
    zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
    bool is_called = false;
    auto new_node = ViewTreeNewRefNodeTemplate(std::move(view_pair.view_ref), kOne);
    new_node.may_receive_focus = [&is_called] {
      is_called = true;
      return true;
    };
    tree.NewRefNode(std::move(new_node));
    EXPECT_TRUE(tree.MayReceiveFocus(view_koid));
    EXPECT_TRUE(is_called);
  }

  {
    scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
    zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
    bool is_called = false;

    auto new_node = ViewTreeNewRefNodeTemplate(std::move(view_pair.view_ref), kOne);
    new_node.may_receive_focus = [&is_called] {
      is_called = true;
      return false;
    };
    tree.NewRefNode(std::move(new_node));
    EXPECT_FALSE(tree.MayReceiveFocus(view_koid));
    EXPECT_TRUE(is_called);
  }
}

TEST(ViewTreePrimitive, HitTestFrom) {
  ViewTree tree{};

  scenic::ViewRefPair scene_pair = scenic::ViewRefPair::New();
  const zx_koid_t scene_koid = ExtractKoid(scene_pair.view_ref);
  bool hit_test1_triggered = false;
  {
    auto scene_node = ViewTreeNewRefNodeTemplate(std::move(scene_pair.view_ref), kOne);
    scene_node.hit_test = [&hit_test1_triggered](auto...) { hit_test1_triggered = true; };
    tree.NewRefNode(std::move(scene_node));
    tree.MakeGlobalRoot(scene_koid);
  }

  const zx_koid_t attach_koid = 1111u;
  {
    tree.NewAttachNode(attach_koid);
    tree.ConnectToParent(attach_koid, scene_koid);
  }

  scenic::ViewRefPair view_pair_1 = scenic::ViewRefPair::New();
  const zx_koid_t view_koid_1 = ExtractKoid(view_pair_1.view_ref);
  bool hit_test2_triggered = false;
  {
    auto new_node = ViewTreeNewRefNodeTemplate(std::move(view_pair_1.view_ref), kTwo);
    new_node.hit_test = [&hit_test2_triggered](auto...) { hit_test2_triggered = true; };
    tree.NewRefNode(std::move(new_node));
    tree.ConnectToParent(view_koid_1, attach_koid);
  }

  // Hit test should fire on the correct node.
  tree.HitTestFrom(scene_koid, {}, nullptr, /*semantic_visibility*/ false);
  EXPECT_TRUE(hit_test1_triggered);
  EXPECT_FALSE(hit_test2_triggered);
  tree.HitTestFrom(view_koid_1, {}, nullptr, /*semantic_visibility*/ false);
  EXPECT_TRUE(hit_test2_triggered);

  {  // Missing hit_test should crash in debug mode.
    EXPECT_DEBUG_DEATH(
        {
          scenic::ViewRefPair view_pair_2 = scenic::ViewRefPair::New();
          auto new_node = ViewTreeNewRefNodeTemplate(std::move(view_pair_2.view_ref), kThree);
          new_node.hit_test = nullptr;
          tree.NewRefNode(std::move(new_node));
        },
        "");
  }
}

TEST(ViewTreePrimitive, ConnectAndDisconnect) {
  ViewTree tree{};

  scenic::ViewRefPair scene_pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(scene_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(scene_pair.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid);

  zx_koid_t attach_koid = 1111u;
  tree.NewAttachNode(attach_koid);

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair.view_ref), kTwo));

  EXPECT_FALSE(tree.ParentOf(scene_koid).has_value());
  EXPECT_FALSE(tree.ParentOf(attach_koid).has_value());
  EXPECT_FALSE(tree.ParentOf(view_koid).has_value());

  tree.ConnectToParent(attach_koid, scene_koid);

  EXPECT_FALSE(tree.ParentOf(scene_koid).has_value());
  EXPECT_TRUE(tree.ParentOf(attach_koid).has_value());
  EXPECT_EQ(tree.ParentOf(attach_koid).value(), scene_koid);
  EXPECT_FALSE(tree.ParentOf(view_koid).has_value());

  tree.ConnectToParent(view_koid, attach_koid);

  EXPECT_FALSE(tree.ParentOf(scene_koid).has_value());
  EXPECT_TRUE(tree.ParentOf(attach_koid).has_value());
  EXPECT_EQ(tree.ParentOf(attach_koid).value(), scene_koid);
  EXPECT_TRUE(tree.ParentOf(view_koid).has_value());
  EXPECT_EQ(tree.ParentOf(view_koid).value(), attach_koid);

  tree.DisconnectFromParent(attach_koid);

  EXPECT_FALSE(tree.ParentOf(scene_koid).has_value());
  EXPECT_FALSE(tree.ParentOf(attach_koid).has_value());
  EXPECT_TRUE(tree.ParentOf(view_koid).has_value());
  EXPECT_EQ(tree.ParentOf(view_koid).value(), attach_koid);

  tree.DisconnectFromParent(view_koid);

  EXPECT_FALSE(tree.ParentOf(scene_koid).has_value());
  EXPECT_FALSE(tree.ParentOf(attach_koid).has_value());
  EXPECT_FALSE(tree.ParentOf(view_koid).has_value());
}

TEST(ViewTreePrimitive, DisconnectUnconnectedChild) {
  ViewTree tree{};

  scenic::ViewRefPair ref_pair = scenic::ViewRefPair::New();
  zx_koid_t ref_koid = ExtractKoid(ref_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(ref_pair.view_ref), kOne));

  tree.DisconnectFromParent(ref_koid);

  EXPECT_TRUE(tree.IsTracked(ref_koid));
  EXPECT_FALSE(tree.ParentOf(ref_koid).has_value());

  zx_koid_t attach_koid = 1111u;
  tree.NewAttachNode(attach_koid);

  tree.DisconnectFromParent(attach_koid);

  EXPECT_TRUE(tree.IsTracked(attach_koid));
  EXPECT_FALSE(tree.ParentOf(attach_koid).has_value());
}

TEST(ViewTreePrimitive, DeleteParentThenDisconnectChild) {
  // 1. RefNode parent, AttachNode child
  {
    ViewTree tree{};

    scenic::ViewRefPair ref_pair = scenic::ViewRefPair::New();
    zx_koid_t ref_koid = ExtractKoid(ref_pair.view_ref);
    tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(ref_pair.view_ref), kOne));

    zx_koid_t attach_koid = 1111u;
    tree.NewAttachNode(attach_koid);
    tree.ConnectToParent(attach_koid, ref_koid);

    EXPECT_EQ(tree.ParentOf(attach_koid), ref_koid);

    tree.DeleteNode(ref_koid);
    tree.DisconnectFromParent(attach_koid);

    EXPECT_FALSE(tree.IsTracked(ref_koid));
    EXPECT_TRUE(tree.IsTracked(attach_koid));
  }

  // 2. AttachNode parent, RefNode child
  {
    ViewTree tree{};

    zx_koid_t attach_koid = 1111u;
    tree.NewAttachNode(attach_koid);

    scenic::ViewRefPair ref_pair = scenic::ViewRefPair::New();
    zx_koid_t ref_koid = ExtractKoid(ref_pair.view_ref);
    tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(ref_pair.view_ref), kOne));
    tree.ConnectToParent(ref_koid, attach_koid);

    EXPECT_EQ(tree.ParentOf(ref_koid), attach_koid);

    tree.DeleteNode(attach_koid);
    tree.DisconnectFromParent(ref_koid);

    EXPECT_FALSE(tree.IsTracked(attach_koid));
    EXPECT_TRUE(tree.IsTracked(ref_koid));
  }
}

// Exercise focus transfer policies on the following view tree.
// Note how v_4 is disconnected from the scene.
//         scene
//        /    \
//      a_1    a_2
//       |      |
//      v_1    v_2
//       |      X
//      a_3    a_4
//       |      |
//      v_3    v_4
TEST(ViewTreePrimitive, RequestFocusChange) {
  ViewTree tree{};

  // Tree setup
  scenic::ViewRefPair scene_pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(scene_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(scene_pair.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid);

  zx_koid_t attach_koid_1 = 1111u;
  tree.NewAttachNode(attach_koid_1);
  tree.ConnectToParent(attach_koid_1, scene_koid);

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid_1 = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair.view_ref), kTwo));
  tree.ConnectToParent(view_koid_1, attach_koid_1);

  zx_koid_t attach_koid_2 = 2222u;
  tree.NewAttachNode(attach_koid_2);
  tree.ConnectToParent(attach_koid_2, scene_koid);

  scenic::ViewRefPair view_pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_2 = ExtractKoid(view_pair_2.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair_2.view_ref), kThree));
  tree.ConnectToParent(view_koid_2, attach_koid_2);

  zx_koid_t attach_koid_3 = 3333u;
  tree.NewAttachNode(attach_koid_3);
  tree.ConnectToParent(attach_koid_3, view_koid_1);

  scenic::ViewRefPair view_pair_3 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_3 = ExtractKoid(view_pair_3.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair_3.view_ref), kFour));
  tree.ConnectToParent(view_koid_3, attach_koid_3);

  zx_koid_t attach_koid_4 = 4444u;
  tree.NewAttachNode(attach_koid_4);
  // Do not connect to view_koid_2!

  scenic::ViewRefPair view_pair_4 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_4 = ExtractKoid(view_pair_4.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair_4.view_ref), kFive));
  tree.ConnectToParent(view_koid_4, attach_koid_4);

  // Transfer requests.

  // scene -> v_1: allow
  EXPECT_EQ(tree.RequestFocusChange(scene_koid, view_koid_1), ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_koid_1);

  // v_1 -> v_3: allow
  EXPECT_EQ(tree.RequestFocusChange(view_koid_1, view_koid_3),
            ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_koid_1);
  EXPECT_EQ(tree.focus_chain()[2], view_koid_3);

  // v_3 -> invalid: deny
  EXPECT_EQ(tree.RequestFocusChange(view_koid_3, ZX_KOID_INVALID),
            ViewTree::FocusChangeStatus::kErrorRequestInvalid);
  EXPECT_EQ(tree.focus_chain().size(), 3u);

  // v_3 -> no_such: deny
  EXPECT_EQ(tree.RequestFocusChange(view_koid_3, /* does not exist */ 1234u),
            ViewTree::FocusChangeStatus::kErrorRequestInvalid);
  EXPECT_EQ(tree.focus_chain().size(), 3u);

  // v_3 -> v_1: deny
  EXPECT_EQ(tree.RequestFocusChange(view_koid_3, view_koid_1),
            ViewTree::FocusChangeStatus::kErrorRequestorNotRequestAncestor);
  EXPECT_EQ(tree.focus_chain().size(), 3u);

  // v_3 -> v_2: deny
  EXPECT_EQ(tree.RequestFocusChange(view_koid_3, view_koid_2),
            ViewTree::FocusChangeStatus::kErrorRequestorNotRequestAncestor);
  EXPECT_EQ(tree.focus_chain().size(), 3u);

  // v_1 -> v_1: allow
  EXPECT_EQ(tree.RequestFocusChange(view_koid_1, view_koid_1),
            ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(tree.focus_chain().size(), 2u);

  // scene -> scene: allow
  EXPECT_EQ(tree.RequestFocusChange(scene_koid, scene_koid), ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(tree.focus_chain().size(), 1u);

  // scene -> v_2: allow
  EXPECT_EQ(tree.RequestFocusChange(scene_koid, view_koid_2), ViewTree::FocusChangeStatus::kAccept);
  EXPECT_EQ(tree.focus_chain().size(), 2u);

  // v_2 -> scene: deny
  EXPECT_EQ(tree.RequestFocusChange(view_koid_2, scene_koid),
            ViewTree::FocusChangeStatus::kErrorRequestorNotRequestAncestor);
  EXPECT_EQ(tree.focus_chain().size(), 2u);

  // v_2 -> v_1: deny
  EXPECT_EQ(tree.RequestFocusChange(view_koid_2, view_koid_1),
            ViewTree::FocusChangeStatus::kErrorRequestorNotRequestAncestor);
  EXPECT_EQ(tree.focus_chain().size(), 2u);

  // v_2 -> v_3: deny
  EXPECT_EQ(tree.RequestFocusChange(view_koid_2, view_koid_3),
            ViewTree::FocusChangeStatus::kErrorRequestorNotRequestAncestor);
  EXPECT_EQ(tree.focus_chain().size(), 2u);

  // scene -> v_4: deny
  EXPECT_EQ(tree.RequestFocusChange(scene_koid, view_koid_4),
            ViewTree::FocusChangeStatus::kErrorRequestInvalid);
  EXPECT_EQ(tree.focus_chain().size(), 2u);
}

TEST(ViewTreePrimitive, RequestFocusChangeDeniedIfUnfocusable) {
  ViewTree tree{};

  // Tree setup
  scenic::ViewRefPair scene_pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(scene_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(scene_pair.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid);

  zx_koid_t attach_koid = 1111u;
  tree.NewAttachNode(attach_koid);
  tree.ConnectToParent(attach_koid, scene_koid);

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
  auto new_node = ViewTreeNewRefNodeTemplate(std::move(view_pair.view_ref), kTwo);
  new_node.may_receive_focus = [] { return false; };
  tree.NewRefNode(std::move(new_node));
  tree.ConnectToParent(view_koid, attach_koid);

  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);

  // Request change of focus, see correct denial, and focus chain should not change.
  EXPECT_EQ(tree.RequestFocusChange(scene_koid, view_koid),
            ViewTree::FocusChangeStatus::kErrorRequestCannotReceiveFocus);
  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
}

}  // namespace lib_ui_gfx_engine_tests
