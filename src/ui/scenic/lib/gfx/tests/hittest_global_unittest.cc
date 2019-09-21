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

// Session wrapper that references a common Engine.
class CustomSession {
 public:
  CustomSession(SessionId id, SessionContext session_context) {
    session_ = std::make_unique<Session>(id, std::move(session_context), EventReporter::Default(),
                                         ErrorReporter::Default());
  }

  ~CustomSession() {}

  void Apply(::fuchsia::ui::gfx::Command command) {
    CommandContext empty_command_context;
    bool result = session_->ApplyCommand(&empty_command_context, std::move(command));
    ASSERT_TRUE(result) << "Failed to apply: " << command;  // Fail fast.
  }

 private:
  std::unique_ptr<Session> session_;
};

// Session wrapper that references a common Engine and sets up a basic scene.
class CustomRootSession : public CustomSession {
 public:
  const uint32_t kSceneId = 20000;

  CustomRootSession(SessionId id, SessionContext session_context, float pixel_width,
                    float pixel_height)
      : CustomSession(id, session_context), compositor_global_id_(id, kCompositorId) {
    const uint32_t kLayerStackId = 20002;
    const uint32_t kLayerId = 20003;
    Apply(scenic::NewCreateCompositorCmd(kCompositorId));
    Apply(scenic::NewCreateLayerStackCmd(kLayerStackId));
    Apply(scenic::NewSetLayerStackCmd(kCompositorId, kLayerStackId));
    Apply(scenic::NewCreateLayerCmd(kLayerId));
    Apply(scenic::NewSetSizeCmd(kLayerId, (float[2]){pixel_width, pixel_height}));
    Apply(scenic::NewAddLayerCmd(kLayerStackId, kLayerId));

    const uint32_t kCameraId = 20005;
    const uint32_t kRendererId = 20006;
    Apply(scenic::NewCreateSceneCmd(kSceneId));
    Apply(scenic::NewCreateCameraCmd(kCameraId, kSceneId));
    Apply(scenic::NewCreateRendererCmd(kRendererId));
    Apply(scenic::NewSetCameraCmd(kRendererId, kCameraId));
    Apply(scenic::NewSetRendererCmd(kLayerId, kRendererId));
  }

  ~CustomRootSession() = default;

  void GetLayerStackPtr(Engine* engine, LayerStackPtr* layer_stack_out) {
    const CompositorWeakPtr compositor =
        engine->scene_graph()->GetCompositor(compositor_global_id_);
    ASSERT_TRUE(compositor);
    *layer_stack_out = compositor->layer_stack();
    ASSERT_TRUE(layer_stack_out);
  }

 private:
  const uint32_t kCompositorId = 20001;
  const scenic_impl::GlobalId compositor_global_id_;
};

// Loop fixture provides dispatcher for Engine's EventTimestamper.
using SingleSessionHitTestTest = ::gtest::TestLoopFixture;
using MultiSessionHitTestTest = ::gtest::TestLoopFixture;

