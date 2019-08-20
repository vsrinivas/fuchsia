// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/view_tree.h"

#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include "gtest/gtest.h"

namespace lib_ui_gfx_engine_tests {

using fuchsia::ui::focus::FocusChain;
using fuchsia::ui::views::ViewRef;
using scenic_impl::gfx::ViewTree;

zx_koid_t ExtractKoid(const fuchsia::ui::views::ViewRef& view_ref) {
  zx_info_handle_basic_t info{};
  if (view_ref.reference.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) !=
      ZX_OK) {
    return ZX_KOID_INVALID;  // no info
  }

  return info.koid;
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
  tree.NewRefNode(std::move(pair.view_ref));
  tree.MakeRoot(koid);

  EXPECT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], koid);

  FocusChain clone = tree.CloneFocusChain();
  EXPECT_FALSE(clone.IsEmpty());
  EXPECT_EQ(clone.focus_chain().size(), 1u);

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
  tree.NewRefNode(std::move(pair.view_ref));
  tree.MakeRoot(scene_koid);

  // Replace it with another scene node.
  scenic::ViewRefPair pair_b = scenic::ViewRefPair::New();
  zx_koid_t scene_koid_b = ExtractKoid(pair_b.view_ref);
  tree.NewRefNode(std::move(pair_b.view_ref));
  tree.MakeRoot(scene_koid_b);

  EXPECT_EQ(tree.focus_chain().size(), 1u);
  FocusChain clone = tree.CloneFocusChain();
  EXPECT_EQ(clone.focus_chain().size(), 1u);

  const ViewRef& root = clone.focus_chain()[0];
  EXPECT_EQ(scene_koid_b, ExtractKoid(root));

  EXPECT_TRUE(tree.IsStateValid());
}

