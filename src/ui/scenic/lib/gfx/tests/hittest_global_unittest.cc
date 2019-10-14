// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fostr/fidl/fuchsia/ui/scenic/formatting.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "lib/ui/scenic/cpp/view_token_pair.h"
#include "sdk/lib/ui/scenic/cpp/resources.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/gfx/engine/hit.h"
#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/tests/mocks.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"
#include "src/ui/scenic/lib/scenic/util/print_event.h"

// The test setup here is sufficiently different from hittest_unittest.cc to
// merit its own file.  We access the global hit test through the compositor,
// instead of through a session.

namespace scenic_impl {
namespace gfx {
namespace test {
namespace {

// Creates a unit ray entering at (x, y) from z = 1 pointed at -z.
//
// This should be kept pretty consistent with input_system.cc CreateScreenPerpendicularRay. One
// notable divergence is that we don't jitter the x,y here, for more straightforward expectations.
escher::ray4 HitRay(float x, float y) {
  return {
      .origin = {x, y, 1, 1},
      .direction = {0, 0, -1, 0},
  };
}

// Session wrapper that references a common Engine.
//
// This class calls Session::TearDown directly and so avoids pulling in
// SessionHandler and SessionManager; these make a call to TearDown that's
// triggered by Engine::RenderFrame, and we don't need that here.
class CustomSession {
 public:
  CustomSession(SessionId id, SessionContext session_context)
      : session_(std::make_unique<Session>(id, std::move(session_context), EventReporter::Default(),
                                           ErrorReporter::Default())) {}

  void Apply(::fuchsia::ui::gfx::Command command) {
    CommandContext empty_command_context;
    bool result = session_->ApplyCommand(&empty_command_context, std::move(command));
    ASSERT_TRUE(result) << "Failed to apply: " << command;  // Fail fast.
  }

 private:
  std::unique_ptr<Session> session_;
};

// Loop fixture provides dispatcher for Engine's EventTimestamper.
class HitTestTest : public gtest::TestLoopFixture {
 public:
  static constexpr uint32_t kSceneId = 20004;  // Hit

  HitTestTest()
      : engine_(context_provider_.context(),
                /* frame_scheduler */ nullptr,
                /* release_fence_signaller */ nullptr,
                /* escher */ nullptr) {}
  Engine* engine() { return &engine_; }
  float layer_width() const { return layer_width_; }
  float layer_height() const { return layer_height_; }

  CustomSession CreateSession(SessionId id) { return CustomSession(id, engine_.session_context()); }

  // Creates a session ID 0 with a compositor, layer stack, layer, scene, camera, and renderer.
  CustomSession CreateRootSession(float layer_width, float layer_height) {
    layer_width_ = layer_width;
    layer_height_ = layer_height;
    CustomSession session = CreateSession(0);

    static constexpr uint32_t kCompositorId = 20001, kLayerStackId = 20002, kLayerId = 20003;
    session.Apply(scenic::NewCreateCompositorCmd(kCompositorId));
    session.Apply(scenic::NewCreateLayerStackCmd(kLayerStackId));
    session.Apply(scenic::NewSetLayerStackCmd(kCompositorId, kLayerStackId));
    session.Apply(scenic::NewCreateLayerCmd(kLayerId));
    session.Apply(scenic::NewSetSizeCmd(kLayerId, (float[]){layer_width, layer_height}));
    session.Apply(scenic::NewAddLayerCmd(kLayerStackId, kLayerId));

    static constexpr uint32_t kCameraId = 20005, kRendererId = 20006;
    session.Apply(scenic::NewCreateSceneCmd(kSceneId));
    session.Apply(scenic::NewCreateCameraCmd(kCameraId, kSceneId));
    session.Apply(scenic::NewCreateRendererCmd(kRendererId));
    session.Apply(scenic::NewSetCameraCmd(kRendererId, kCameraId));
    session.Apply(scenic::NewSetRendererCmd(kLayerId, kRendererId));

    return session;
  }

  LayerStackPtr layer_stack() {
    // Models input subsystem's access to Engine internals.
    // For simplicity, we use the first (and only) compositor and layer stack.
    const CompositorWeakPtr& compositor = engine_.scene_graph()->first_compositor();
    FXL_CHECK(compositor);
    LayerStackPtr layer_stack = compositor->layer_stack();
    FXL_CHECK(layer_stack);

    return layer_stack;
  }

