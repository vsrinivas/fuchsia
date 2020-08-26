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
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/gfx/engine/hit.h"
#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"
#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"
#include "src/ui/scenic/lib/gfx/resources/camera.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/renderers/renderer.h"
#include "src/ui/scenic/lib/input/helper.h"
#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"
#include "src/ui/scenic/lib/scenic/util/print_event.h"
#include "src/ui/scenic/lib/utils/helpers.h"

#include <glm/gtc/epsilon.hpp>
#include <glm/gtx/string_cast.hpp>

namespace scenic_impl {
namespace gfx {
namespace test {
namespace {

// Creates a hit ray at z = -1000, pointing in the z-direction.
escher::ray4 CreateZRay(glm::vec2 coords) {
  return {
      // Origin as homogeneous point.
      .origin = {coords.x, coords.y, -1000, 1},
      .direction = {0, 0, 1, 0},
  };
}

// Session wrapper that references a common Engine.
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

// Accumulator that just accumulates all hits.
template <typename T>
class TestHitAccumulator : public HitAccumulator<T> {
 public:
  const std::vector<T>& hits() const { return hits_; }

  // |HitAccumulator<T>|
  void Add(const T& hit) override { hits_.push_back(hit); }

  // |HitAccumulator<T>|
  bool EndLayer() override { return true; }

 private:
  std::vector<T> hits_;
};

// Loop fixture provides dispatcher for Engine's EventTimestamper.
// Many hit tests are performed indirectly through a LayerStack owned by this class to access the
// scene graph.
class HitTestTest : public gtest::TestLoopFixture {
 public:
  enum : uint32_t {
    kRootSessionId = 1,
    kCompositorId = 20001,
    kLayerStackId,
    kLayerId,
    kSceneId,
    kCameraId,
    kRendererId,
  };

  Engine* engine() { return engine_.get(); }
  float layer_width() const { return layer_width_; }
  float layer_height() const { return layer_height_; }

  // | ::testing::Test |
  void SetUp() override {
    gtest::TestLoopFixture::SetUp();
    engine_ = std::make_unique<Engine>(context_provider_.context(),
                                       /* frame_scheduler */ nullptr,
                                       /* release_fence_signaller */ nullptr,
                                       /* escher */ nullptr);
  }

  // | ::testing::Test |
  void TearDown() override {
    gtest::TestLoopFixture::TearDown();
    engine_.reset();
  }

  CustomSession CreateSession(SessionId id) {
    return CustomSession(id, engine_->session_context());
  }

  // Creates a session ID 0 with a compositor, layer stack, layer, scene, camera, and renderer.
  CustomSession CreateRootSession(float layer_width, float layer_height) {
    layer_width_ = layer_width;
    layer_height_ = layer_height;
    CustomSession session = CreateSession(kRootSessionId);

    session.Apply(scenic::NewCreateCompositorCmd(kCompositorId));
    session.Apply(scenic::NewCreateLayerStackCmd(kLayerStackId));
    session.Apply(scenic::NewSetLayerStackCmd(kCompositorId, kLayerStackId));
    session.Apply(scenic::NewCreateLayerCmd(kLayerId));
    session.Apply(scenic::NewSetSizeCmd(kLayerId, {layer_width, layer_height}));
    session.Apply(scenic::NewAddLayerCmd(kLayerStackId, kLayerId));

    session.Apply(scenic::NewCreateSceneCmd(kSceneId));
    session.Apply(scenic::NewCreateCameraCmd(kCameraId, kSceneId));
    session.Apply(scenic::NewCreateRendererCmd(kRendererId));
    session.Apply(scenic::NewSetCameraCmd(kRendererId, kCameraId));
    session.Apply(scenic::NewSetRendererCmd(kLayerId, kRendererId));

    return session;
  }

  // Direct scene access for more focused hit tester unit testing.
  Scene* scene() {
    const CompositorWeakPtr& compositor =
        engine_->scene_graph()->GetCompositor(GlobalId(kRootSessionId, kCompositorId));
    FX_CHECK(compositor);
    LayerStackPtr layer_stack = compositor->layer_stack();
    FX_CHECK(layer_stack);
    const auto& layers = layer_stack->layers();
    FX_CHECK(layers.size() == 1);
    const Layer* layer = layers.begin()->get();
    FX_CHECK(layer->renderer());
    FX_CHECK(layer->renderer()->camera());
    FX_CHECK(layer->renderer()->camera()->scene());
    return layer->renderer()->camera()->scene().get();
  }