// Every GFX ViewNode has id 0.
static constexpr uint32_t kViewNodeId = 0;

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
  sys::testing::ComponentContextProvider context_provider;
  std::unique_ptr<Engine> engine =
      std::make_unique<Engine>(context_provider.context(),
                               /*frame_scheduler*/ nullptr,
                               /*release fence signaller*/ nullptr, escher::EscherWeakPtr());

  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const float display_width = 1024;
  const float display_height = 768;
  CustomRootSession sess(0, engine->session_context(), display_width, display_height);
  {
    const uint32_t kViewId = 15;  // Hit
    const uint32_t kViewHolderId = 30;
    const uint32_t kShapeNodeId = 50;  // Hit
    const uint32_t kMaterialId = 60;
    const uint32_t kRectId = 70;
    const uint32_t kRootNodeId = 20007;

    const int32_t pane_width = display_width;
    const int32_t pane_height = 0.25 * display_height;

    sess.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));

    sess.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token),
                                              "MyViewHolder"));

    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "MyView"));

    // Set the bounding box on the view holder.
    const float bbox_min[3] = {0.f, 0.f, -2.f};
    const float bbox_max[3] = {display_width / 2, display_height, 1.f};
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
        kShapeNodeId, (float[3]){0.5f * pane_width, 0.5f * display_height, 0.f}));

    sess.Apply(scenic::NewAddChildCmd(sess.kSceneId, kRootNodeId));
    sess.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShapeNodeId));

    // Perform two hit tests on either side of the display.
    std::vector<Hit> hits, hits2;
    {
      LayerStackPtr layer_stack;
      sess.GetLayerStackPtr(engine.get(), &layer_stack);

      // First hit test should intersect the view's bounding box.
      escher::ray4 ray;
      ray.origin = escher::vec4(5, display_height / 2, 1.f, 1.f);
      ray.direction = escher::vec4(0.f, 0.f, -1.f, 0.f);
      HitTester hit_tester;
      hits = layer_stack->HitTest(ray, &hit_tester);
      ASSERT_EQ(hits.size(), 2u) << "Should see a hit on the rectangle and on the view node.";
      EXPECT_EQ(hits[0].node->id(), kViewNodeId) << "Closest hit should be the view";
      EXPECT_EQ(hits[1].node->id(), kShapeNodeId) << "Second closest hit should be the rectangle";

      // Second hit test should completely miss the view's bounding box.
      HitTester hit_tester2;
      ray.origin = escher::vec4(display_width / 2 + 50, display_height / 2, 1.f, 1.f);
      ray.direction = escher::vec4(0.f, 0.f, -1.f, 0.f);
      hits2 = layer_stack->HitTest(ray, &hit_tester2);
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

  const float display_width = 1024;
  const float display_height = 768;

  const uint32_t kHittableShapeNodeId = 1007;  // Hit
  CustomRootSession sess(1, engine->session_context(), /* px-width */ display_width,
                         /* px-height */ display_height);
  {
    const uint32_t kViewHolderId = 1001;
    sess.Apply(
        scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token), "ViewHolder"));
    const uint32_t kViewId = 1002;  // Hit
    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "view"));
    // Set the bounding box on the view holder.
    const float bbox_min[3] = {0.f, 0.f, -10.f};
    const float bbox_max[3] = {display_width, display_height, 0.f};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
    sess.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolderId, bbox_min, bbox_max, inset_min, inset_max));
    sess.Apply(scenic::NewAddChildCmd(sess.kSceneId, kViewHolderId));

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
        kUnhittableShapeNodeId, (float[3]){display_width / 2.f, display_height / 2.f, -5.f}));
    const uint32_t kShapeId1 = 1006;
    sess.Apply(scenic::NewCreateRectangleCmd(kShapeId1, display_width, display_height));
    sess.Apply(scenic::NewSetShapeCmd(kUnhittableShapeNodeId, kShapeId1));

    // Create second branch
    sess.Apply(scenic::NewCreateShapeNodeCmd(kHittableShapeNodeId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kHittableShapeNodeId));
    const uint32_t kShapeId2 = 1008;
    sess.Apply(scenic::NewCreateRectangleCmd(kShapeId2, display_width, display_height));
    sess.Apply(scenic::NewSetShapeCmd(kHittableShapeNodeId, kShapeId2));
    // Move to middle of view, below UnhittableShapeNode.
    sess.Apply(scenic::NewSetTranslationCmd(
        kHittableShapeNodeId, (float[3]){display_width / 2.f, display_height / 2.f, -2.5f}));
  }

  std::vector<Hit> hits;
  {
    LayerStackPtr layer_stack;
    sess.GetLayerStackPtr(engine.get(), &layer_stack);

    escher::ray4 ray;
    ray.origin = escher::vec4(display_width / 2.f, display_height / 2.f, 1.f, 1.f);
    ray.direction = escher::vec4(0.f, 0.f, -1.f, 0.f);
    HitTester hit_tester;
    hits = layer_stack->HitTest(ray, &hit_tester);
  }

  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].node->id(), kViewNodeId) << "Closest hit should be the view.";
  EXPECT_EQ(hits[1].node->id(), kHittableShapeNodeId)
      << "Second closest hit should be the hittable rectangle.";
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
  sys::testing::ComponentContextProvider context_provider;
  std::unique_ptr<Engine> engine =
      std::make_unique<Engine>(context_provider.context(), /*frame_scheduler*/ nullptr,
                               /*release fence signaller*/ nullptr, escher::EscherWeakPtr());

  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  constexpr float display_width = 1024;
  constexpr float display_height = 768;

  // Root session sets up the scene and two view holders.
  CustomRootSession sess(0, engine->session_context(), /* px-width */ display_width,
                         /* px-height */ display_height);
  {
    // Create root node and middle node.
    const uint32_t kRootNodeId = 20007;
    sess.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    const uint32_t kViewHolderId = 35;
    sess.Apply(
        scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token), "ViewHolder"));

    // Add the first view holder as a child of the root node, and the second
    // view holder as a child of the first view holder.
    sess.Apply(scenic::NewAddChildCmd(sess.kSceneId, kRootNodeId));
    sess.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolderId));

    // Set view_holder 1's bounding box. It is a small box centered in the display.
    const float width = 100, height = 100;
    const float bbox_min[3] = {(display_width - width) / 2, (display_height - height) / 2, -6};
    const float bbox_max[3] = {(display_width + width) / 2, (display_height + height) / 2, -4};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
    sess.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolderId, bbox_min, bbox_max, inset_min, inset_max));
  }

  // Sets up the parent view.
  const uint32_t kSession1 = 1;
  CustomSession sess1(kSession1, engine->session_context());
  {
    const uint32_t kViewId = 15;  // Hit
    sess1.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "MyView"));
    const uint32_t kMiddleNodeId = 37;
    sess1.Apply(scenic::NewCreateEntityNodeCmd(kMiddleNodeId));
    sess1.Apply(scenic::NewAddChildCmd(kViewId, kMiddleNodeId));
    const uint32_t kViewHolderId2 = 36;
    sess1.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId2, std::move(view_holder_token2),
                                               "ViewHolder2"));

    sess1.Apply(scenic::NewAddChildCmd(kMiddleNodeId, kViewHolderId2));

    // Set view holder 2's bounding box. It takes up the entire display and thus is bigger
    // than it's parent's box.
    const float bbox_min2[3] = {0, 0, -9};
    const float bbox_max2[3] = {display_width, display_height, 0};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
    sess1.Apply(scenic::NewSetViewPropertiesCmd(kViewHolderId2, bbox_min2, bbox_max2, inset_min,
                                                inset_max));
  }

  // Setup the child view.
  const uint32_t kShapeNodeId = 50;  // Hit
  const uint32_t kSession2 = 2;
  CustomSession sess2(kSession2, engine->session_context());
  {
    const uint32_t kViewId2 = 16;  // Hit
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
        kShapeNodeId, (float[3]){display_width / 2, display_height / 2, -5.f}));

    sess2.Apply(scenic::NewAddChildCmd(kViewId2, kShapeNodeId));
  }

  // Perform two hit tests on either side of the display.
  std::vector<Hit> hits;
  {
    LayerStackPtr layer_stack;
    sess.GetLayerStackPtr(engine.get(), &layer_stack);

    // First hit test should intersect the view's bounding box.
    escher::ray4 ray;
    ray.origin = escher::vec4(display_width / 2, display_height / 2, 1, 1);
    ray.direction = escher::vec4(0.f, 0.f, -1.f, 0.f);
    HitTester hit_tester;
    hits = layer_stack->HitTest(ray, &hit_tester);
    ASSERT_EQ(hits.size(), 3u) << "Should hit the parent, child, and the shape.";
    EXPECT_EQ(hits[0].node->global_id(), GlobalId(kSession1, kViewNodeId))
        << "Closest hit should be the parent view.";
    EXPECT_EQ(hits[1].node->global_id(), GlobalId(kSession2, kViewNodeId))
        << "Second closest hit should be the child view.";
    EXPECT_EQ(hits[2].node->id(), kShapeNodeId) << "Third hit should be the shape.";
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
  constexpr float display_width = 1024;
  constexpr float display_height = 768;

  sys::testing::ComponentContextProvider context_provider;
  std::unique_ptr<Engine> engine =
      std::make_unique<Engine>(context_provider.context(), /*frame_scheduler*/ nullptr,
                               /*release fence signaller*/ nullptr, escher::EscherWeakPtr());

  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  // Root session sets up the scene and two view holders.
  CustomRootSession sess(0, engine->session_context(), /* px-width */ display_width,
                         /* px-height */ display_height);
  {
    // Create root node and view holder node.
    const uint32_t kRootNodeId = 20007;
    sess.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    const uint32_t kViewHolderId = 35;
    sess.Apply(
        scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token), "ViewHolder"));

    // Add the first view holder as a child of the root node, and the second
    // view holder as a child of the first view holder.
    sess.Apply(scenic::NewAddChildCmd(sess.kSceneId, kRootNodeId));
    sess.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolderId));

    // Set view_holder 1's bounding box. It takes up the left-hand side of the display.
    const float bbox_min[3] = {0, 0, -9};
    const float bbox_max[3] = {display_width / 2, display_height / 2, 0};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
    sess.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolderId, bbox_min, bbox_max, inset_min, inset_max));
  }

  // Sets up the parent view.
  CustomSession sess1(1, engine->session_context());
  {
    const uint32_t kViewId = 15;
    sess1.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "MyView"));
    const uint32_t kMiddleNodeId = 37;
    sess1.Apply(scenic::NewCreateEntityNodeCmd(kMiddleNodeId));
    sess1.Apply(scenic::NewAddChildCmd(kViewId, kMiddleNodeId));
    const uint32_t kViewHolderId2 = 36;
    sess1.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId2, std::move(view_holder_token2),
                                               "ViewHolder2"));

    sess1.Apply(scenic::NewAddChildCmd(kMiddleNodeId, kViewHolderId2));

    // Set view holder 2's bounding box. It takes up the right-hand side of the display.
    const float bbox_min2[3] = {display_width / 2, display_height / 2, -9};
    const float bbox_max2[3] = {display_width, display_height, 0};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
    sess1.Apply(scenic::NewSetViewPropertiesCmd(kViewHolderId2, bbox_min2, bbox_max2, inset_min,
                                                inset_max));
  }

  // Setup the child view.
  CustomSession sess2(2, engine->session_context());
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
        kShapeNodeId, (float[3]){3.f * display_width / 4.f, 3.f * display_height / 4.f, -5.f}));

    sess2.Apply(scenic::NewAddChildCmd(kViewId2, kShapeNodeId));
  }

  // Perform two hit tests on either side of the display.
  std::vector<Hit> hits;
  {
    LayerStackPtr layer_stack;
    sess.GetLayerStackPtr(engine.get(), &layer_stack);

    // First hit test should intersect the view's bounding box.
    escher::ray4 ray;
    ray.origin = escher::vec4(3.f * display_width / 4, 3.f * display_height / 4.f, 1, 1);
    ray.direction = escher::vec4(0.f, 0.f, -1.f, 0.f);
    HitTester hit_tester;
    hits = layer_stack->HitTest(ray, &hit_tester);
    EXPECT_EQ(hits.size(), 0u) << "Should not hit anything at all";
  }
}