 private:
  sys::testing::ComponentContextProvider context_provider_;
  Engine engine_;

  float layer_width_, layer_height_;
};

using SingleSessionHitTestTest = HitTestTest;
using MultiSessionHitTestTest = HitTestTest;

// This unit test checks to make sure that geometry that is a child of
// a view is not hit by a hit-test ray if the intersection point
// with the ray lies outside of the view's bounding box.
//
// The setup is that there is a view which covers the left half of the
// display with a rectangle that goes across the entire width of the
// display from left to right, and thus extends beyond the bounds of
// its view. Two hit tests are performed on the rectangle, one inside
// the view bounds and one without. The total number of hits is then
// checked to make sure they are what we would expect.
//
// This is an ASCII representation of what the test looks like:
//
// VVVVVVVV
// rrrrrrrrrrrrrrr
// rrrrrrrrrrrrrrr
// VVVVVVVV
//
// Where "V" represents the view boundary and "r" is the extent
// of the rectangle.
TEST_F(SingleSessionHitTestTest, ViewClippingHitTest) {
  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  CustomSession sess = CreateRootSession(1024, 768);
  {
    const uint32_t kViewId = 15;
    const uint32_t kViewHolderId = 30;  // Hit
    const uint32_t kShapeNodeId = 50;   // Hit
    const uint32_t kMaterialId = 60;
    const uint32_t kRectId = 70;         // Hit
    const uint32_t kRootNodeId = 20007;  // Hit

    const int32_t pane_width = layer_width();
    const int32_t pane_height = 0.25 * layer_height();

    sess.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));

    sess.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token),
                                              "MyViewHolder"));

    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "MyView"));

    // Set the bounding box on the view holder.
    const float bbox_min[3] = {0.f, 0.f, -2.f};
    const float bbox_max[3] = {layer_width() / 2, layer_height(), 1.f};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
    sess.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolderId, bbox_min, bbox_max, inset_min, inset_max));

    // Create rectangle and material
    sess.Apply(scenic::NewCreateMaterialCmd(kMaterialId));
    sess.Apply(scenic::NewSetColorCmd(kMaterialId, 0, 255, 255, 255));
    sess.Apply(scenic::NewCreateRectangleCmd(kRectId, pane_width, pane_height));

    // Create shape node and apply rectangle
    sess.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId));
    sess.Apply(scenic::NewSetShapeCmd(kShapeNodeId, kRectId));
    sess.Apply(scenic::NewSetMaterialCmd(kShapeNodeId, kMaterialId));
    sess.Apply(scenic::NewSetTranslationCmd(
        kShapeNodeId, (float[3]){0.5f * pane_width, 0.5f * layer_height(), 0.f}));

    sess.Apply(scenic::NewAddChildCmd(kSceneId, kRootNodeId));
    sess.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShapeNodeId));

    // Perform two hit tests on either side of the display.
    std::vector<Hit> hits, hits2;
    {
      // First hit test should intersect the view's bounding box.
      GlobalHitTester hit_tester;
      hits = layer_stack()->HitTest(HitRay(5, layer_height() / 2), &hit_tester);
      EXPECT_EQ(hits.size(), 5u) << "Should see a single hit on the rectangle";

      // Second hit test should completely miss the view's bounding box.
      GlobalHitTester hit_tester2;
      hits2 =
          layer_stack()->HitTest(HitRay(layer_width() / 2 + 50, layer_height() / 2), &hit_tester2);
      EXPECT_EQ(hits2.size(), 0u) << "Should see no hits since its outside the view bounds";
    }
  }
}