 private:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<Engine> engine_;

  float layer_width_, layer_height_;
};

using SingleSessionHitTestTest = HitTestTest;
using MultiSessionHitTestTest = HitTestTest;

// Makes sure basic hit coordinates are correct.
//
// This scene includes a full-screen rectangle at z = -1 in a 16 x 9 x 1000 viewing volume.
TEST_F(SingleSessionHitTestTest, HitCoordinates) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  enum : uint32_t {
    kViewHolderId = 10,
    kViewId,
    kShapeId,
    kRectId,
    kMaterialId,
  };

  CustomSession sess = CreateRootSession(16, 9);
  zx_koid_t view_ref_koid = ZX_KOID_INVALID;
  {
    sess.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token),
                                              "MyViewHolder"));
    auto pair = scenic::ViewRefPair::New();
    view_ref_koid = utils::ExtractKoid(pair.view_ref);
    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), std::move(pair.control_ref),
                                        std::move(pair.view_ref), "MyView"));
    sess.Apply(scenic::NewSetViewPropertiesCmd(kViewHolderId,
                                               {.bounding_box{.min{0, 0, -2}, .max{16, 9, 0}}}));

    // Rectangle (full screen) and material
    sess.Apply(scenic::NewCreateMaterialCmd(kMaterialId));
    sess.Apply(scenic::NewSetColorCmd(kMaterialId, 0, 255, 255, 255));
    sess.Apply(scenic::NewCreateRectangleCmd(kRectId, 16, 9));

    // Shape
    sess.Apply(scenic::NewCreateShapeNodeCmd(kShapeId));
    sess.Apply(scenic::NewSetShapeCmd(kShapeId, kRectId));
    sess.Apply(scenic::NewSetMaterialCmd(kShapeId, kMaterialId));
    sess.Apply(scenic::NewSetTranslationCmd(kShapeId, {8, 4.5f, -1}));

    // Graph
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShapeId));
  }

  {
    // Hit from (1, 1.5) should be at (1, 1.5, -1) in view coordinates.
    // Depth should be 999:
    // * hit ray originates at -1000 in World Space in direction z = 1
    // * geometry is at z = 1 in World Space
    //  result: hit distance = 999
    TestHitAccumulator<ViewHit> accumulator;
    const glm::vec2 world_space_point{1, 1.5f};
    const escher::ray4 ray = CreateZRay(world_space_point);
    HitTest(scene(), ray, &accumulator, /*semantic_hit_test*/ false);
    ASSERT_FALSE(accumulator.hits().empty());

    const ViewHit& hit = accumulator.hits().front();
    EXPECT_EQ(hit.view_ref_koid, view_ref_koid);
    EXPECT_NEAR(hit.distance, 999.f, std::numeric_limits<float>::epsilon());
  }
}