// A comprehensive test that sets up three independent sessions, with
// View/ViewHolder pairs, and checks if global hit testing has access to
// hittable nodes across all sessions.
TEST_F(MultiSessionHitTestTest, GlobalHits) {
  sys::testing::ComponentContextProvider context_provider;
  std::unique_ptr<Engine> engine =
      std::make_unique<Engine>(context_provider.context(), /*frame_scheduler*/ nullptr,
                               /*release fence signaller*/ nullptr, escher::EscherWeakPtr());

  // Create our tokens for View/ViewHolder creation.
  auto [view_token_1, view_holder_token_1] = scenic::ViewTokenPair::New();
  auto [view_token_2, view_holder_token_2] = scenic::ViewTokenPair::New();

  // Create bounds for the views.
  const float bbox_min[3] = {0, 0, -4};
  const float bbox_max[3] = {10, 10, 0};
  const float inset_min[3] = {0, 0, 0};
  const float inset_max[3] = {0, 0, 0};

  // Root session sets up the scene and two view holders.
  CustomRootSession s_r(0, engine->session_context(), /*px-width*/ 9, /*px-height*/ 9);
  {
    const uint32_t kRootNodeId = 1007;
    s_r.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));

    const uint32_t kViewHolder1Id = 1008;
    s_r.Apply(scenic::NewAddChildCmd(s_r.kSceneId, kRootNodeId));
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
  }

  // Two sessions (s_1 and s_2) create an overlapping and hittable surface.
  const uint32_t kSession1 = 1;
  const uint32_t kChildId1 = 2003;  // Hit
  CustomSession s_1(kSession1, engine->session_context());
  {
    const uint32_t kViewId = 2001;  // Hit
    s_1.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token_1), "view_1"));

    const uint32_t kRootNodeId = 2002;
    s_1.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_1.Apply(scenic::NewAddChildCmd(kViewId, kRootNodeId));

    s_1.Apply(scenic::NewCreateShapeNodeCmd(kChildId1));
    s_1.Apply(scenic::NewAddChildCmd(kRootNodeId, kChildId1));
    s_1.Apply(scenic::NewSetTranslationCmd(kChildId1, (float[3]){4.f, 4.f, /*z*/ -2.f}));

    const uint32_t kShapeId = 2004;
    s_1.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_1.Apply(scenic::NewSetShapeCmd(kChildId1, kShapeId));
  }

  const uint32_t kSession2 = 2;
  const uint32_t kChildId2 = 3003;  // Hit
  CustomSession s_2(kSession2, engine->session_context());
  {
    const uint32_t kViewId = 3001;  // Hit
    s_2.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token_2), "view_2"));

    const uint32_t kRootNodeId = 3002;
    s_2.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_2.Apply(scenic::NewAddChildCmd(kViewId, kRootNodeId));

    s_2.Apply(scenic::NewCreateShapeNodeCmd(kChildId2));
    s_2.Apply(scenic::NewAddChildCmd(kRootNodeId, kChildId2));
    s_2.Apply(scenic::NewSetTranslationCmd(kChildId2, (float[3]){4.f, 4.f, /*z*/ -3.f}));

    const uint32_t kShapeId = 3004;
    s_2.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_2.Apply(scenic::NewSetShapeCmd(kChildId2, kShapeId));
  }

