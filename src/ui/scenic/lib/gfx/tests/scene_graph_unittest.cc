// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/zx/eventpair.h>

#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace scenic_impl::gfx::test {

using SceneGraphTest = SessionTest;

ViewTreeNewRefNode ViewTreeNewRefNodeTemplate() {
  return {
      .may_receive_focus = [] { return true; },
      .is_input_suppressed = [] { return false; },
      .global_transform = [] { return glm::mat4(1.f); },
      .bounding_box = [] { return escher::BoundingBox(); },
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
  SceneGraph scene_graph;
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
  SceneGraph scene_graph;
  CompositorPtr c1 = Compositor::New(session(), session()->id(), 1, scene_graph.GetWeakPtr());
  auto c1_weak = scene_graph.GetCompositor(c1->global_id());
  ASSERT_EQ(c1.get(), c1_weak.get());
}

TEST_F(SceneGraphTest, FirstCompositorIsStable) {
  SceneGraph scene_graph;

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

}  // namespace scenic_impl::gfx::test