// Makes sure that content scaling does not affect hit depth incorrectly.
//
// This scene includes a full-screen rectangle at z = -1 in a 16 x 9 x 1000 viewing volume. The
// rectangle is scaled to 2x.
TEST_F(SingleSessionHitTestTest, Scaling) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  enum : uint32_t {
    kViewHolderId = 10,
    kViewId,
    kShapeId,
    kRectId,
    kMaterialId,
  };

  CustomSession sess = CreateRootSession(16, 9);
  zx_koid_t view_ref_koid = ZX_KOID_INVALID;
  {
    sess.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token),
                                              "MyViewHolder"));
    auto pair = scenic::ViewRefPair::New();
    view_ref_koid = utils::ExtractKoid(pair.view_ref);
    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), std::move(pair.control_ref),
                                        std::move(pair.view_ref), "MyView"));
    sess.Apply(scenic::NewSetViewPropertiesCmd(kViewHolderId,
                                               {.bounding_box{.min{0, 0, -2}, .max{16, 9, 0}}}));

    // Rectangle (half scale) and material
    sess.Apply(scenic::NewCreateMaterialCmd(kMaterialId));
    sess.Apply(scenic::NewSetColorCmd(kMaterialId, 0, 255, 255, 255));
    sess.Apply(scenic::NewCreateRectangleCmd(kRectId, 8, 4.5));

    // Shape
    sess.Apply(scenic::NewCreateShapeNodeCmd(kShapeId));
    sess.Apply(scenic::NewSetShapeCmd(kShapeId, kRectId));
    sess.Apply(scenic::NewSetMaterialCmd(kShapeId, kMaterialId));
    sess.Apply(scenic::NewSetTranslationCmd(kShapeId, {8, 4.5f, -1}));
    sess.Apply(scenic::NewSetScaleCmd(kShapeId, {2, 2, 2}));

    // Graph
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShapeId));
  }

  {
    // Hit from (1, 1.5) should be at (1, 1.5, -1) in view coordinates and depth should
    // be 999 (z = -1 in 1000-space). Although the rectangle is scaled, the view is not.
    TestHitAccumulator<ViewHit> accumulator;
    const glm::vec2 world_space_point{1, 1.5f};
    const escher::ray4 ray = CreateZRay(world_space_point);
    HitTest(scene(), ray, &accumulator, /*semantic_hit_test*/ false);
    ASSERT_FALSE(accumulator.hits().empty());

    const ViewHit& hit = accumulator.hits().front();
    EXPECT_EQ(hit.view_ref_koid, view_ref_koid);
    EXPECT_NEAR(hit.distance, 999.f, std::numeric_limits<float>::epsilon());
  }
}

