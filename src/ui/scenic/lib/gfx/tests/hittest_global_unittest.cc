// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <lib/fostr/fidl/fuchsia/ui/scenic/formatting.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
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
  static constexpr uint32_t kSceneId = 20004;
  static constexpr uint32_t kViewNodeId = 0;

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

  const uint32_t kShapeNodeId = 50;
  CustomSession sess = CreateRootSession(1024, 768);
  {
    const uint32_t kViewId = 15;
    const uint32_t kViewHolderId = 30;
    const uint32_t kRectId = 70;
    const uint32_t kRootNodeId = 20007;

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

    // Create shape node and apply rectangle
    sess.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId));
    sess.Apply(scenic::NewCreateRectangleCmd(kRectId, pane_width, pane_height));
    sess.Apply(scenic::NewSetShapeCmd(kShapeNodeId, kRectId));
    sess.Apply(scenic::NewSetTranslationCmd(
        kShapeNodeId, (float[3]){0.5f * pane_width, 0.5f * layer_height(), 0.f}));

    sess.Apply(scenic::NewAddChildCmd(kSceneId, kRootNodeId));
    sess.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShapeNodeId));

    // Perform two hit tests on either side of the display.
    std::vector<Hit> hits, hits2;
    {
      // First hit test should intersect the view's bounding box.
      HitTester hit_tester;
      hits = layer_stack()->HitTest(HitRay(5, layer_height() / 2), &hit_tester);
      ASSERT_EQ(hits.size(), 1u) << "Should see a single hit on the rectangle";
      EXPECT_EQ(hits[0].node->id(), kShapeNodeId);

      // Second hit test should completely miss the view's bounding box.
      HitTester hit_tester2;
      hits2 =
          layer_stack()->HitTest(HitRay(layer_width() / 2 + 50, layer_height() / 2), &hit_tester2);
      EXPECT_EQ(hits2.size(), 0u) << "Should see no hits since its outside the view bounds";
    }
  }
}

// Test to check that no hits can be detected in the subtree of a hit-suppressed node.
// Sets up a scene with a hit suppressed shape node above a non-suppressed shape node and performs a
// hit test that goes through both of them. Only the non-suppressed node should register a hit.
//
// Diagram:                |  Scene graph:
//                         |
// vvvvvvvvvvvvvvvvvvvvvv  |     View
// v                    v  |     /  \
// v    (rrrrrrrrrrr)   v  |    |  EntityNode(suppressed)
// v                    v  |    |    |
// v     rrrrrrrrrrr    v  |    |  ShapeNode(no hit)
// v                    v  |    |
// vvvvvvvvvvvvvvvvvvvvvv  | ShapeNode (hit)
//
// Where v represents a view, r represents a hittable rectangle inside that view, and (r) represents
// a second rectangle inside a subtree topped with a hit-suppressed EntityNode.
TEST_F(SingleSessionHitTestTest, SuppressedHitTestForSubtree) {
  sys::testing::ComponentContextProvider context_provider;
  std::unique_ptr<Engine> engine =
      std::make_unique<Engine>(context_provider.context(), /*frame_scheduler*/ nullptr,
                               /*release fence signaller*/ nullptr, escher::EscherWeakPtr());

  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  // Create bounds for the views.
  const float bbox_min[3] = {0, 0, -4};
  const float bbox_max[3] = {10, 10, 0};
  const float inset_min[3] = {0, 0, 0};
  const float inset_max[3] = {0, 0, 0};

  const uint32_t kHittableShapeNodeId = 1007;
  CustomSession sess = CreateRootSession(1024, 768);
  {
    const uint32_t kViewHolderId = 1001;
    sess.Apply(
        scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token), "ViewHolder"));
    const uint32_t kViewId = 1002;
    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "view"));
    // Set the bounding box on the view holder.
    const float bbox_min[3] = {0.f, 0.f, -10.f};
    const float bbox_max[3] = {layer_width(), layer_height(), 0.f};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
    sess.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolderId, bbox_min, bbox_max, inset_min, inset_max));
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kViewHolderId));
    const uint32_t kRootNodeId = 1003;
    sess.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kRootNodeId));

    // Create first branch
    const uint32_t kUnhittableEntityNodeId = 1004;
    sess.Apply(scenic::NewCreateEntityNodeCmd(kUnhittableEntityNodeId));
    sess.Apply(scenic::NewSetHitTestBehaviorCmd(kUnhittableEntityNodeId,
                                                fuchsia::ui::gfx::HitTestBehavior::kSuppress));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kUnhittableEntityNodeId));
    const uint32_t kUnhittableShapeNodeId = 1005;
    sess.Apply(scenic::NewCreateShapeNodeCmd(kUnhittableShapeNodeId));
    sess.Apply(scenic::NewAddChildCmd(kUnhittableEntityNodeId, kUnhittableShapeNodeId));
    // Move to middle of view.
    sess.Apply(scenic::NewSetTranslationCmd(
        kUnhittableShapeNodeId, (float[3]){layer_width() / 2.f, layer_height() / 2.f, -5.f}));
    const uint32_t kShapeId1 = 1006;
    sess.Apply(scenic::NewCreateRectangleCmd(kShapeId1, layer_width(), layer_height()));
    sess.Apply(scenic::NewSetShapeCmd(kUnhittableShapeNodeId, kShapeId1));

    // Create second branch
    sess.Apply(scenic::NewCreateShapeNodeCmd(kHittableShapeNodeId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kHittableShapeNodeId));
    const uint32_t kShapeId2 = 1008;
    sess.Apply(scenic::NewCreateRectangleCmd(kShapeId2, layer_width(), layer_height()));
    sess.Apply(scenic::NewSetShapeCmd(kHittableShapeNodeId, kShapeId2));
    // Move to middle of view, below UnhittableShapeNode.
    sess.Apply(scenic::NewSetTranslationCmd(
        kHittableShapeNodeId, (float[3]){layer_width() / 2.f, layer_height() / 2.f, -2.5f}));
  }

  std::vector<Hit> hits;
  {
    HitTester hit_tester;
    hits = layer_stack()->HitTest(HitRay(layer_width() / 2, layer_height() / 2), &hit_tester);
  }

  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].node->id(), kHittableShapeNodeId);
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

