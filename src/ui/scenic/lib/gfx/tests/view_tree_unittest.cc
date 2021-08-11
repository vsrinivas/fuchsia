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

namespace {

scenic_impl::gfx::ViewTreeNewRefNode ViewTreeNewRefNodeTemplate(ViewRef view_ref,
                                                                scheduling::SessionId session_id) {
  EventReporterWeakPtr no_reporter{};
  return {.view_ref = std::move(view_ref),
          .event_reporter = no_reporter,
          .may_receive_focus = [] { return true; },
          .is_input_suppressed = [] { return false; },
          .global_transform = [] { return glm::mat4(1.f); },
          .bounding_box = [] { return escher::BoundingBox(); },
          .hit_test = [](auto...) {},
          .add_annotation_view_holder = [](auto) {},
          .session_id = session_id};
}

std::array<float, 16> Mat4ToArray(const glm::mat4& matrix) {
  std::array<float, 16> array;
  for (size_t i = 0; i < 4; ++i) {
    for (size_t j = 0; j < 4; ++j) {
      array[i * 4 + j] = matrix[i][j];
    }
  }

  return array;
}

}  // namespace

TEST(ViewTreeLifecycle, EmptyScene) {
  ViewTree tree{};
  EXPECT_TRUE(tree.IsStateValid());
}

TEST(ViewTreeLifecycle, SceneCreateThenDestroy) {
  ViewTree tree{};

  // Create a scene node.
  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair.view_ref), kOne));
  tree.MakeGlobalRoot(koid);

  // Destroy the scene node.
  tree.DeleteNode(koid);

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

  // Destroy view 2.
  tree.DeleteNode(view_2_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Destroy view 1.
  tree.DeleteNode(view_1_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Destroy scene.
  tree.DeleteNode(scene_koid);
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

  // Disconnect view 2.
  tree.DisconnectFromParent(view_2_koid);
  EXPECT_TRUE(tree.IsStateValid());

  // Disconnect view 1.
  tree.DisconnectFromParent(view_1_koid);
  EXPECT_TRUE(tree.IsStateValid());
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

  scenic::ViewRefPair pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid);

  scenic::ViewRefPair pair_2 = scenic::ViewRefPair::New();
  zx_koid_t scene_koid_2 = ExtractKoid(pair_2.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(pair_2.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid_2);

  tree.MakeGlobalRoot(ZX_KOID_INVALID);
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

    is_called = false;
    tree.DeleteNode(view_koid);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, tree.AddAnnotationViewHolder(view_koid, ViewHolderPtr()));
    EXPECT_FALSE(is_called);
  }

  {
    scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
    scenic::ViewRefPair view_pair_invalid = scenic::ViewRefPair::New();
    zx_koid_t view_koid = ExtractKoid(view_pair.view_ref);
    zx_koid_t view_koid_invalid = ExtractKoid(view_pair_invalid.view_ref);
    bool is_called = false;

    auto new_node = ViewTreeNewRefNodeTemplate(std::move(view_pair.view_ref), kOne);
    new_node.add_annotation_view_holder = [&is_called](auto) { is_called = true; };

    tree.NewRefNode(std::move(new_node));
    EXPECT_EQ(ZX_OK, tree.AddAnnotationViewHolder(view_koid, ViewHolderPtr()));
    EXPECT_TRUE(is_called);

    is_called = false;
    tree.InvalidateAnnotationViewHolder(view_koid_invalid);
    EXPECT_EQ(ZX_OK, tree.AddAnnotationViewHolder(view_koid, ViewHolderPtr()));
    EXPECT_TRUE(is_called);

    is_called = false;
    tree.InvalidateAnnotationViewHolder(view_koid);
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, tree.AddAnnotationViewHolder(view_koid, ViewHolderPtr()));
    EXPECT_FALSE(is_called);
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

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, tree.AddAnnotationViewHolder(ZX_KOID_INVALID, ViewHolderPtr()));
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