// Test to ensure the collision debug messages are correct.
TEST(HitTestTest, CollisionsWarningTest) {
  // Empty list should have no collisions.
  EXPECT_EQ(GetDistanceCollisionsWarning({}), "");

  // Set up fake nodes.
  std::vector<fxl::RefPtr<ShapeNode>> nodes;
  const size_t num_nodes = 6;
  for (size_t i = 0; i < num_nodes; ++i) {
    nodes.push_back(fxl::MakeRefCounted<ShapeNode>(/*session=*/nullptr,
                                                   /*session_id=*/num_nodes - i, /*node_id=*/i));
  }

  // Nodes at same distance should collide.
  const float distance1 = 100.0f;
  const float distance2 = 200.0f;
  const float distance3 = 300.0f;
  std::vector<Hit> collisions_list{
      {.node = nodes[0].get(), .distance = distance1},
      {.node = nodes[1].get(), .distance = distance1},
      {.node = nodes[2].get(), .distance = distance1},
      {.node = nodes[3].get(), .distance = distance2},
      {.node = nodes[4].get(), .distance = distance3},
      {.node = nodes[5].get(), .distance = distance3},
  };

  EXPECT_EQ(GetDistanceCollisionsWarning(collisions_list),
            "Input-hittable nodes with ids [ 6-0 5-1 4-2 ] [ 2-4 1-5 ] are at equal distance and "
            "overlapping. See https://fuchsia.dev/fuchsia-src/the-book/ui/view_bounds#collisions");

  // Nodes at different distances should have no collisions.
  std::vector<Hit> no_collisions_list;
  for (size_t i = 0; i < num_nodes; ++i) {
    no_collisions_list.push_back(Hit{.node = nodes[i].get(), .distance = distance1 + i * 1.0f});
  }

  EXPECT_EQ(GetDistanceCollisionsWarning(no_collisions_list), "");
}

// A unit test to see what happens when a child view is bigger than its parent view,
// but still overlaps with the parent view. The hit ray should still hit the child
// view in this case.
//
// Diagram, where |p| shows the parent bounds, |c| shows the child bounds, and |r| is
// a rectangle that is a child of the child view.
//
// ccccccccccccccccccccccccccc
// c                         c
// c         pppppppp        c
// c         p      p        c
// c         p   r  p        c
// c         p      p        c
// c         pppppppp        c
// c                         c
// ccccccccccccccccccccccccccc
TEST_F(MultiSessionHitTestTest, ChildBiggerThanParentTest) {
  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  const uint32_t kRootNodeId = 20007;  // Hit
  const uint32_t kMiddleNodeId = 37;   // Hit
  const uint32_t kViewHolderId = 35;   // Hit
  const uint32_t kViewHolderId2 = 36;  // Hit

  // Root session sets up the scene and two view holders.
  CustomSession sess = CreateRootSession(1024, 768);
  {
    // Create root node and middle node.
    sess.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));

    sess.Apply(
        scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token), "ViewHolder"));

    // Add the first view holder as a child of the root node, and the second
    // view holder as a child of the first view holder.
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kRootNodeId));
    sess.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolderId));

    // Set view_holder 1's bounding box. It is a small box centered in the display.
    const float width = 100, height = 100;
    const float bbox_min[3] = {(layer_width() - width) / 2, (layer_height() - height) / 2, -6};
    const float bbox_max[3] = {(layer_width() + width) / 2, (layer_height() + height) / 2, -4};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
    sess.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolderId, bbox_min, bbox_max, inset_min, inset_max));
  }

  // Sets up the parent view.
  CustomSession sess1 = CreateSession(1);
  {
    const uint32_t kViewId = 15;
    sess1.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "MyView"));

    sess1.Apply(scenic::NewCreateEntityNodeCmd(kMiddleNodeId));

    sess1.Apply(scenic::NewAddChildCmd(kViewId, kMiddleNodeId));

    sess1.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId2, std::move(view_holder_token2),
                                               "ViewHolder2"));

    sess1.Apply(scenic::NewAddChildCmd(kMiddleNodeId, kViewHolderId2));

    // Set view holder 2's bounding box. It takes up the entire display and thus is bigger
    // than it's parent's box.
    const float bbox_min2[3] = {0, 0, -9};
    const float bbox_max2[3] = {layer_width(), layer_height(), 0};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
    sess1.Apply(scenic::NewSetViewPropertiesCmd(kViewHolderId2, bbox_min2, bbox_max2, inset_min,
                                                inset_max));
  }

  // Set up the child view.
  CustomSession sess2 = CreateSession(2);
  {
    const uint32_t kViewId2 = 16;      // Hit
    const uint32_t kShapeNodeId = 50;  // Hit
    const uint32_t kMaterialId = 60;
    const uint32_t kRectId = 70;  // Hit

    const int32_t pane_width = 25;
    const int32_t pane_height = 25;

    sess2.Apply(scenic::NewCreateViewCmd(kViewId2, std::move(view_token2), "MyView2"));

    // Create rectangle and material
    sess2.Apply(scenic::NewCreateMaterialCmd(kMaterialId));
    sess2.Apply(scenic::NewSetColorCmd(kMaterialId, 0, 255, 255, 255));
    sess2.Apply(scenic::NewCreateRectangleCmd(kRectId, pane_width, pane_height));

    // Create shape node and apply rectangle
    sess2.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId));
    sess2.Apply(scenic::NewSetShapeCmd(kShapeNodeId, kRectId));
    sess2.Apply(scenic::NewSetMaterialCmd(kShapeNodeId, kMaterialId));
    sess2.Apply(scenic::NewSetTranslationCmd(
        kShapeNodeId, (float[3]){layer_width() / 2, layer_height() / 2, -5.f}));

    sess2.Apply(scenic::NewAddChildCmd(kViewId2, kShapeNodeId));
  }

  // Perform two hit tests on either side of the display.
  std::vector<Hit> hits;
  {
    // First hit test should intersect the view's bounding box.
    GlobalHitTester hit_tester;
    hits = layer_stack()->HitTest(HitRay(layer_width() / 2, layer_height() / 2), &hit_tester);
    EXPECT_EQ(hits.size(), 8u) << "Should hit the parent, child, and the shape";
  }
}