// A unit test to see what happens when a child view is bigger than its parent view, but still
// overlaps with the parent view. The hit ray should still hit the ShapeNode of the child view
// overlapped by both views.
//
// Diagram, where |p| shows the parent bounds, |c| shows the child bounds, and |r| are
// rectangles that are children of the child view.
//
// ccccccccccccccccccccccccccc
// c             r           c
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

  CustomSession sess = CreateRootSession(1024, 768);
  {
    const uint32_t kRootNodeId = 20007;
    const uint32_t kViewHolderId = 35;
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
    const uint32_t kMiddleNodeId = 37;
    const uint32_t kViewHolderId2 = 36;
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
  const uint32_t kInnerShapeNodeId = 50;
  CustomSession sess2 = CreateSession(2);
  {
    const uint32_t kViewId2 = 16;
    const uint32_t kOuterShapeNodeId = 51;
    const uint32_t kRectId = 70;

    const int32_t pane_width = 25;
    const int32_t pane_height = 25;

    sess2.Apply(scenic::NewCreateViewCmd(kViewId2, std::move(view_token2), "MyView2"));

    // Create shape node, apply rectangle and translate it outside the parent view.
    sess2.Apply(scenic::NewCreateShapeNodeCmd(kOuterShapeNodeId));
    sess2.Apply(scenic::NewCreateRectangleCmd(kRectId, pane_width, pane_height));
    sess2.Apply(scenic::NewSetShapeCmd(kOuterShapeNodeId, kRectId));
    sess2.Apply(scenic::NewSetTranslationCmd(
        kOuterShapeNodeId, (float[3]){layer_width() / 2, layer_height() / 2, -8.f}));
    sess2.Apply(scenic::NewAddChildCmd(kViewId2, kOuterShapeNodeId));

    // Create shape node, apply rectangle and translate it inside the parent view.
    sess2.Apply(scenic::NewCreateShapeNodeCmd(kInnerShapeNodeId));
    sess2.Apply(scenic::NewSetShapeCmd(kInnerShapeNodeId, kRectId));
    sess2.Apply(scenic::NewSetTranslationCmd(
        kInnerShapeNodeId, (float[3]){layer_width() / 2, layer_height() / 2, -5.f}));
    sess2.Apply(scenic::NewAddChildCmd(kViewId2, kInnerShapeNodeId));
  }

  // Perform two hit tests on either side of the display.
  std::vector<Hit> hits;
  {
    // First hit test should intersect the view's bounding box.
    HitTester hit_tester;
    hits = layer_stack()->HitTest(HitRay(layer_width() / 2, layer_height() / 2), &hit_tester);
    ASSERT_EQ(hits.size(), 1u) << "Should only hit the shape encompassed by both views.";
    EXPECT_EQ(hits[0].node->id(), kInnerShapeNodeId);
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

  // Root session sets up the scene and two view holders.
  CustomSession sess = CreateRootSession(1024, 768);
  {
    const uint32_t kRootNodeId = 20007;
    const uint32_t kViewHolderId = 35;

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
    const uint32_t kMiddleNodeId = 37;
    const uint32_t kViewHolderId2 = 36;
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
    const uint32_t kRectId = 70;

    const int32_t pane_width = 25;
    const int32_t pane_height = 25;
    sess2.Apply(scenic::NewCreateViewCmd(kViewId2, std::move(view_token2), "MyView2"));

    // Create shape node and apply rectangle
    sess2.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId));
    sess2.Apply(scenic::NewCreateRectangleCmd(kRectId, pane_width, pane_height));
    sess2.Apply(scenic::NewSetShapeCmd(kShapeNodeId, kRectId));
    sess2.Apply(scenic::NewSetTranslationCmd(
        kShapeNodeId, (float[3]){3.f * layer_width() / 4.f, 3.f * layer_height() / 4.f, -5.f}));
    sess2.Apply(scenic::NewAddChildCmd(kViewId2, kShapeNodeId));
  }

  // Perform two hit tests on either side of the display.
  std::vector<Hit> hits;
  {
    // First hit test should intersect the view's bounding box.
    HitTester hit_tester;
    hits =
        layer_stack()->HitTest(HitRay(3 * layer_width() / 4, 3 * layer_height() / 4), &hit_tester);
    EXPECT_EQ(hits.size(), 0u) << "Should not hit anything at all";
  }
}

