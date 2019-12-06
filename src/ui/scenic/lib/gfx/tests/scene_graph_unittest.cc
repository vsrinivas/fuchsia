// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/zx/eventpair.h>

#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using SceneGraphTest = SessionTest;

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
                                         .global_transform = NoGlobalTransform()});
    updates.push_back(ViewTreeNewAttachNode{.koid = 1111u});
    updates.push_back(ViewTreeNewRefNode{.view_ref = std::move(child_view_pair.view_ref),
                                         .may_receive_focus = [] { return true; },
                                         .global_transform = NoGlobalTransform()});
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
                                         .global_transform = NoGlobalTransform()});
    updates.push_back(ViewTreeNewAttachNode{.koid = 1111u});
    updates.push_back(ViewTreeNewRefNode{.view_ref = std::move(child_view_pair.view_ref),
                                         .may_receive_focus = [] { return false; },  // Different!
                                         .global_transform = NoGlobalTransform()});
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

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
