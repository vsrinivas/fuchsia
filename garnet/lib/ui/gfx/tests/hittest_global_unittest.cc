// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <vector>

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/hit.h"
#include "garnet/lib/ui/gfx/engine/hit_tester.h"
#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "garnet/lib/ui/scenic/util/print_event.h"
#include "gtest/gtest.h"
#include "lib/fostr/fidl/fuchsia/ui/scenic/formatting.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "lib/ui/scenic/cpp/view_token_pair.h"
#include "sdk/lib/ui/scenic/cpp/resources.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/forward_declarations.h"

// The test setup here is sufficiently different from hittest_unittest.cc to
// merit its own file.  We access the global hit test through the compositor,
// instead of through a session.

namespace scenic_impl {
namespace gfx {
namespace test {

// Session wrapper that references a common Engine.
//
// This class calls Session::TearDown directly and so avoids pulling in
// SessionHandler and SessionManager; these make a call to TearDown that's
// triggered by Engine::RenderFrame, and we don't need that here.
class CustomSession {
 public:
  CustomSession(SessionId id, SessionContext session_context) {
    session_ = std::make_unique<Session>(id, std::move(session_context), EventReporter::Default(),
                                         ErrorReporter::Default());
  }

  ~CustomSession() {}

  void Apply(::fuchsia::ui::gfx::Command command) {
    CommandContext empty_command_context(nullptr);
    bool result = session_->ApplyCommand(&empty_command_context, std::move(command));
    ASSERT_TRUE(result) << "Failed to apply: " << command;  // Fail fast.
  }

 private:
  std::unique_ptr<Session> session_;
};

// Loop fixture provides dispatcher for Engine's EventTimestamper.
using SingleSessionHitTestTest = ::gtest::TestLoopFixture;
using MultiSessionHitTestTest = ::gtest::TestLoopFixture;

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
  constexpr float display_width = 1024;
  constexpr float display_height = 768;

  DisplayManager display_manager;
  display_manager.SetDefaultDisplayForTests(std::make_unique<Display>(
      /*id*/ 0, /*px-width*/ display_width, /*px-height*/ display_height));
  std::unique_ptr<Engine> engine = std::make_unique<Engine>(
      /*frame_scheduler*/ nullptr, &display_manager,
      /*release fence signaller*/ nullptr, escher::EscherWeakPtr());

  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  CustomSession sess(0, engine->session_context());
  {
    const uint32_t kCompositorId = 20001;
    const uint32_t kLayerStackId = 20002;
    const uint32_t kLayerId = 20003;
    sess.Apply(scenic::NewCreateCompositorCmd(kCompositorId));
    sess.Apply(scenic::NewCreateLayerStackCmd(kLayerStackId));
    sess.Apply(scenic::NewSetLayerStackCmd(kCompositorId, kLayerStackId));
    sess.Apply(scenic::NewCreateLayerCmd(kLayerId));
    sess.Apply(scenic::NewSetSizeCmd(
        kLayerId, (float[2]){/*px-width*/ display_width, /*px-height*/ display_height}));
    sess.Apply(scenic::NewAddLayerCmd(kLayerStackId, kLayerId));

    const uint32_t kSceneId = 20004;  // Hit
    const uint32_t kCameraId = 20005;
    const uint32_t kRendererId = 20006;
    sess.Apply(scenic::NewCreateSceneCmd(kSceneId));
    sess.Apply(scenic::NewCreateCameraCmd(kCameraId, kSceneId));
    sess.Apply(scenic::NewCreateRendererCmd(kRendererId));
    sess.Apply(scenic::NewSetCameraCmd(kRendererId, kCameraId));
    sess.Apply(scenic::NewSetRendererCmd(kLayerId, kRendererId));

    const uint32_t kViewId = 15;
    const uint32_t kViewHolderId = 30;  // Hit
    const uint32_t kShapeNodeId = 50;   // Hit
    const uint32_t kMaterialId = 60;
    const uint32_t kRectId = 70;         // Hit
    const uint32_t kRootNodeId = 20007;  // Hit

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
        kShapeNodeId, (float[3]){0.5 * pane_width, 0.5 * display_height, 0.f}));