// Makes sure view-space hit coordinates are correct under view transformation.
//
// This scene includes a centered 5 x 3 rectangle at z = -1 in a 16 x 9 x 1000 viewing volume where
// the view is translated by (3, 2, 1) and scaled by 3x. So, the resulting rectangle is from
// (3, 2, -2) to (18, 11, -2) in World Space.
TEST_F(SingleSessionHitTestTest, ViewTransform) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  enum : uint32_t {
    kViewHolderId = 10,
    kViewId,
    kShapeId,
    kRectId,
    kMaterialId,
  };

  CustomSession sess = CreateRootSession(16, 9);
  zx_koid_t view_ref_koid = ZX_KOID_INVALID;
  {
    sess.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token),
                                              "MyViewHolder"));
    auto pair = scenic::ViewRefPair::New();
    view_ref_koid = utils::ExtractKoid(pair.view_ref);
    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), std::move(pair.control_ref),
                                        std::move(pair.view_ref), "MyView"));
    sess.Apply(scenic::NewSetViewPropertiesCmd(kViewHolderId,
                                               {.bounding_box{.min{0, 0, -2}, .max{16, 9, 0}}}));

    // Rectangle (half scale) and material
    sess.Apply(scenic::NewCreateMaterialCmd(kMaterialId));
    sess.Apply(scenic::NewSetColorCmd(kMaterialId, 0, 255, 255, 255));
    sess.Apply(scenic::NewCreateRectangleCmd(kRectId, 5, 3));

    // Shape
    sess.Apply(scenic::NewCreateShapeNodeCmd(kShapeId));
    sess.Apply(scenic::NewSetShapeCmd(kShapeId, kRectId));
    sess.Apply(scenic::NewSetMaterialCmd(kShapeId, kMaterialId));
    sess.Apply(scenic::NewSetTranslationCmd(kShapeId, {2.5, 1.5f, -1}));

    // Graph
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShapeId));
    sess.Apply(scenic::NewSetTranslationCmd(kViewHolderId, {3, 2, 1}));
    sess.Apply(scenic::NewSetScaleCmd(kViewHolderId, {3, 3, 3}));
  }

  {
    // Hit from (5, 6) should be at (2/3, 4/3, -1) in view coordinates and depth should
    // be 998 (z = -2 in a World Space with hit ray originating in a z = -1000)
    TestHitAccumulator<ViewHit> accumulator;
    const glm::vec2 world_space_point{5, 6};
    const escher::ray4 ray = CreateZRay(world_space_point);
    HitTest(scene(), ray, &accumulator, /*semantic_hit_test*/ false);
    ASSERT_FALSE(accumulator.hits().empty());

    const ViewHit& hit = accumulator.hits().front();
    EXPECT_EQ(hit.view_ref_koid, view_ref_koid);
    // Need to check against 1000x epsilon, since we lose that much precision starting the ray at
    // -1000.
    EXPECT_NEAR(hit.distance, 998.f, 1000 * std::numeric_limits<float>::epsilon());
  }
}

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
TEST_F(SingleSessionHitTestTest, ViewClipping) {
  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  CustomSession sess = CreateRootSession(1024, 768);
  {
    enum : uint32_t {
      kViewId = 15,
      kViewHolderId,
      kShapeNodeId,
      kRectId,
    };

    const int32_t pane_width = layer_width();
    const int32_t pane_height = 0.25 * layer_height();

    sess.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token),
                                              "MyViewHolder"));
    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "MyView"));

    // Set the bounding box on the view holder.
    const std::array<float, 3> bbox_min = {0.f, 0.f, -2.f};
    const std::array<float, 3> bbox_max = {layer_width() / 2, layer_height(), 1.f};
    const std::array<float, 3> inset_min = {0, 0, 0};
    const std::array<float, 3> inset_max = {0, 0, 0};
    sess.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolderId, bbox_min, bbox_max, inset_min, inset_max));

    // Create shape node and apply rectangle
    sess.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId));
    sess.Apply(scenic::NewCreateRectangleCmd(kRectId, pane_width, pane_height));
    sess.Apply(scenic::NewSetShapeCmd(kShapeNodeId, kRectId));
    sess.Apply(scenic::NewSetTranslationCmd(kShapeNodeId,
                                            {0.5f * pane_width, 0.5f * layer_height(), 0.f}));

    sess.Apply(scenic::NewAddChildCmd(kSceneId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShapeNodeId));
  }

  // Perform two hit tests on either side of the display.
  {
    // First hit test should intersect the view's bounding box.
    TestHitAccumulator<ViewHit> accumulator;
    const glm::vec2 world_space_point{5, layer_height() / 2};
    const escher::ray4 ray = CreateZRay(world_space_point);
    HitTest(scene(), ray, &accumulator, /*semantic_hit_test*/ false);
    EXPECT_EQ(accumulator.hits().size(), 1u) << "Should see a hit on the rectangle";
  }
  {
    // Second hit test should completely miss the view's bounding box.
    TestHitAccumulator<ViewHit> accumulator;
    const glm::vec2 world_space_point{layer_width() / 2 + 50, layer_height() / 2};
    const escher::ray4 ray = CreateZRay(world_space_point);
    HitTest(scene(), ray, &accumulator, /*semantic_hit_test*/ false);
    EXPECT_EQ(accumulator.hits().size(), 0u)
        << "Should see no hits since its outside the view bounds";
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
  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  // Create bounds for the views.
  const std::array<float, 3> bbox_min = {0, 0, -4};
  const std::array<float, 3> bbox_max = {10, 10, 0};
  const std::array<float, 3> inset_min = {0, 0, 0};
  const std::array<float, 3> inset_max = {0, 0, 0};

  const uint32_t kHittableShapeNodeId = 1007;
  CustomSession sess = CreateRootSession(1024, 768);
  {
    const uint32_t kViewHolderId = 1001;
    sess.Apply(
        scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token), "ViewHolder"));
    const uint32_t kViewId = 1002;
    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "view"));
    // Set the bounding box on the view holder.
    const std::array<float, 3> bbox_min = {0.f, 0.f, -10.f};
    const std::array<float, 3> bbox_max = {layer_width(), layer_height(), 0.f};
    const std::array<float, 3> inset_min = {0, 0, 0};
    const std::array<float, 3> inset_max = {0, 0, 0};
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
    sess.Apply(scenic::NewSetTranslationCmd(kUnhittableShapeNodeId,
                                            {layer_width() / 2.f, layer_height() / 2.f, -5.f}));
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
    sess.Apply(scenic::NewSetTranslationCmd(kHittableShapeNodeId,
                                            {layer_width() / 2.f, layer_height() / 2.f, -2.5f}));
  }

  {
    TestHitAccumulator<NodeHit> accumulator;
    const glm::vec2 world_space_point{layer_width() / 2, layer_height() / 2};
    const escher::ray4 ray = CreateZRay(world_space_point);
    HitTest(scene(), ray, &accumulator, /*semantic_hit_test*/ false);

    ASSERT_EQ(accumulator.hits().size(), 1u);
    EXPECT_EQ(accumulator.hits().front().node->id(), kHittableShapeNodeId);
  }
}