// This unit test checks that overlapping nodes have hit tests delivered in consistent order.
//
// All hittable nodes in this test are placed overlapping at the same (x,y,z) coordinates,
// and all of them should be receiving hits.
//
// Graph topology:
//    Scene
//      |
//    Root
//      |
//  ViewHolder
//      |
//    View1
//      |
//  EntityNode
//      |      \
//  ViewHolder  ShapeNode1
//      |
//    View2
//      |
//  ShapeNode2
//
// TODO(37785): Remove this entire test as soon as we can stop caring about ordering and hits on
// views.
TEST_F(MultiSessionHitTestTest, HitTestOrderingTest) {
  sys::testing::ComponentContextProvider context_provider;
  std::unique_ptr<Engine> engine =
      std::make_unique<Engine>(context_provider.context(), /*frame_scheduler*/ nullptr,
                               /*release fence signaller*/ nullptr, escher::EscherWeakPtr());

  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  // Root session sets up the scene and two view holders.
  CustomSession sess = CreateRootSession(1024, 768);

  // Set view holder bounding box to take up entire display.
  const float bbox_min[3] = {0, 0, -10.f};
  const float bbox_max[3] = {layer_width(), layer_height(), 0};
  const float inset_min[3] = {0, 0, 0};
  const float inset_max[3] = {0, 0, 0};

  {
    // Create root node and view holder node.
    const uint32_t kRootNodeId = 20007;
    sess.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    const uint32_t kViewHolderId1 = 35;
    sess.Apply(
        scenic::NewCreateViewHolderCmd(kViewHolderId1, std::move(view_holder_token), "ViewHolder"));

    // Add the first view holder as a child of the root node, and the second
    // view holder as a child of the first view holder.
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kRootNodeId));
    sess.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolderId1));
    sess.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolderId1, bbox_min, bbox_max, inset_min, inset_max));
  }

  // Sets up the parent view.
  const uint32_t kShapeNodeId1 = 40;
  CustomSession sess1 = CreateSession(1);
  {
    const uint32_t kViewId = 15;
    sess1.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "MyView"));
    const uint32_t kEntityNodeId = 37;
    sess1.Apply(scenic::NewCreateEntityNodeCmd(kEntityNodeId));
    sess1.Apply(scenic::NewAddChildCmd(kViewId, kEntityNodeId));

    const uint32_t kRectId = 39;
    sess1.Apply(scenic::NewCreateRectangleCmd(kRectId, layer_width(), layer_height()));

    // Create shape node and apply rectangle
    sess1.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId1));
    sess1.Apply(scenic::NewSetShapeCmd(kShapeNodeId1, kRectId));
    sess1.Apply(scenic::NewAddChildCmd(kEntityNodeId, kShapeNodeId1));

    const uint32_t kViewHolderId2 = 36;
    sess1.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId2, std::move(view_holder_token2),
                                               "ViewHolder2"));

    sess1.Apply(scenic::NewAddChildCmd(kEntityNodeId, kViewHolderId2));
    sess1.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolderId2, bbox_min, bbox_max, inset_min, inset_max));
  }

  // Setup the child view.
  const uint32_t kShapeNodeId2 = 80;
  CustomSession sess2 = CreateSession(2);
  {
    const uint32_t kViewId2 = 16;
    sess2.Apply(scenic::NewCreateViewCmd(kViewId2, std::move(view_token2), "MyView2"));
    sess2.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId2));
    sess2.Apply(scenic::NewAddChildCmd(kViewId2, kShapeNodeId2));
    const uint32_t kRectId = 81;
    sess2.Apply(scenic::NewCreateRectangleCmd(kRectId, layer_width(), layer_height()));
    sess2.Apply(scenic::NewSetShapeCmd(kShapeNodeId2, kRectId));
  }

  // Perform hit test in the middle of the display and check ordering.
  std::vector<Hit> hits;
  {
    HitTester hit_tester;
    hits = layer_stack()->HitTest(HitRay(layer_width() / 2, layer_height() / 2), &hit_tester);

    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0].node->id(), kShapeNodeId1);
    EXPECT_EQ(hits[1].node->id(), kShapeNodeId2);
  }
}