// Check that the snapshot is correct as the scene is created and manipulated.
TEST(ViewTreePrimitive, Snapshot) {
  ViewTree tree{};

  {  // Empty ViewTree.
    auto snapshot = tree.Snapshot();
    EXPECT_TRUE(snapshot.tree_boundaries.empty());
    auto& [root, view_tree, unconnected_views, hit_tester, tree_boundaries] = snapshot;
    EXPECT_EQ(root, ZX_KOID_INVALID);
    EXPECT_TRUE(view_tree.empty());
    EXPECT_TRUE(unconnected_views.empty());
  }

  // Just the scene node
  scenic::ViewRefPair scene_pair = scenic::ViewRefPair::New();
  zx_koid_t scene_koid = ExtractKoid(scene_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(scene_pair.view_ref), kOne));
  tree.MakeGlobalRoot(scene_koid);
  {
    auto snapshot = tree.Snapshot();
    EXPECT_TRUE(snapshot.tree_boundaries.empty());
    auto& [root, view_tree, unconnected_views, hit_tester, tree_boundaries] = snapshot;
    EXPECT_EQ(root, scene_koid);
    EXPECT_EQ(view_tree.size(), 1u);

    EXPECT_TRUE(view_tree.count(scene_koid) == 1);
    EXPECT_EQ(view_tree.at(scene_koid).parent, ZX_KOID_INVALID);
    EXPECT_TRUE(view_tree.at(scene_koid).children.empty());

    EXPECT_TRUE(unconnected_views.empty());
  }

  //     scene
  //    /
  //  a_1
  zx_koid_t attach_koid_1 = 1111u;
  tree.NewAttachNode(attach_koid_1);
  tree.ConnectToParent(attach_koid_1, scene_koid);
  {  // Attach node should cause no change.
    auto snapshot = tree.Snapshot();
    EXPECT_TRUE(snapshot.tree_boundaries.empty());
    auto& [root, view_tree, unconnected_views, hit_tester, tree_boundaries] = snapshot;
    EXPECT_EQ(root, scene_koid);
    EXPECT_EQ(view_tree.size(), 1u);

    EXPECT_TRUE(view_tree.count(scene_koid) == 1);
    EXPECT_EQ(view_tree.at(scene_koid).parent, ZX_KOID_INVALID);
    EXPECT_TRUE(view_tree.at(scene_koid).children.empty());

    EXPECT_TRUE(unconnected_views.empty());
  }

  //     scene
  //    /
  //  a_1
  //   |
  //  v_1
  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_koid_t view_koid_1 = ExtractKoid(view_pair.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair.view_ref), kTwo));
  tree.ConnectToParent(view_koid_1, attach_koid_1);
  {
    auto snapshot = tree.Snapshot();
    EXPECT_TRUE(snapshot.tree_boundaries.empty());
    auto& [root, view_tree, unconnected_views, hit_tester, tree_boundaries] = snapshot;
    EXPECT_EQ(root, scene_koid);
    EXPECT_EQ(view_tree.size(), 2u);

    EXPECT_TRUE(view_tree.count(scene_koid) == 1);
    EXPECT_EQ(view_tree.at(scene_koid).parent, ZX_KOID_INVALID);
    EXPECT_THAT(view_tree.at(scene_koid).children, testing::UnorderedElementsAre(view_koid_1));

    EXPECT_TRUE(view_tree.count(view_koid_1) == 1);
    EXPECT_EQ(view_tree.at(view_koid_1).parent, scene_koid);
    EXPECT_TRUE(view_tree.at(view_koid_1).children.empty());

    EXPECT_TRUE(unconnected_views.empty());
  }

  //     scene
  //    /    \
  //  a_1    a_2
  //   |      |
  //  v_1    v_2
  zx_koid_t attach_koid_2 = 2222u;
  tree.NewAttachNode(attach_koid_2);
  tree.ConnectToParent(attach_koid_2, scene_koid);
  scenic::ViewRefPair view_pair_2 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_2 = ExtractKoid(view_pair_2.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair_2.view_ref), kThree));
  tree.ConnectToParent(view_koid_2, attach_koid_2);
  {
    auto snapshot = tree.Snapshot();
    EXPECT_TRUE(snapshot.tree_boundaries.empty());
    auto& [root, view_tree, unconnected_views, hit_tester, tree_boundaries] = snapshot;
    EXPECT_EQ(root, scene_koid);
    EXPECT_EQ(view_tree.size(), 3u);

    EXPECT_TRUE(view_tree.count(scene_koid) == 1);
    EXPECT_EQ(view_tree.at(scene_koid).parent, ZX_KOID_INVALID);
    EXPECT_THAT(view_tree.at(scene_koid).children,
                testing::UnorderedElementsAre(view_koid_1, view_koid_2));

    EXPECT_TRUE(view_tree.count(view_koid_1) == 1);
    EXPECT_EQ(view_tree.at(view_koid_1).parent, scene_koid);
    EXPECT_TRUE(view_tree.at(view_koid_1).children.empty());

    EXPECT_TRUE(view_tree.count(view_koid_2) == 1);
    EXPECT_EQ(view_tree.at(view_koid_2).parent, scene_koid);
    EXPECT_TRUE(view_tree.at(view_koid_2).children.empty());

    EXPECT_TRUE(unconnected_views.empty());
  }

  //     scene
  //    /    \
  //  a_1    a_2
  //   |      |
  //  v_1    v_2
  //   |
  //  a_3
  //   |
  //  v_3
  zx_koid_t attach_koid_3 = 3333u;
  tree.NewAttachNode(attach_koid_3);
  tree.ConnectToParent(attach_koid_3, view_koid_1);
  scenic::ViewRefPair view_pair_3 = scenic::ViewRefPair::New();
  zx_koid_t view_koid_3 = ExtractKoid(view_pair_3.view_ref);
  tree.NewRefNode(ViewTreeNewRefNodeTemplate(std::move(view_pair_3.view_ref), kFour));
  tree.ConnectToParent(view_koid_3, attach_koid_3);
  {
    auto snapshot = tree.Snapshot();
    EXPECT_TRUE(snapshot.tree_boundaries.empty());
    auto& [root, view_tree, unconnected_views, hit_tester, tree_boundaries] = snapshot;
    EXPECT_EQ(root, scene_koid);
    EXPECT_EQ(view_tree.size(), 4u);

    EXPECT_TRUE(view_tree.count(scene_koid) == 1);
    EXPECT_EQ(view_tree.at(scene_koid).parent, ZX_KOID_INVALID);
    EXPECT_THAT(view_tree.at(scene_koid).children,
                testing::UnorderedElementsAre(view_koid_1, view_koid_2));

    EXPECT_TRUE(view_tree.count(view_koid_1) == 1);
    EXPECT_EQ(view_tree.at(view_koid_1).parent, scene_koid);
    EXPECT_THAT(view_tree.at(view_koid_1).children, testing::UnorderedElementsAre(view_koid_3));

    EXPECT_TRUE(view_tree.count(view_koid_2) == 1);
    EXPECT_EQ(view_tree.at(view_koid_2).parent, scene_koid);
    EXPECT_TRUE(view_tree.at(view_koid_2).children.empty());

    EXPECT_TRUE(view_tree.count(view_koid_3) == 1);
    EXPECT_EQ(view_tree.at(view_koid_3).parent, view_koid_1);
    EXPECT_TRUE(view_tree.at(view_koid_3).children.empty());

    EXPECT_TRUE(unconnected_views.empty());
  }

  // Disconnect a subtree
  //     scene
  //    X    \
  //  a_1    a_2
  //   |      |
  //  v_1    v_2
  //   |
  //  a_3
  //   |
  //  v_3
  tree.DisconnectFromParent(attach_koid_1);
  {
    auto snapshot = tree.Snapshot();
    EXPECT_TRUE(snapshot.tree_boundaries.empty());
    auto& [root, view_tree, unconnected_views, hit_tester, tree_boundaries] = snapshot;
    EXPECT_EQ(root, scene_koid);
    EXPECT_EQ(view_tree.size(), 2u);

    EXPECT_TRUE(view_tree.count(scene_koid) == 1);
    EXPECT_EQ(view_tree.at(scene_koid).parent, ZX_KOID_INVALID);
    EXPECT_THAT(view_tree.at(scene_koid).children, testing::UnorderedElementsAre(view_koid_2));

    EXPECT_TRUE(view_tree.count(view_koid_2) == 1);
    EXPECT_EQ(view_tree.at(view_koid_2).parent, scene_koid);
    EXPECT_TRUE(view_tree.at(view_koid_2).children.empty());

    EXPECT_THAT(unconnected_views, testing::UnorderedElementsAre(view_koid_1, view_koid_3));
  }
}