// Test to check that no hits can be detected in the subtree of a semantically invisible node when
// performing a semantic hit test, and that it performs as normal with a regular hit test.
// Sets up a scene with a semantically invisible entity node with a shape node child, above
// a semantically visible shape node, and performs two hit tests that goes through both shape nodes:
// a semantic hit test and a non-semantic hit test. Only the visible node should register a hit
// in the semantic hit test, while both shapes should register in the non-semantic test.
//
// Diagram:                |  Scene graph:
//                         |
// vvvvvvvvvvvvvvvvvvvvvv  |     View
// v                    v  |     /  \
// v    (rrrrrrrrrrr)   v  |    |  EntityNode(semantically invisible)
// v                    v  |    |    |
// v     rrrrrrrrrrr    v  |    |  ShapeNode
// v                    v  |    |
// vvvvvvvvvvvvvvvvvvvvvv  | ShapeNode
//
// Where v represents a view, r represents the semantically visible rectangle inside that view, and
// (r) represents the semantically invisible rectangle.
TEST_F(SingleSessionHitTestTest, SemanticVisibilityTest) {
  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  // Create bounds for the views.
  const std::array<float, 3> bbox_min = {0, 0, -4};
  const std::array<float, 3> bbox_max = {10, 10, 0};
  const std::array<float, 3> inset_min = {0, 0, 0};
  const std::array<float, 3> inset_max = {0, 0, 0};

  const uint32_t kVisibleShapeNodeId = 1007;
  const uint32_t kInvisibleShapeNodeId = 1005;
  CustomSession sess = CreateRootSession(1024, 768);
  {
    const uint32_t kViewHolderId = 1001;
    sess.Apply(
        scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token), "ViewHolder"));
    const uint32_t kViewId = 1002;
    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "view"));
    // Set the bounding box on the view holder.
    const std::array<float, 3> bbox_min = {0.f, 0.f, -10.f};
    const std::array<float, 3> bbox_max = {layer_width(), layer_height(), 0.f};
    const std::array<float, 3> inset_min = {0, 0, 0};
    const std::array<float, 3> inset_max = {0, 0, 0};
    sess.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolderId, bbox_min, bbox_max, inset_min, inset_max));
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kViewHolderId));
    const uint32_t kRootNodeId = 1003;
    sess.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kRootNodeId));

    // Create first branch
    const uint32_t kInvisibleEntityNodeId = 1004;
    sess.Apply(scenic::NewCreateEntityNodeCmd(kInvisibleEntityNodeId));
    sess.Apply(scenic::NewSetSemanticVisibilityCmd(kInvisibleEntityNodeId, /*visible*/ false));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kInvisibleEntityNodeId));
    sess.Apply(scenic::NewCreateShapeNodeCmd(kInvisibleShapeNodeId));
    sess.Apply(scenic::NewAddChildCmd(kInvisibleEntityNodeId, kInvisibleShapeNodeId));
    // Move to middle of view.
    sess.Apply(scenic::NewSetTranslationCmd(kInvisibleShapeNodeId,
                                            {layer_width() / 2.f, layer_height() / 2.f, -5.f}));
    const uint32_t kShapeId1 = 1006;
    sess.Apply(scenic::NewCreateRectangleCmd(kShapeId1, layer_width(), layer_height()));
    sess.Apply(scenic::NewSetShapeCmd(kInvisibleShapeNodeId, kShapeId1));

    // Create second branch
    sess.Apply(scenic::NewCreateShapeNodeCmd(kVisibleShapeNodeId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kVisibleShapeNodeId));
    const uint32_t kShapeId2 = 1008;
    sess.Apply(scenic::NewCreateRectangleCmd(kShapeId2, layer_width(), layer_height()));
    sess.Apply(scenic::NewSetShapeCmd(kVisibleShapeNodeId, kShapeId2));
    // Move to middle of view, below InvisibleShapeNode.
    sess.Apply(scenic::NewSetTranslationCmd(kVisibleShapeNodeId,
                                            {layer_width() / 2.f, layer_height() / 2.f, -2.5f}));
  }

  const glm::vec2 world_space_point{layer_width() / 2, layer_height() / 2};
  const escher::ray4 ray = CreateZRay(world_space_point);
  {  // Semantic hit test should only register the semantically visible shape.
    TestHitAccumulator<NodeHit> accumulator;
    HitTest(scene(), ray, &accumulator, /*semantic_hit_test*/ true);

    ASSERT_EQ(accumulator.hits().size(), 1u);
    EXPECT_EQ(accumulator.hits().front().node->id(), kVisibleShapeNodeId);
  }

  {  // Non-semantic hit test should see both shapes.
    TestHitAccumulator<NodeHit> accumulator;
    HitTest(scene(), ray, &accumulator, /*semantic_hit_test*/ false);

    ASSERT_EQ(accumulator.hits().size(), 2u);
    EXPECT_EQ(accumulator.hits()[0].node->id(), kInvisibleShapeNodeId);
    EXPECT_EQ(accumulator.hits()[1].node->id(), kVisibleShapeNodeId);
  }
}