    sess.Apply(scenic::NewAddChildCmd(kSceneId, kRootNodeId));
    sess.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShapeNodeId));

    // Perform two hit tests on either side of the display.
    std::vector<Hit> hits, hits2;
    {
      // Models input subsystem's access to Engine internals.
      // For simplicity, we use the first (and only) compositor and layer stack.
      const CompositorWeakPtr& compositor = engine->scene_graph()->first_compositor();
      ASSERT_TRUE(compositor);
      LayerStackPtr layer_stack = compositor->layer_stack();
      ASSERT_TRUE(layer_stack);

      // First hit test should intersect the view's bounding box.
      escher::ray4 ray;
      ray.origin = escher::vec4(5, display_height / 2, 1.f, 1.f);
      ray.direction = escher::vec4(0.f, 0.f, -1.f, 0.f);
      GlobalHitTester hit_tester;
      hits = layer_stack->HitTest(ray, &hit_tester);
      EXPECT_EQ(hits.size(), 5u) << "Should see a single hit on the rectangle";

      // Second hit test should completely miss the view's bounding box.
      GlobalHitTester hit_tester2;
      ray.origin = escher::vec4(display_width / 2 + 50, display_height / 2, 1.f, 1.f);
      ray.direction = escher::vec4(0.f, 0.f, -1.f, 0.f);
      hits2 = layer_stack->HitTest(ray, &hit_tester2);
      EXPECT_EQ(hits2.size(), 0u) << "Should see no hits since its outside the view bounds";
    }
  }
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
  constexpr float display_width = 1024;
  constexpr float display_height = 768;

  DisplayManager display_manager;
  display_manager.SetDefaultDisplayForTests(std::make_unique<Display>(
      /*id*/ 0, /*px-width*/ display_width, /*px-height*/ display_height));
  std::unique_ptr<Engine> engine = std::make_unique<Engine>(
      /*frame_scheduler*/ nullptr, &display_manager,
      /*release fence signaller*/ nullptr, escher::EscherWeakPtr());

  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  const uint32_t kSceneId = 20004;     // Hit
  const uint32_t kRootNodeId = 20007;  // Hit
  const uint32_t kMiddleNodeId = 37;   // Hit
  const uint32_t kViewHolderId = 35;   // Hit
  const uint32_t kViewHolderId2 = 36;  // Hit

  // Root session sets up the scene and two view holders.
  CustomSession sess(0, engine->session_context());
  {
    const uint32_t kCompositorId = 20001;
    const uint32_t kLayerStackId = 20002;
    const uint32_t kLayerId = 20003;
    sess.Apply(scenic::NewCreateCompositorCmd(kCompositorId));
    sess.Apply(scenic::NewCreateLayerStackCmd(kLayerStackId));
    sess.Apply(scenic::NewSetLayerStackCmd(kCompositorId, kLayerStackId));
    sess.Apply(scenic::NewCreateLayerCmd(kLayerId));
    sess.Apply(scenic::NewSetSizeCmd(
        kLayerId, (float[2]){/*px-width*/ display_width, /*px-height*/ display_height}));
    sess.Apply(scenic::NewAddLayerCmd(kLayerStackId, kLayerId));

    const uint32_t kCameraId = 20005;
    const uint32_t kRendererId = 20006;
    sess.Apply(scenic::NewCreateSceneCmd(kSceneId));
    sess.Apply(scenic::NewCreateCameraCmd(kCameraId, kSceneId));
    sess.Apply(scenic::NewCreateRendererCmd(kRendererId));
    sess.Apply(scenic::NewSetCameraCmd(kRendererId, kCameraId));
    sess.Apply(scenic::NewSetRendererCmd(kLayerId, kRendererId));

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
    const float bbox_min[3] = {(display_width - width) / 2, (display_height - height) / 2, -6};
    const float bbox_max[3] = {(display_width + width) / 2, (display_height + height) / 2, -4};
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

    sess1.Apply(scenic::NewCreateEntityNodeCmd(kMiddleNodeId));

    sess1.Apply(scenic::NewAddChildCmd(kViewId, kMiddleNodeId));

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
  CustomSession sess2(2, engine->session_context());
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
        kShapeNodeId, (float[3]){display_width / 2, display_height / 2, -5.f}));

    sess2.Apply(scenic::NewAddChildCmd(kViewId2, kShapeNodeId));
  }

  // Perform two hit tests on either side of the display.
  std::vector<Hit> hits;
  {
    // Models input subsystem's access to Engine internals.
    // For simplicity, we use the first (and only) compositor and layer stack.
    const CompositorWeakPtr& compositor = engine->scene_graph()->first_compositor();
    ASSERT_TRUE(compositor);
    LayerStackPtr layer_stack = compositor->layer_stack();
    ASSERT_TRUE(layer_stack);

    // First hit test should intersect the view's bounding box.
    escher::ray4 ray;
    ray.origin = escher::vec4(display_width / 2, display_height / 2, 1, 1);
    ray.direction = escher::vec4(0.f, 0.f, -1.f, 0.f);
    GlobalHitTester hit_tester;
    hits = layer_stack->HitTest(ray, &hit_tester);
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
  constexpr float display_width = 1024;
  constexpr float display_height = 768;

  DisplayManager display_manager;
  display_manager.SetDefaultDisplayForTests(std::make_unique<Display>(
      /*id*/ 0, /*px-width*/ display_width, /*px-height*/ display_height));
  std::unique_ptr<Engine> engine = std::make_unique<Engine>(
      /*frame_scheduler*/ nullptr, &display_manager,
      /*release fence signaller*/ nullptr, escher::EscherWeakPtr());

  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  const uint32_t kSceneId = 20004;
  const uint32_t kRootNodeId = 20007;
  const uint32_t kMiddleNodeId = 37;
  const uint32_t kViewHolderId = 35;
  const uint32_t kViewHolderId2 = 36;

  // Root session sets up the scene and two view holders.
  CustomSession sess(0, engine->session_context());
  {
    const uint32_t kCompositorId = 20001;
    const uint32_t kLayerStackId = 20002;
    const uint32_t kLayerId = 20003;
    sess.Apply(scenic::NewCreateCompositorCmd(kCompositorId));
    sess.Apply(scenic::NewCreateLayerStackCmd(kLayerStackId));
    sess.Apply(scenic::NewSetLayerStackCmd(kCompositorId, kLayerStackId));
    sess.Apply(scenic::NewCreateLayerCmd(kLayerId));
    sess.Apply(scenic::NewSetSizeCmd(
        kLayerId, (float[2]){/*px-width*/ display_width, /*px-height*/ display_height}));
    sess.Apply(scenic::NewAddLayerCmd(kLayerStackId, kLayerId));

    const uint32_t kCameraId = 20005;
    const uint32_t kRendererId = 20006;
    sess.Apply(scenic::NewCreateSceneCmd(kSceneId));
    sess.Apply(scenic::NewCreateCameraCmd(kCameraId, kSceneId));
    sess.Apply(scenic::NewCreateRendererCmd(kRendererId));
    sess.Apply(scenic::NewSetCameraCmd(kRendererId, kCameraId));
    sess.Apply(scenic::NewSetRendererCmd(kLayerId, kRendererId));

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

    sess1.Apply(scenic::NewCreateEntityNodeCmd(kMiddleNodeId));

    sess1.Apply(scenic::NewAddChildCmd(kViewId, kMiddleNodeId));

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
    // Models input subsystem's access to Engine internals.
    // For simplicity, we use the first (and only) compositor and layer stack.
    const CompositorWeakPtr& compositor = engine->scene_graph()->first_compositor();
    ASSERT_TRUE(compositor);
    LayerStackPtr layer_stack = compositor->layer_stack();
    ASSERT_TRUE(layer_stack);

    // First hit test should intersect the view's bounding box.
    escher::ray4 ray;
    ray.origin = escher::vec4(3.f * display_width / 4, 3.f * display_height / 4.f, 1, 1);
    ray.direction = escher::vec4(0.f, 0.f, -1.f, 0.f);
    GlobalHitTester hit_tester;
    hits = layer_stack->HitTest(ray, &hit_tester);
    EXPECT_EQ(hits.size(), 0u) << "Should not hit anything at all";
  }
}