TEST(ViewTreePrimitive, Snapshot_NodesHaveAllFields) {
  ViewTree tree{};
  zx_koid_t node1_koid, node2_koid;
  {
    {
      auto [control_ref1, view_ref1] = scenic::ViewRefPair::New();
      node1_koid = ExtractKoid(view_ref1);
      scenic_impl::gfx::ViewTreeNewRefNode node1{
          .view_ref = std::move(view_ref1),
          .event_reporter = {},
          .may_receive_focus = [] { return true; },
          .is_input_suppressed = [] { return false; },
          .global_transform = [] { return glm::mat4(2.f); },
          .bounding_box =
              [] {
                return escher::BoundingBox({1, 2, 3}, {4, 5, 6});
              },
          .hit_test = [](auto...) {},
          .add_annotation_view_holder = [](auto) {},
          .session_id = 1};
      tree.NewRefNode(std::move(node1));
    }

    {
      auto [control_ref2, view_ref2] = scenic::ViewRefPair::New();
      node2_koid = ExtractKoid(view_ref2);
      scenic_impl::gfx::ViewTreeNewRefNode node2{
          .view_ref = std::move(view_ref2),
          .event_reporter = {},
          .may_receive_focus = [] { return false; },
          .is_input_suppressed = [] { return false; },
          .global_transform = [] { return glm::mat4(1.f); },
          .bounding_box =
              [] {
                return escher::BoundingBox({7, 8, 9}, {10, 11, 12});
              },
          .hit_test = [](auto...) {},
          .add_annotation_view_holder = [](auto) {},
          .session_id = 2};
      tree.NewRefNode(std::move(node2));
    }

    tree.MakeGlobalRoot(node1_koid);
    const zx_koid_t attach_koid_1 = 1111u;
    tree.NewAttachNode(attach_koid_1);
    tree.ConnectToParent(attach_koid_1, node1_koid);
    tree.ConnectToParent(node2_koid, attach_koid_1);
  }

  const auto& [root, view_tree, unconnected_views, hit_tester, tree_boundaries] = tree.Snapshot();
  EXPECT_EQ(root, node1_koid);
  EXPECT_EQ(view_tree.size(), 2u);
  ASSERT_TRUE(view_tree.count(node1_koid));
  ASSERT_TRUE(view_tree.count(node2_koid));
  EXPECT_TRUE(tree_boundaries.empty());
  EXPECT_TRUE(unconnected_views.empty());

  {
    const auto& node1 = view_tree.at(node1_koid);
    EXPECT_THAT(node1.children, testing::ElementsAre(node2_koid));
    EXPECT_TRUE(node1.is_focusable);
    EXPECT_THAT(node1.bounding_box.min, testing::ElementsAre(1, 2));
    EXPECT_THAT(node1.bounding_box.max, testing::ElementsAre(4, 5));
    // Transform should be inverted from |global_transform| above.
    EXPECT_THAT(Mat4ToArray(node1.local_from_world_transform),
                testing::ElementsAre(0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 0.5f));
    EXPECT_EQ(utils::ExtractKoid(*node1.view_ref), node1_koid);
  }

  {
    const auto& node2 = view_tree.at(node2_koid);
    EXPECT_EQ(node2.parent, node1_koid);
    EXPECT_FALSE(node2.is_focusable);
    EXPECT_THAT(node2.bounding_box.min, testing::ElementsAre(7, 8));
    EXPECT_THAT(node2.bounding_box.max, testing::ElementsAre(10, 11));
    // Transform should be inverted from |global_transform| above.
    EXPECT_THAT(Mat4ToArray(node2.local_from_world_transform),
                testing::ElementsAre(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1));
    EXPECT_EQ(utils::ExtractKoid(*node2.view_ref), node2_koid);
  }
}

}  // namespace lib_ui_gfx_engine_tests