TEST(ViewTreeLifecycle, ConnectedSceneWithFocusTransfer) {
  ViewTree tree{};

  // Create a scene node.
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(std::move(pair.view_ref));
  tree.MakeRoot(scene_koid);

  // Create an attach node for view 1, connect to scene.
  zx_koid_t attach_1_koid = 1111u;
  tree.NewAttachNode(attach_1_koid);
  tree.ConnectToParent(attach_1_koid, scene_koid);

  EXPECT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Create a view node, attach it.
  scenic::ViewRefPair pair_1 = scenic::ViewRefPair::New();
  zx_koid_t view_1_koid = ExtractKoid(pair_1.view_ref);
  tree.NewRefNode(std::move(pair_1.view_ref));
  tree.ConnectToParent(view_1_koid, attach_1_koid);

  EXPECT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Create a attach node for view 2, connect to scene.
  zx_koid_t attach_2_koid = 2222u;
  tree.NewAttachNode(attach_2_koid);
  tree.ConnectToParent(attach_2_koid, scene_koid);

  EXPECT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Create a view node, attach it.
  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_2_koid = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(std::move(pair_2.view_ref));
  tree.ConnectToParent(view_2_koid, attach_2_koid);

  // Transfer focus: scene to view 2.
  tree.RequestFocusChange(scene_koid, view_2_koid);

  EXPECT_EQ(tree.focus_chain().size(), 2u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_2_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Destroy view 2.
  tree.DeleteNode(view_2_koid);

  EXPECT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Transfer focus, scene to child 1
  tree.RequestFocusChange(scene_koid, view_1_koid);

  EXPECT_EQ(tree.focus_chain().size(), 2u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_1_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Destroy attach 1.
  tree.DeleteNode(attach_1_koid);

  EXPECT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_TRUE(tree.IsStateValid());
}

TEST(ViewTreeLifecycle, SlowlyDestroyedScene) {
  ViewTree tree{};

  // Create a scene, attach 1, view 1, attach 2, view 2 in one deep hierarchy..
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(std::move(pair.view_ref));
  tree.MakeRoot(scene_koid);

  zx_koid_t attach_1_koid = 1111u;
  tree.NewAttachNode(attach_1_koid);
  tree.ConnectToParent(attach_1_koid, scene_koid);

  scenic::ViewRefPair pair_1 = scenic::ViewRefPair::New();
  zx_koid_t view_1_koid = ExtractKoid(pair_1.view_ref);
  tree.NewRefNode(std::move(pair_1.view_ref));
  tree.ConnectToParent(view_1_koid, attach_1_koid);

  zx_koid_t attach_2_koid = 2222u;
  tree.NewAttachNode(attach_2_koid);
  tree.ConnectToParent(attach_2_koid, view_1_koid);

  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_2_koid = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(std::move(pair_2.view_ref));
  tree.ConnectToParent(view_2_koid, attach_2_koid);

  EXPECT_TRUE(tree.IsStateValid());

  // Transfer focus to view 2.
  tree.RequestFocusChange(scene_koid, view_2_koid);

  EXPECT_EQ(tree.focus_chain().size(), 3u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_1_koid);
  EXPECT_EQ(tree.focus_chain()[2], view_2_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Destroy view 2.
  tree.DeleteNode(view_2_koid);

  EXPECT_EQ(tree.focus_chain().size(), 2u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_1_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Destroy view 1.
  tree.DeleteNode(view_1_koid);

  EXPECT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Destroy scene.
  tree.DeleteNode(scene_koid);

  EXPECT_EQ(tree.focus_chain().size(), 0u);
  EXPECT_TRUE(tree.IsStateValid());
}

TEST(ViewTreeLifecycle, SlowlyDisconnectedScene) {
  ViewTree tree{};

  // Create a scene, attach 1, view 1, attach 2, view 2 in one deep hierarchy..
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(std::move(pair.view_ref));
  tree.MakeRoot(scene_koid);

  zx_koid_t attach_1_koid = 1111u;
  tree.NewAttachNode(attach_1_koid);
  tree.ConnectToParent(attach_1_koid, scene_koid);

  scenic::ViewRefPair pair_1 = scenic::ViewRefPair::New();
  zx_koid_t view_1_koid = ExtractKoid(pair_1.view_ref);
  tree.NewRefNode(std::move(pair_1.view_ref));
  tree.ConnectToParent(view_1_koid, attach_1_koid);

  zx_koid_t attach_2_koid = 2222u;
  tree.NewAttachNode(attach_2_koid);
  tree.ConnectToParent(attach_2_koid, view_1_koid);

  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_2_koid = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(std::move(pair_2.view_ref));
  tree.ConnectToParent(view_2_koid, attach_2_koid);

  EXPECT_TRUE(tree.IsStateValid());

  // Transfer focus to view 2.
  tree.RequestFocusChange(scene_koid, view_2_koid);

  EXPECT_EQ(tree.focus_chain().size(), 3u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_1_koid);
  EXPECT_EQ(tree.focus_chain()[2], view_2_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Disconnect view 2.
  tree.DisconnectFromParent(view_2_koid);

  EXPECT_EQ(tree.focus_chain().size(), 2u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_1_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Disconnect view 1.
  tree.DisconnectFromParent(view_1_koid);

  EXPECT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_TRUE(tree.IsStateValid());
}

TEST(ViewTreePrimitive, NewRefNode) {
  ViewTree tree{};

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(std::move(view_pair.view_ref));

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
  tree.NewRefNode(std::move(scene_pair.view_ref));

  zx_koid_t attach_koid = 1111u;
  tree.NewAttachNode(attach_koid);

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(std::move(view_pair.view_ref));

  tree.DeleteNode(scene_koid);
  tree.DeleteNode(attach_koid);
  tree.DeleteNode(view_koid);

  EXPECT_FALSE(tree.IsTracked(scene_koid));
  EXPECT_FALSE(tree.IsTracked(attach_koid));
  EXPECT_FALSE(tree.IsTracked(view_koid));
}

TEST(ViewTreePrimitive, MakeRoot) {
  ViewTree tree{};

  tree.MakeRoot(ZX_KOID_INVALID);

  EXPECT_TRUE(tree.focus_chain().empty());

  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(std::move(pair.view_ref));
  tree.MakeRoot(scene_koid);

  EXPECT_FALSE(tree.focus_chain().empty());
  EXPECT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);

  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t scene_koid_2 = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(std::move(pair_2.view_ref));
  tree.MakeRoot(scene_koid_2);

  EXPECT_FALSE(tree.focus_chain().empty());
  EXPECT_EQ(tree.focus_chain().size(), 1u);
  EXPECT_EQ(tree.focus_chain()[0], scene_koid_2);

  tree.MakeRoot(ZX_KOID_INVALID);

  EXPECT_TRUE(tree.focus_chain().empty());
}

TEST(ViewTreePrimitive, IsConnected) {
  ViewTree tree{};

  // New scene, connected to scene by definition.
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(std::move(pair.view_ref));
  tree.MakeRoot(scene_koid);

  EXPECT_TRUE(tree.IsConnected(scene_koid));

  // Replacement scene considered connected, old scene disconnected.
  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t scene_koid_2 = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(std::move(pair_2.view_ref));
  tree.MakeRoot(scene_koid_2);

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

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(std::move(view_pair.view_ref));

  EXPECT_TRUE(tree.IsRefNode(view_koid));

  zx_koid_t attach_koid = 1111u;
  tree.NewAttachNode(attach_koid);

  EXPECT_FALSE(tree.IsRefNode(attach_koid));
}

TEST(ViewTreePrimitive, ConnectAndDisconnect) {
  ViewTree tree{};

  scenic::ViewRefPair scene_pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(scene_pair.view_ref);
  tree.NewRefNode(std::move(scene_pair.view_ref));
  tree.MakeRoot(scene_koid);

  zx_koid_t attach_koid = 1111u;
  tree.NewAttachNode(attach_koid);

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(std::move(view_pair.view_ref));

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
  tree.NewRefNode(std::move(scene_pair.view_ref));
  tree.MakeRoot(scene_koid);

  zx_koid_t attach_koid_1 = 1111u;
  tree.NewAttachNode(attach_koid_1);
  tree.ConnectToParent(attach_koid_1, scene_koid);

  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid_1 = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(std::move(view_pair.view_ref));
  tree.ConnectToParent(view_koid_1, attach_koid_1);

  zx_koid_t attach_koid_2 = 2222u;
  tree.NewAttachNode(attach_koid_2);
  tree.ConnectToParent(attach_koid_2, scene_koid);

  scenic::ViewRefPair view_pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_2 = ExtractKoid(view_pair_2.view_ref);
  tree.NewRefNode(std::move(view_pair_2.view_ref));
  tree.ConnectToParent(view_koid_2, attach_koid_2);

  zx_koid_t attach_koid_3 = 3333u;
  tree.NewAttachNode(attach_koid_3);
  tree.ConnectToParent(attach_koid_3, view_koid_1);

  scenic::ViewRefPair view_pair_3 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_3 = ExtractKoid(view_pair_3.view_ref);
  tree.NewRefNode(std::move(view_pair_3.view_ref));
  tree.ConnectToParent(view_koid_3, attach_koid_3);

  zx_koid_t attach_koid_4 = 4444u;
  tree.NewAttachNode(attach_koid_4);
  // Do not connect to view_koid_2!

  scenic::ViewRefPair view_pair_4 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_4 = ExtractKoid(view_pair_4.view_ref);
  tree.NewRefNode(std::move(view_pair_4.view_ref));
  tree.ConnectToParent(view_koid_4, attach_koid_4);

  // Transfer requests.

  // scene -> v_1: allow
  EXPECT_TRUE(tree.RequestFocusChange(scene_koid, view_koid_1));
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_koid_1);

  // v_1 -> v_3: allow
  EXPECT_TRUE(tree.RequestFocusChange(view_koid_1, view_koid_3));
  EXPECT_EQ(tree.focus_chain()[0], scene_koid);
  EXPECT_EQ(tree.focus_chain()[1], view_koid_1);
  EXPECT_EQ(tree.focus_chain()[2], view_koid_3);

  // v_3 -> invalid: deny
  EXPECT_FALSE(tree.RequestFocusChange(view_koid_3, ZX_KOID_INVALID));
  EXPECT_EQ(tree.focus_chain().size(), 3u);

  // v_3 -> no_such: deny
  EXPECT_FALSE(tree.RequestFocusChange(view_koid_3, /* does not exist */ 1234u));
  EXPECT_EQ(tree.focus_chain().size(), 3u);

  // v_3 -> v_1: deny
  EXPECT_FALSE(tree.RequestFocusChange(view_koid_3, view_koid_1));
  EXPECT_EQ(tree.focus_chain().size(), 3u);

  // v_3 -> v_2: deny
  EXPECT_FALSE(tree.RequestFocusChange(view_koid_3, view_koid_2));
  EXPECT_EQ(tree.focus_chain().size(), 3u);

  // v_1 -> v_1: allow
  EXPECT_TRUE(tree.RequestFocusChange(view_koid_1, view_koid_1));
  EXPECT_EQ(tree.focus_chain().size(), 2u);

  // scene -> scene: allow
  EXPECT_TRUE(tree.RequestFocusChange(scene_koid, scene_koid));
  EXPECT_EQ(tree.focus_chain().size(), 1u);

  // scene -> v_2: allow
  EXPECT_TRUE(tree.RequestFocusChange(scene_koid, view_koid_2));
  EXPECT_EQ(tree.focus_chain().size(), 2u);

  // v_2 -> scene: deny
  EXPECT_FALSE(tree.RequestFocusChange(view_koid_2, scene_koid));
  EXPECT_EQ(tree.focus_chain().size(), 2u);

  // v_2 -> v_1: deny
  EXPECT_FALSE(tree.RequestFocusChange(view_koid_2, view_koid_1));
  EXPECT_EQ(tree.focus_chain().size(), 2u);

  // v_2 -> v_3: deny
  EXPECT_FALSE(tree.RequestFocusChange(view_koid_2, view_koid_3));
  EXPECT_EQ(tree.focus_chain().size(), 2u);

  // scene -> v_4: deny
  EXPECT_FALSE(tree.RequestFocusChange(scene_koid, view_koid_4));
  EXPECT_EQ(tree.focus_chain().size(), 2u);
}

}  // namespace lib_ui_gfx_engine_tests