// A unit test where the ray passes through a child view, but the child view
// is completely clipped by its parent view. In this case there should be no
// hit registered.
//
// Diagram:
//
// pppppppppppppppcccccccccccccccc
// p             pc              c
// p             pc              c
// p             pc              c
// p             pc              c
// pppppppppppppppcccccccccccccccc
TEST_F(MultiSessionHitTestTest, ChildCompletelyClipped) {
  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  const uint32_t kRootNodeId = 20007;
  const uint32_t kMiddleNodeId = 37;
  const uint32_t kViewHolderId = 35;
  const uint32_t kViewHolderId2 = 36;

  // Root session sets up the scene and two view holders.
  CustomSession sess = CreateRootSession(1024, 768);
  {
    // Create root node and middle node.
    sess.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));

    sess.Apply(
        scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token), "ViewHolder"));

    // Add the first view holder as a child of the root node, and the second
    // view holder as a child of the first view holder.
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kRootNodeId));
    sess.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolderId));

    // Set view_holder 1's bounding box. It takes up the left-hand side of the display.
    const float bbox_min[3] = {0, 0, -9};
    const float bbox_max[3] = {layer_width() / 2, layer_height() / 2, 0};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
    sess.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolderId, bbox_min, bbox_max, inset_min, inset_max));
  }

  // Sets up the parent view.
  CustomSession sess1 = CreateSession(1);
  {
    const uint32_t kViewId = 15;
    sess1.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "MyView"));

    sess1.Apply(scenic::NewCreateEntityNodeCmd(kMiddleNodeId));

    sess1.Apply(scenic::NewAddChildCmd(kViewId, kMiddleNodeId));

    sess1.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId2, std::move(view_holder_token2),
                                               "ViewHolder2"));

    sess1.Apply(scenic::NewAddChildCmd(kMiddleNodeId, kViewHolderId2));

    // Set view holder 2's bounding box. It takes up the right-hand side of the display.
    const float bbox_min2[3] = {layer_width() / 2, layer_height() / 2, -9};
    const float bbox_max2[3] = {layer_width(), layer_height(), 0};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
    sess1.Apply(scenic::NewSetViewPropertiesCmd(kViewHolderId2, bbox_min2, bbox_max2, inset_min,
                                                inset_max));
  }

  // Set up the child view.
  CustomSession sess2 = CreateSession(2);
  {
    const uint32_t kViewId2 = 16;
    const uint32_t kShapeNodeId = 50;
    const uint32_t kMaterialId = 60;
    const uint32_t kRectId = 70;

    const int32_t pane_width = 25;
    const int32_t pane_height = 25;

    sess2.Apply(scenic::NewCreateViewCmd(kViewId2, std::move(view_token2), "MyView2"));

    // Create rectangle and material
    sess2.Apply(scenic::NewCreateMaterialCmd(kMaterialId));
    sess2.Apply(scenic::NewSetColorCmd(kMaterialId, 0, 255, 255, 255));
    sess2.Apply(scenic::NewCreateRectangleCmd(kRectId, pane_width, pane_height));

    // Create shape node and apply rectangle
    sess2.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId));
    sess2.Apply(scenic::NewSetShapeCmd(kShapeNodeId, kRectId));
    sess2.Apply(scenic::NewSetMaterialCmd(kShapeNodeId, kMaterialId));
    sess2.Apply(scenic::NewSetTranslationCmd(
        kShapeNodeId, (float[3]){3.f * layer_width() / 4.f, 3.f * layer_height() / 4.f, -5.f}));

    sess2.Apply(scenic::NewAddChildCmd(kViewId2, kShapeNodeId));
  }

  // Perform two hit tests on either side of the display.
  std::vector<Hit> hits;
  {
    // First hit test should intersect the view's bounding box.
    GlobalHitTester hit_tester;
    hits =
        layer_stack()->HitTest(HitRay(3 * layer_width() / 4, 3 * layer_height() / 4), &hit_tester);
    EXPECT_EQ(hits.size(), 0u) << "Should not hit anything at all";
  }
}