// TODO(fxbug.dev/40161): This is fragile but we don't want this to regress if we can help it before
// officially dropping support.
//
// This scene includes two rectangles: the one on the left is on the near plane of the view bound,
// and the one on the right is on the far plane.
//
// vrrrrrrrrrrvvvvvvvvvvv
// v                    v
// vvvvvvvvvvvrrrrrrrrrrv
TEST_F(SingleSessionHitTestTest, InclusiveViewBounds) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  enum : uint32_t {
    kViewHolderId = 10,
    kViewId,
    kShape1Id,
    kShape2Id,
    kRectId,
    kMaterialId,
  };

  CustomSession sess = CreateRootSession(16, 9);
  {
    sess.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token),
                                              "MyViewHolder"));
    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "MyView"));
    sess.Apply(scenic::NewSetViewPropertiesCmd(kViewHolderId,
                                               {.bounding_box{.min{0, 0, -1}, .max{16, 9, 1}}}));

    // Rectangle and material
    sess.Apply(scenic::NewCreateMaterialCmd(kMaterialId));
    sess.Apply(scenic::NewSetColorCmd(kMaterialId, 0, 255, 255, 255));
    sess.Apply(scenic::NewCreateRectangleCmd(kRectId, 8, 9));

    // Shapes
    sess.Apply(scenic::NewCreateShapeNodeCmd(kShape1Id));
    sess.Apply(scenic::NewSetShapeCmd(kShape1Id, kRectId));
    sess.Apply(scenic::NewSetMaterialCmd(kShape1Id, kMaterialId));
    sess.Apply(scenic::NewSetTranslationCmd(kShape1Id, {4, 4.5f, -1}));

    sess.Apply(scenic::NewCreateShapeNodeCmd(kShape2Id));
    sess.Apply(scenic::NewSetShapeCmd(kShape2Id, kRectId));
    sess.Apply(scenic::NewSetMaterialCmd(kShape2Id, kMaterialId));
    sess.Apply(scenic::NewSetTranslationCmd(kShape2Id, {12, 4.5f, 1}));

    // Graph
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShape1Id));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShape2Id));
  }

  {
    TestHitAccumulator<ViewHit> accumulator;
    const glm::vec2 world_space_point{4, 4.5f};
    const escher::ray4 ray = CreateZRay(world_space_point);
    HitTest(scene(), ray, &accumulator, /*semantic_hit_test*/ false);
    EXPECT_FALSE(accumulator.hits().empty());
  }
  {
    TestHitAccumulator<ViewHit> accumulator;
    const glm::vec2 world_space_point{12, 4.5f};
    const escher::ray4 ray = CreateZRay(world_space_point);
    HitTest(scene(), ray, &accumulator, /*semantic_hit_test*/ false);
    EXPECT_FALSE(accumulator.hits().empty());
  }
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
TEST_F(MultiSessionHitTestTest, ChildBiggerThanParent) {
  // Create our tokens for View/ViewHolder creation.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  CustomSession sess = CreateRootSession(1024, 768);
  {
    const uint32_t kViewHolderId = 35;
    sess.Apply(
        scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token), "ViewHolder"));

    // Add the first view holder under the scene root, and the second view holder as a child of the
    // first view holder.
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kViewHolderId));

    // Set view_holder 1's bounding box. It is a small box centered in the display.
    const float width = 100, height = 100;
    const std::array<float, 3> bbox_min = {(layer_width() - width) / 2,
                                           (layer_height() - height) / 2, -6};
    const std::array<float, 3> bbox_max = {(layer_width() + width) / 2,
                                           (layer_height() + height) / 2, -4};
    const std::array<float, 3> inset_min = {0, 0, 0};
    const std::array<float, 3> inset_max = {0, 0, 0};
    sess.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolderId, bbox_min, bbox_max, inset_min, inset_max));
  }

  // Sets up the parent view.
  CustomSession sess1 = CreateSession(2);
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
    const std::array<float, 3> bbox_min2 = {0, 0, -9};
    const std::array<float, 3> bbox_max2 = {layer_width(), layer_height(), 0};
    const std::array<float, 3> inset_min = {0, 0, 0};
    const std::array<float, 3> inset_max = {0, 0, 0};
    sess1.Apply(scenic::NewSetViewPropertiesCmd(kViewHolderId2, bbox_min2, bbox_max2, inset_min,
                                                inset_max));
  }

  // Set up the child view.
  const uint32_t kInnerShapeNodeId = 50;
  CustomSession sess2 = CreateSession(3);
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
    sess2.Apply(scenic::NewSetTranslationCmd(kOuterShapeNodeId,
                                             {layer_width() / 2, layer_height() / 2, -8.f}));
    sess2.Apply(scenic::NewAddChildCmd(kViewId2, kOuterShapeNodeId));

    // Create shape node, apply rectangle and translate it inside the parent view.
    sess2.Apply(scenic::NewCreateShapeNodeCmd(kInnerShapeNodeId));
    sess2.Apply(scenic::NewSetShapeCmd(kInnerShapeNodeId, kRectId));
    sess2.Apply(scenic::NewSetTranslationCmd(kInnerShapeNodeId,
                                             {layer_width() / 2, layer_height() / 2, -5.f}));
    sess2.Apply(scenic::NewAddChildCmd(kViewId2, kInnerShapeNodeId));
  }

  {
    TestHitAccumulator<NodeHit> accumulator;
    const glm::vec2 world_space_point{layer_width() / 2, layer_height() / 2};
    const escher::ray4 ray = CreateZRay(world_space_point);
    HitTest(scene(), ray, &accumulator, /*semantic_hit_test*/ false);
    EXPECT_EQ(accumulator.hits().size(), 1u)
        << "Should only hit the shape encompassed by both views.";
    EXPECT_EQ(accumulator.hits().front().node->id(), kInnerShapeNodeId);
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
    const std::array<float, 3> bbox_min = {0, 0, -9};
    const std::array<float, 3> bbox_max = {layer_width() / 2, layer_height() / 2, 0};
    const std::array<float, 3> inset_min = {0, 0, 0};
    const std::array<float, 3> inset_max = {0, 0, 0};
    sess.Apply(
        scenic::NewSetViewPropertiesCmd(kViewHolderId, bbox_min, bbox_max, inset_min, inset_max));
  }

  // Sets up the parent view.
  CustomSession sess1 = CreateSession(2);
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
    const std::array<float, 3> bbox_min2 = {layer_width() / 2, layer_height() / 2, -9};
    const std::array<float, 3> bbox_max2 = {layer_width(), layer_height(), 0};
    const std::array<float, 3> inset_min = {0, 0, 0};
    const std::array<float, 3> inset_max = {0, 0, 0};
    sess1.Apply(scenic::NewSetViewPropertiesCmd(kViewHolderId2, bbox_min2, bbox_max2, inset_min,
                                                inset_max));
  }

  // Set up the child view.
  CustomSession sess2 = CreateSession(3);
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
        kShapeNodeId, {3.f * layer_width() / 4.f, 3.f * layer_height() / 4.f, -5.f}));
    sess2.Apply(scenic::NewAddChildCmd(kViewId2, kShapeNodeId));
  }

  {
    TestHitAccumulator<ViewHit> accumulator;
    const glm::vec2 world_space_point{3 * layer_width() / 4, 3 * layer_height() / 4};
    const escher::ray4 ray = CreateZRay(world_space_point);
    HitTest(scene(), ray, &accumulator, /*semantic_hit_test*/ false);
    EXPECT_TRUE(accumulator.hits().empty());
  }
}