// A comprehensive test that sets up three independent sessions, with
// View/ViewHolder pairs, and checks if global hit testing has access to
// hittable nodes across all sessions.
TEST_F(MultiSessionHitTestTest, GlobalHits) {
  DisplayManager display_manager;
  display_manager.SetDefaultDisplayForTests(std::make_unique<Display>(
      /*id*/ 0, /*px-width*/ 9, /*px-height*/ 9));
  std::unique_ptr<Engine> engine = std::make_unique<Engine>(
      /*frame_scheduler*/ nullptr, &display_manager,
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
  CustomSession s_r(0, engine->session_context());
  {
    const uint32_t kCompositorId = 1001;
    const uint32_t kLayerStackId = 1002;
    const uint32_t kLayerId = 1003;
    s_r.Apply(scenic::NewCreateCompositorCmd(kCompositorId));
    s_r.Apply(scenic::NewCreateLayerStackCmd(kLayerStackId));
    s_r.Apply(scenic::NewSetLayerStackCmd(kCompositorId, kLayerStackId));
    s_r.Apply(scenic::NewCreateLayerCmd(kLayerId));
    s_r.Apply(scenic::NewSetSizeCmd(kLayerId, (float[2]){/*px-width*/ 9, /*px-height*/ 9}));
    s_r.Apply(scenic::NewAddLayerCmd(kLayerStackId, kLayerId));

    const uint32_t kSceneId = 1004;  // Hit
    const uint32_t kCameraId = 1005;
    const uint32_t kRendererId = 1006;
    s_r.Apply(scenic::NewCreateSceneCmd(kSceneId));
    s_r.Apply(scenic::NewCreateCameraCmd(kCameraId, kSceneId));
    s_r.Apply(scenic::NewCreateRendererCmd(kRendererId));
    s_r.Apply(scenic::NewSetCameraCmd(kRendererId, kCameraId));
    s_r.Apply(scenic::NewSetRendererCmd(kLayerId, kRendererId));

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
  CustomSession s_1(1, engine->session_context());
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

  CustomSession s_2(2, engine->session_context());
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

#if 0
  FXL_LOG(INFO) << engine->DumpScenes();  // Handy debugging.
#endif

  std::vector<Hit> hits;
  {
    // Models input subsystem's access to Engine internals.
    // For simplicity, we use the first (and only) compositor and layer stack.
    const CompositorWeakPtr& compositor = engine->scene_graph()->first_compositor();
    ASSERT_TRUE(compositor);
    LayerStackPtr layer_stack = compositor->layer_stack();
    ASSERT_TRUE(layer_stack);

    escher::ray4 ray;
    ray.origin = escher::vec4(4.f, 4.f, 1.f, 1.f);
    ray.direction = escher::vec4(0.f, 0.f, -1.f, 0.f);
    GlobalHitTester hit_tester;
    hits = layer_stack->HitTest(ray, &hit_tester);
  }

  // All that for this!
  EXPECT_EQ(hits.size(), 10u) << "Should see ten hits across three sessions.";
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