// A comprehensive test that sets up three independent sessions, with
// View/ViewHolder pairs, and checks if global hit testing has access to
// hittable nodes across all sessions.
TEST_F(MultiSessionHitTestTest, GlobalHits) {
  // Create our tokens for View/ViewHolder creation.
  auto [view_token_1, view_holder_token_1] = scenic::ViewTokenPair::New();
  auto [view_token_2, view_holder_token_2] = scenic::ViewTokenPair::New();

  // Create bounds for the views.
  const float bbox_min[3] = {0, 0, -4};
  const float bbox_max[3] = {10, 10, 0};
  const float inset_min[3] = {0, 0, 0};
  const float inset_max[3] = {0, 0, 0};

  // Root session sets up the scene and two view holders.
  CustomSession s_r = CreateRootSession(9, 9);
  {
    // TODO(SCN-885) - Adjust hit count; an EntityNode shouldn't be hit.
    const uint32_t kRootNodeId = 1007;  // Hit
    s_r.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));

    const uint32_t kViewHolder1Id = 1008;  // Hit
    s_r.Apply(scenic::NewAddChildCmd(kSceneId, kRootNodeId));
    s_r.Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder_token_1),
                                             "viewholder_1"));
    s_r.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolder1Id));

    const uint32_t kViewHolder2Id = 1009;  // Hit
    s_r.Apply(scenic::NewCreateViewHolderCmd(kViewHolder2Id, std::move(view_holder_token_2),
                                             "viewholder_2"));
    s_r.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolder2Id));

    s_r.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolder1Id, bbox_min, bbox_max, inset_min, inset_max));

    s_r.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolder2Id, bbox_min, bbox_max, inset_min, inset_max));
  }

  // Two sessions (s_1 and s_2) create an overlapping and hittable surface.
  CustomSession s_1(1, engine()->session_context());
  {
    const uint32_t kViewId = 2001;  // Hit
    s_1.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token_1), "view_1"));

    const uint32_t kRootNodeId = 2002;  // Hit
    s_1.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_1.Apply(scenic::NewAddChildCmd(kViewId, kRootNodeId));

    const uint32_t kChildId = 2003;  // Hit
    s_1.Apply(scenic::NewCreateShapeNodeCmd(kChildId));
    s_1.Apply(scenic::NewAddChildCmd(kRootNodeId, kChildId));
    s_1.Apply(scenic::NewSetTranslationCmd(kChildId, (float[3]){4.f, 4.f, /*z*/ -2.f}));

    const uint32_t kShapeId = 2004;
    s_1.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_1.Apply(scenic::NewSetShapeCmd(kChildId, kShapeId));
  }

  CustomSession s_2(2, engine()->session_context());
  {
    const uint32_t kViewId = 3001;  // Hit
    s_2.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token_2), "view_2"));

    const uint32_t kRootNodeId = 3002;  // Hit
    s_2.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_2.Apply(scenic::NewAddChildCmd(kViewId, kRootNodeId));

    const uint32_t kChildId = 3003;  // Hit
    s_2.Apply(scenic::NewCreateShapeNodeCmd(kChildId));
    s_2.Apply(scenic::NewAddChildCmd(kRootNodeId, kChildId));
    s_2.Apply(scenic::NewSetTranslationCmd(kChildId, (float[3]){4.f, 4.f, /*z*/ -3.f}));

    const uint32_t kShapeId = 3004;
    s_2.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_2.Apply(scenic::NewSetShapeCmd(kChildId, kShapeId));
  }

  std::vector<Hit> hits;
  {
    GlobalHitTester hit_tester;
    hits = layer_stack()->HitTest(HitRay(4, 4), &hit_tester);
  }

  // All that for this!
  EXPECT_EQ(hits.size(), 10u) << "Should see ten hits across three sessions.";
}

}  // namespace
}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
