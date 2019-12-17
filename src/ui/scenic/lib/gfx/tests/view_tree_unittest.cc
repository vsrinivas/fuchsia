// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/view_tree.h"

#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include "gtest/gtest.h"
#include "src/ui/scenic/lib/gfx/id.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace lib_ui_gfx_engine_tests {

using fuchsia::ui::focus::FocusChain;
using fuchsia::ui::views::ViewRef;
using scenic_impl::EventReporterWeakPtr;
using scenic_impl::gfx::ExtractKoid;
using scenic_impl::gfx::ViewTree;

const scenic_impl::SessionId kOne = 1u, kTwo = 2u, kThree = 3u, kFour = 4u, kFive = 5u;

fit::function<bool()> MayReceiveFocus() {
  return [] { return true; };  // Most views may receive focus in these tests.
}

fit::function<std::optional<glm::mat4>()> NoGlobalTransform() {
  return [] { return std::nullopt; };  // Global transform is not used by these tests.
}

TEST(ViewTreeLifecycle, EmptyScene) {
  ViewTree tree{};

  EXPECT_TRUE(tree.focus_chain().empty());

  EXPECT_TRUE(tree.CloneFocusChain().IsEmpty());

  EXPECT_TRUE(tree.IsStateValid());
}

TEST(ViewTreeLifecycle, SceneCreateThenDestroy) {
  ViewTree tree{};
  EventReporterWeakPtr no_reporter{};

  // Create a scene node.
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(std::move(pair.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kOne);
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
  EventReporterWeakPtr no_reporter{};

  // Create a scene node.
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(std::move(pair.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kOne);
  tree.MakeGlobalRoot(scene_koid);

  // Replace it with another scene node.
  scenic::ViewRefPair pair_b = scenic::ViewRefPair::New();
  zx_koid_t scene_koid_b = ExtractKoid(pair_b.view_ref);
  tree.NewRefNode(std::move(pair_b.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kOne);
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
  EventReporterWeakPtr no_reporter{};

  // Create a scene node.
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(std::move(pair.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kOne);
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
  tree.NewRefNode(std::move(pair_1.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kTwo);
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
  tree.NewRefNode(std::move(pair_2.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kThree);
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
  EventReporterWeakPtr no_reporter{};

  // Create a scene, attach 1, view 1, attach 2, view 2 in one deep hierarchy..
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(std::move(pair.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kOne);
  tree.MakeGlobalRoot(scene_koid);

  zx_koid_t attach_1_koid = 1111u;
  tree.NewAttachNode(attach_1_koid);
  tree.ConnectToParent(attach_1_koid, scene_koid);

  scenic::ViewRefPair pair_1 = scenic::ViewRefPair::New();
  zx_koid_t view_1_koid = ExtractKoid(pair_1.view_ref);
  tree.NewRefNode(std::move(pair_1.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kTwo);
  tree.ConnectToParent(view_1_koid, attach_1_koid);

  zx_koid_t attach_2_koid = 2222u;
  tree.NewAttachNode(attach_2_koid);
  tree.ConnectToParent(attach_2_koid, view_1_koid);

  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_2_koid = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(std::move(pair_2.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kThree);
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
  EventReporterWeakPtr no_reporter{};

  // Create a scene, attach 1, view 1, attach 2, view 2 in one deep hierarchy.
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(std::move(pair.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kOne);
  tree.MakeGlobalRoot(scene_koid);

  zx_koid_t attach_1_koid = 1111u;
  tree.NewAttachNode(attach_1_koid);
  tree.ConnectToParent(attach_1_koid, scene_koid);

  scenic::ViewRefPair pair_1 = scenic::ViewRefPair::New();
  zx_koid_t view_1_koid = ExtractKoid(pair_1.view_ref);
  tree.NewRefNode(std::move(pair_1.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kTwo);
  tree.ConnectToParent(view_1_koid, attach_1_koid);

  zx_koid_t attach_2_koid = 2222u;
  tree.NewAttachNode(attach_2_koid);
  tree.ConnectToParent(attach_2_koid, view_1_koid);

  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_2_koid = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(std::move(pair_2.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kThree);
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
  EventReporterWeakPtr no_reporter{};

  // Tree setup
  scenic::ViewRefPair scene_pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(scene_pair.view_ref);
  tree.NewRefNode(std::move(scene_pair.view_ref), no_reporter, MayReceiveFocus(),
                  NoGlobalTransform(), kOne);
  tree.MakeGlobalRoot(scene_koid);

  zx_koid_t attach_koid_1 = 1111u;
  tree.NewAttachNode(attach_koid_1);
  tree.ConnectToParent(attach_koid_1, scene_koid);

  scenic::ViewRefPair view_pair_1 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_1 = ExtractKoid(view_pair_1.view_ref);
  tree.NewRefNode(
      std::move(view_pair_1.view_ref), no_reporter, [] { return false; }, NoGlobalTransform(),
      kTwo);
  tree.ConnectToParent(view_koid_1, attach_koid_1);

  zx_koid_t attach_koid_2 = 2222u;
  tree.NewAttachNode(attach_koid_2);
  tree.ConnectToParent(attach_koid_2, view_koid_1);

  scenic::ViewRefPair view_pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_2 = ExtractKoid(view_pair_2.view_ref);
  tree.NewRefNode(
      std::move(view_pair_2.view_ref), no_reporter, [] { return false; }, NoGlobalTransform(),
      kThree);
  tree.ConnectToParent(view_koid_2, attach_koid_2);

  zx_koid_t attach_koid_3 = 3333u;
  tree.NewAttachNode(attach_koid_3);
  tree.ConnectToParent(attach_koid_3, view_koid_2);

  scenic::ViewRefPair view_pair_3 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_3 = ExtractKoid(view_pair_3.view_ref);
  tree.NewRefNode(std::move(view_pair_3.view_ref), no_reporter, MayReceiveFocus(),
                  NoGlobalTransform(), kFour);
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
  EventReporterWeakPtr no_reporter{};

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(std::move(view_pair.view_ref), no_reporter, MayReceiveFocus(),
                  NoGlobalTransform(), kOne);

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
  EventReporterWeakPtr no_reporter{};

  scenic::ViewRefPair scene_pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(scene_pair.view_ref);
  tree.NewRefNode(std::move(scene_pair.view_ref), no_reporter, MayReceiveFocus(),
                  NoGlobalTransform(), kOne);

  zx_koid_t attach_koid = 1111u;
  tree.NewAttachNode(attach_koid);

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(std::move(view_pair.view_ref), no_reporter, MayReceiveFocus(),
                  NoGlobalTransform(), kTwo);

  tree.DeleteNode(scene_koid);
  tree.DeleteNode(attach_koid);
  tree.DeleteNode(view_koid);

  EXPECT_FALSE(tree.IsTracked(scene_koid));
  EXPECT_FALSE(tree.IsTracked(attach_koid));
  EXPECT_FALSE(tree.IsTracked(view_koid));
}

TEST(ViewTreePrimitive, MakeGlobalRoot) {
  ViewTree tree{};
  EventReporterWeakPtr no_reporter{};

  tree.MakeGlobalRoot(ZX_KOID_INVALID);

  EXPECT_TRUE(tree.focus_chain().empty());

  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(std::move(pair.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kOne);
  tree.MakeGlobalRoot(scene_koid);

  EXPECT_FALSE(tree.focus_chain().empty());
  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);

  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t scene_koid_2 = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(std::move(pair_2.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kOne);
  tree.MakeGlobalRoot(scene_koid_2);

  EXPECT_FALSE(tree.focus_chain().empty());
  ASSERT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid_2);

  tree.MakeGlobalRoot(ZX_KOID_INVALID);

  EXPECT_TRUE(tree.focus_chain().empty());
}

TEST(ViewTreePrimitive, IsConnected) {
  ViewTree tree{};
  EventReporterWeakPtr no_reporter{};

  // New scene, connected to scene by definition.
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(std::move(pair.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kOne);
  tree.MakeGlobalRoot(scene_koid);

  EXPECT_TRUE(tree.IsConnected(scene_koid));

  // Replacement scene considered connected, old scene disconnected.
  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t scene_koid_2 = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(std::move(pair_2.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kOne);
  tree.MakeGlobalRoot(scene_koid_2);

  EXPECT_FALSE(tree.IsConnected(scene_koid));
  EXPECT_TRUE(tree.IsConnected(scene_koid_2));

  // New nodes not automatically connected.
  zx_koid_t attach = 1111u;
  tree.NewAttachNode(attach);

  EXPECT_FALSE(tree.IsConnected(attach));

  // Connect operation properly connects to scene.
  tree.ConnectToParent(attach, scene_koid_2);

  EXPECT_TRUE(tree.IsConnected(attach));

  // Disconnect operation really does disconnect.
  tree.DisconnectFromParent(attach);

  EXPECT_FALSE(tree.IsConnected(attach));
}

TEST(ViewTreePrimitive, IsRefNode) {
  ViewTree tree{};
  EventReporterWeakPtr no_reporter{};

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(std::move(view_pair.view_ref), no_reporter, MayReceiveFocus(),
                  NoGlobalTransform(), kOne);

  EXPECT_TRUE(tree.IsRefNode(view_koid));

  zx_koid_t attach_koid = 1111u;
  tree.NewAttachNode(attach_koid);

  EXPECT_FALSE(tree.IsRefNode(attach_koid));
}

TEST(ViewTreePrimitive, MayReceiveFocus) {
  ViewTree tree{};
  EventReporterWeakPtr no_reporter{};

  {
    scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
    zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
    bool is_called = false;
    tree.NewRefNode(
        std::move(view_pair.view_ref), no_reporter,
        [&is_called] {
          is_called = true;
          return true;
        },
        NoGlobalTransform(), kOne);
    EXPECT_TRUE(tree.MayReceiveFocus(view_koid));
    EXPECT_TRUE(is_called);
  }

  {
    scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
    zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
    bool is_called = false;
    tree.NewRefNode(
        std::move(view_pair.view_ref), no_reporter,
        [&is_called] {
          is_called = true;
          return false;
        },
        NoGlobalTransform(), kOne);
    EXPECT_FALSE(tree.MayReceiveFocus(view_koid));
    EXPECT_TRUE(is_called);
  }
}

TEST(ViewTreePrimitive, ConnectAndDisconnect) {
  ViewTree tree{};
  EventReporterWeakPtr no_reporter{};

  scenic::ViewRefPair scene_pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(scene_pair.view_ref);
  tree.NewRefNode(std::move(scene_pair.view_ref), no_reporter, MayReceiveFocus(),
                  NoGlobalTransform(), kOne);
  tree.MakeGlobalRoot(scene_koid);

  zx_koid_t attach_koid = 1111u;
  tree.NewAttachNode(attach_koid);

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(std::move(view_pair.view_ref), no_reporter, MayReceiveFocus(),
                  NoGlobalTransform(), kTwo);

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
  EventReporterWeakPtr no_reporter{};

  scenic::ViewRefPair ref_pair = scenic::ViewRefPair::New();
  zx_koid_t ref_koid = ExtractKoid(ref_pair.view_ref);
  tree.NewRefNode(std::move(ref_pair.view_ref), no_reporter, MayReceiveFocus(), NoGlobalTransform(),
                  kOne);

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
    EventReporterWeakPtr no_reporter{};

    scenic::ViewRefPair ref_pair = scenic::ViewRefPair::New();
    zx_koid_t ref_koid = ExtractKoid(ref_pair.view_ref);
    tree.NewRefNode(std::move(ref_pair.view_ref), no_reporter, MayReceiveFocus(),
                    NoGlobalTransform(), kOne);

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
    EventReporterWeakPtr no_reporter{};

    zx_koid_t attach_koid = 1111u;
    tree.NewAttachNode(attach_koid);

    scenic::ViewRefPair ref_pair = scenic::ViewRefPair::New();
    zx_koid_t ref_koid = ExtractKoid(ref_pair.view_ref);
    tree.NewRefNode(std::move(ref_pair.view_ref), no_reporter, MayReceiveFocus(),
                    NoGlobalTransform(), kOne);
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
  EventReporterWeakPtr no_reporter{};

  // Tree setup
  scenic::ViewRefPair scene_pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(scene_pair.view_ref);
  tree.NewRefNode(std::move(scene_pair.view_ref), no_reporter, MayReceiveFocus(),
                  NoGlobalTransform(), kOne);
  tree.MakeGlobalRoot(scene_koid);

  zx_koid_t attach_koid_1 = 1111u;
  tree.NewAttachNode(attach_koid_1);
  tree.ConnectToParent(attach_koid_1, scene_koid);

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid_1 = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(std::move(view_pair.view_ref), no_reporter, MayReceiveFocus(),
                  NoGlobalTransform(), kTwo);
  tree.ConnectToParent(view_koid_1, attach_koid_1);

  zx_koid_t attach_koid_2 = 2222u;
  tree.NewAttachNode(attach_koid_2);
  tree.ConnectToParent(attach_koid_2, scene_koid);

  scenic::ViewRefPair view_pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_2 = ExtractKoid(view_pair_2.view_ref);
  tree.NewRefNode(std::move(view_pair_2.view_ref), no_reporter, MayReceiveFocus(),
                  NoGlobalTransform(), kThree);
  tree.ConnectToParent(view_koid_2, attach_koid_2);

  zx_koid_t attach_koid_3 = 3333u;
  tree.NewAttachNode(attach_koid_3);
  tree.ConnectToParent(attach_koid_3, view_koid_1);

  scenic::ViewRefPair view_pair_3 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_3 = ExtractKoid(view_pair_3.view_ref);
  tree.NewRefNode(std::move(view_pair_3.view_ref), no_reporter, MayReceiveFocus(),
                  NoGlobalTransform(), kFour);
  tree.ConnectToParent(view_koid_3, attach_koid_3);

  zx_koid_t attach_koid_4 = 4444u;
  tree.NewAttachNode(attach_koid_4);
  // Do not connect to view_koid_2!

  scenic::ViewRefPair view_pair_4 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_4 = ExtractKoid(view_pair_4.view_ref);
  tree.NewRefNode(std::move(view_pair_4.view_ref), no_reporter, MayReceiveFocus(),
                  NoGlobalTransform(), kFive);
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
  EventReporterWeakPtr no_reporter{};

  // Tree setup
  scenic::ViewRefPair scene_pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(scene_pair.view_ref);
  tree.NewRefNode(std::move(scene_pair.view_ref), no_reporter, MayReceiveFocus(),
                  NoGlobalTransform(), kOne);
  tree.MakeGlobalRoot(scene_koid);

  zx_koid_t attach_koid = 1111u;
  tree.NewAttachNode(attach_koid);
  tree.ConnectToParent(attach_koid, scene_koid);

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(
      std::move(view_pair.view_ref), no_reporter, [] { return false; }, NoGlobalTransform(), kTwo);
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