#if 0
  FXL_LOG(INFO) << engine->DumpScenes();  // Handy debugging.
#endif

  std::vector<Hit> hits;
  {
    LayerStackPtr layer_stack;
    s_r.GetLayerStackPtr(engine.get(), &layer_stack);

    escher::ray4 ray;
    ray.origin = escher::vec4(4.f, 4.f, 1.f, 1.f);
    ray.direction = escher::vec4(0.f, 0.f, -1.f, 0.f);
    HitTester hit_tester;
    hits = layer_stack->HitTest(ray, &hit_tester);
  }

  // All that for this!
  ASSERT_EQ(hits.size(), 4u) << "Should see four hits across three sessions.";
  EXPECT_EQ(hits[0].node->id(), kViewNodeId) << "Closest hit should be the view of session 2.";
  EXPECT_EQ(hits[0].node->global_id(), GlobalId(kSession2, kViewNodeId))
      << "Closest hit should be the view of session 2.";
  EXPECT_EQ(hits[1].node->global_id(), GlobalId(kSession1, kViewNodeId))
      << "Closest hit should be the view of session 1.";
  EXPECT_EQ(hits[2].node->id(), kChildId2) << "Third closest hit should be rectangle of session 2.";
  EXPECT_EQ(hits[3].node->id(), kChildId1)
      << "Fourth closest hit should be rectangle of session 1.";
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