// A comprehensive test that sets up three independent sessions, with View/ViewHolder pairs and a
// ShapeNode in each View, and checks if global hit testing has access to hittable nodes across all
// sessions.
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
  const uint32_t kShapeNodeId1 = 1001;
  CustomSession s_r = CreateRootSession(9, 9);
  {
    const uint32_t kRootNodeId = 1007;
    s_r.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));

    const uint32_t kViewHolder1Id = 1008;
    s_r.Apply(scenic::NewAddChildCmd(kSceneId, kRootNodeId));
    s_r.Apply(scenic::NewCreateViewHolderCmd(kViewHolder1Id, std::move(view_holder_token_1),
                                             "viewholder_1"));
    s_r.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolder1Id));

    const uint32_t kViewHolder2Id = 1009;
    s_r.Apply(scenic::NewCreateViewHolderCmd(kViewHolder2Id, std::move(view_holder_token_2),
                                             "viewholder_2"));
    s_r.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolder2Id));

    s_r.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolder1Id, bbox_min, bbox_max, inset_min, inset_max));

    s_r.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolder2Id, bbox_min, bbox_max, inset_min, inset_max));

    s_r.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId1));
    s_r.Apply(scenic::NewAddChildCmd(kRootNodeId, kShapeNodeId1));
    s_r.Apply(scenic::NewSetTranslationCmd(kShapeNodeId1, (float[3]){4.f, 4.f, /*z*/ -1.f}));

    const uint32_t kShapeId = 2004;
    s_r.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_r.Apply(scenic::NewSetShapeCmd(kShapeNodeId1, kShapeId));
  }

  // Two sessions (s_1 and s_2) create an overlapping and hittable surface.
  const uint32_t kShapeNodeId2 = 2003;
  CustomSession s_1(1, engine()->session_context());
  {
    const uint32_t kViewId = 2001;
    s_1.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token_1), "view_1"));

    const uint32_t kRootNodeId = 2002;
    s_1.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_1.Apply(scenic::NewAddChildCmd(kViewId, kRootNodeId));

    s_1.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId2));
    s_1.Apply(scenic::NewAddChildCmd(kRootNodeId, kShapeNodeId2));
    s_1.Apply(scenic::NewSetTranslationCmd(kShapeNodeId2, (float[3]){4.f, 4.f, /*z*/ -2.f}));

    const uint32_t kShapeId = 2004;
    s_1.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_1.Apply(scenic::NewSetShapeCmd(kShapeNodeId2, kShapeId));
  }

  const uint32_t kShapeNodeId3 = 3003;
  CustomSession s_2(2, engine()->session_context());
  {
    const uint32_t kViewId = 3001;
    s_2.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token_2), "view_2"));

    const uint32_t kRootNodeId = 3002;
    s_2.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_2.Apply(scenic::NewAddChildCmd(kViewId, kRootNodeId));

    s_2.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId3));
    s_2.Apply(scenic::NewAddChildCmd(kRootNodeId, kShapeNodeId3));
    s_2.Apply(scenic::NewSetTranslationCmd(kShapeNodeId3, (float[3]){4.f, 4.f, /*z*/ -3.f}));

    const uint32_t kShapeId = 3004;
    s_2.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_2.Apply(scenic::NewSetShapeCmd(kShapeNodeId3, kShapeId));
  }

  std::vector<Hit> hits;
  {
    HitTester hit_tester;
    hits = layer_stack()->HitTest(HitRay(4, 4), &hit_tester);
  }

  // All that for this!
  ASSERT_EQ(hits.size(), 3u) << "Should see three hits across three sessions.";
  EXPECT_EQ(hits[0].node->id(), kShapeNodeId3);
  EXPECT_EQ(hits[1].node->id(), kShapeNodeId2);
  EXPECT_EQ(hits[2].node->id(), kShapeNodeId1);
}

}  // namespace
}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
