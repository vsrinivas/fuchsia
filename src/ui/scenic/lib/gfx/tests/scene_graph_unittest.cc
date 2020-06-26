// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/zx/eventpair.h>

#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using SceneGraphTest = SessionTest;

ViewTreeNewRefNode ViewTreeNewRefNodeTemplate() {
  return {
      .may_receive_focus = [] { return true; },
      .is_input_suppressed = [] { return false; },
      .global_transform = [] { return std::nullopt; },
      .hit_test = [](auto...) {},
      .add_annotation_view_holder = [](auto) {},
      .session_id = 1u,
  };
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
  const zx_koid_t parent_koid = utils::ExtractKoid(parent_view_pair.view_ref);
  auto child_view_pair = scenic::ViewRefPair::New();
  const zx_koid_t child_koid = utils::ExtractKoid(child_view_pair.view_ref);
  {
    ViewTreeUpdates updates;
    {
      ViewTreeNewRefNode ref_node = ViewTreeNewRefNodeTemplate();
      ref_node.view_ref = std::move(parent_view_pair.view_ref);
      ref_node.session_id = 1u;
      updates.push_back(std::move(ref_node));
    }
    updates.push_back(ViewTreeNewAttachNode{.koid = 1111u});
    {
      ViewTreeNewRefNode ref_node = ViewTreeNewRefNodeTemplate();
      ref_node.view_ref = std::move(child_view_pair.view_ref);
      ref_node.session_id = 2u;
      updates.push_back(std::move(ref_node));
    }
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
  const zx_koid_t parent_koid = utils::ExtractKoid(parent_view_pair.view_ref);
  auto child_view_pair = scenic::ViewRefPair::New();
  const zx_koid_t child_koid = utils::ExtractKoid(child_view_pair.view_ref);
  {
    ViewTreeUpdates updates;
    {
      ViewTreeNewRefNode ref_node = ViewTreeNewRefNodeTemplate();
      ref_node.view_ref = std::move(parent_view_pair.view_ref);
      ref_node.session_id = 1u;
      updates.push_back(std::move(ref_node));
    }
    updates.push_back(ViewTreeNewAttachNode{.koid = 1111u});
    {
      ViewTreeNewRefNode ref_node = ViewTreeNewRefNodeTemplate();
      ref_node.may_receive_focus = [] { return false; };  // Different!
      ref_node.view_ref = std::move(child_view_pair.view_ref);
      ref_node.session_id = 2u;
      updates.push_back(std::move(ref_node));
    }
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

// This test confirms that the ViewRefInstalled protocol is correctly integrated with the
// SceneGraph/ViewTree.
TEST_F(SceneGraphTest, ViewRefInstalledIntegrationTest) {
  // Construct ViewTree with 2 ViewRefs in a parent-child relationship.
  sys::testing::ComponentContextProvider context_provider;
  SceneGraph scene_graph(context_provider.context());

  // Connect to the ViewRefInstalled API.
  fuchsia::ui::views::ViewRefInstalledPtr view_ref_installed;
  context_provider.ConnectToPublicService(view_ref_installed.NewRequest());

  // Set up two ViewRefs. One for the parent and one for the child.
  auto parent_view_pair = scenic::ViewRefPair::New();
  const zx_koid_t parent_koid = utils::ExtractKoid(parent_view_pair.view_ref);
  auto child_view_pair = scenic::ViewRefPair::New();
  const zx_koid_t child_koid = utils::ExtractKoid(child_view_pair.view_ref);

  bool watch1 = false;
  bool watch2 = false;
  {  // Watch for both ViewRefs, making sure they don't activate until the SceneGraph has been
     // updated.
    fuchsia::ui::views::ViewRef clone1;
    fidl::Clone(parent_view_pair.view_ref, &clone1);
    view_ref_installed->Watch(std::move(clone1), [&watch1](auto) { watch1 = true; });

    fuchsia::ui::views::ViewRef clone2;
    fidl::Clone(child_view_pair.view_ref, &clone2);
    view_ref_installed->Watch(std::move(clone2), [&watch2](auto) { watch2 = true; });
  }

  RunLoopUntilIdle();
  EXPECT_FALSE(watch1);
  EXPECT_FALSE(watch2);

  {
    ViewTreeUpdates updates;
    {
      ViewTreeNewRefNode ref_node = ViewTreeNewRefNodeTemplate();
      fuchsia::ui::views::ViewRef clone;
      fidl::Clone(parent_view_pair.view_ref, &clone);
      ref_node.view_ref = std::move(clone);
      ref_node.session_id = 1u;
      updates.push_back(std::move(ref_node));
    }
    updates.push_back(ViewTreeNewAttachNode{.koid = 1111u});
    {
      ViewTreeNewRefNode ref_node = ViewTreeNewRefNodeTemplate();
      fuchsia::ui::views::ViewRef clone;
      fidl::Clone(child_view_pair.view_ref, &clone);
      ref_node.view_ref = std::move(clone);
      ref_node.session_id = 2u;
      updates.push_back(std::move(ref_node));
    }
    updates.push_back(ViewTreeMakeGlobalRoot{.koid = parent_koid});
    updates.push_back(ViewTreeConnectToParent{.child = child_koid, .parent = 1111u});
    updates.push_back(ViewTreeConnectToParent{.child = 1111u, .parent = parent_koid});

    scene_graph.StageViewTreeUpdates(std::move(updates));
    scene_graph.ProcessViewTreeUpdates();
  }

  // ViewRefs are now installed and both Watch calls should return.
  RunLoopUntilIdle();
  EXPECT_TRUE(watch1);
  EXPECT_TRUE(watch2);

  {  // If we now Watch for the ViewRefs, the calls should return immediately.
    bool watch3 = false;
    fuchsia::ui::views::ViewRef clone1;
    fidl::Clone(parent_view_pair.view_ref, &clone1);
    view_ref_installed->Watch(std::move(clone1), [&watch3](auto) { watch3 = true; });

    bool watch4 = false;
    fuchsia::ui::views::ViewRef clone2;
    fidl::Clone(child_view_pair.view_ref, &clone2);
    view_ref_installed->Watch(std::move(clone2), [&watch4](auto) { watch4 = true; });

    RunLoopUntilIdle();
    EXPECT_TRUE(watch3);
    EXPECT_TRUE(watch4);
  }
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