// A comprehensive test that sets up a root session and two view sessions, with a ShapeNode in the
// root scene and in each View, and checks if both view hits are produced by the
// |ViewHitAccumulator|.
TEST_F(MultiSessionHitTestTest, GlobalHits) {
  // Create our tokens for View/ViewHolder creation.
  auto [view_token_1, view_holder_token_1] = scenic::ViewTokenPair::New();
  auto [view_token_2, view_holder_token_2] = scenic::ViewTokenPair::New();

  // Create bounds for the views.
  const std::array<float, 3> bbox_min = {0, 0, -1000};
  const std::array<float, 3> bbox_max = {10, 10, 0};
  const std::array<float, 3> inset_min = {0, 0, 0};
  const std::array<float, 3> inset_max = {0, 0, 0};

  // Root session sets up the scene with two view holders and some geometry.
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

    const uint32_t kShapeNodeId = 1001;
    s_r.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId));
    s_r.Apply(scenic::NewAddChildCmd(kRootNodeId, kShapeNodeId));
    s_r.Apply(scenic::NewSetTranslationCmd(kShapeNodeId, {4.f, 4.f, /*z*/ -1.f}));

    const uint32_t kShapeId = 2004;
    s_r.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_r.Apply(scenic::NewSetShapeCmd(kShapeNodeId, kShapeId));
  }

  // Two sessions (s_1 and s_2) create an overlapping and hittable surface.
  CustomSession s_1(2, engine()->session_context());
  zx_koid_t view_ref_koid1 = ZX_KOID_INVALID;
  {
    auto pair = scenic::ViewRefPair::New();
    view_ref_koid1 = utils::ExtractKoid(pair.view_ref);
    const uint32_t kViewId1 = 2001;
    s_1.Apply(scenic::NewCreateViewCmd(kViewId1, std::move(view_token_1),
                                       std::move(pair.control_ref), std::move(pair.view_ref),
                                       "view_1"));

    const uint32_t kRootNodeId = 2002;
    s_1.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_1.Apply(scenic::NewAddChildCmd(kViewId1, kRootNodeId));

    const uint32_t kShapeNodeId = 2003;
    s_1.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId));
    s_1.Apply(scenic::NewAddChildCmd(kRootNodeId, kShapeNodeId));
    s_1.Apply(scenic::NewSetTranslationCmd(kShapeNodeId, {4.f, 4.f, /*z*/ -2.f}));

    const uint32_t kShapeId = 2004;  // Hit
    s_1.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_1.Apply(scenic::NewSetShapeCmd(kShapeNodeId, kShapeId));
  }

  CustomSession s_2(3, engine()->session_context());
  zx_koid_t view_ref_koid2 = ZX_KOID_INVALID;
  {
    auto pair = scenic::ViewRefPair::New();
    view_ref_koid2 = utils::ExtractKoid(pair.view_ref);
    const uint32_t kViewId2 = 3001;
    s_2.Apply(scenic::NewCreateViewCmd(kViewId2, std::move(view_token_2),
                                       std::move(pair.control_ref), std::move(pair.view_ref),
                                       "view_2"));

    const uint32_t kRootNodeId = 3002;
    s_2.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_2.Apply(scenic::NewAddChildCmd(kViewId2, kRootNodeId));

    const uint32_t kShapeNodeId = 3003;
    s_2.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId));
    s_2.Apply(scenic::NewAddChildCmd(kRootNodeId, kShapeNodeId));
    s_2.Apply(scenic::NewSetTranslationCmd(kShapeNodeId, {4.f, 4.f, /*z*/ -3.f}));

    const uint32_t kShapeId = 3004;  // Hit
    s_2.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_2.Apply(scenic::NewSetShapeCmd(kShapeNodeId, kShapeId));
  }

  {
    ViewHitAccumulator accumulator;
    const glm::vec2 world_space_point{4, 4};
    const escher::ray4 ray = CreateZRay(world_space_point);
    HitTest(scene(), ray, &accumulator, /*semantic_hit_test*/ false);
    accumulator.EndLayer();
    const auto& hits = accumulator.hits();

    // All that for this!
    ASSERT_EQ(hits.size(), 2u) << "Should see two hits across two view sessions.";
    EXPECT_EQ(hits[0].view_ref_koid, view_ref_koid2);
    EXPECT_EQ(hits[1].view_ref_koid, view_ref_koid1);
  }
}

}  // namespace
}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
