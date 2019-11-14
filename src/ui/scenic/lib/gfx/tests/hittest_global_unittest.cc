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

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"
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
#include "src/ui/scenic/lib/scenic/event_reporter.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"
#include "src/ui/scenic/lib/scenic/util/print_event.h"

#include <glm/gtc/epsilon.hpp>
#include <glm/gtx/string_cast.hpp>

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

// Creates a hit ray in world space. This is an input hit ray after being transformed by the layer.
escher::ray4 WorldSpaceHitRay(float x, float y) {
  return {
      .origin = {x, y, -1000, 1},
      .direction = {0, 0, 1000, 0},
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
    kCompositorId = 20001,
    kLayerStackId,
    kLayerId,
    kSceneId,
    kCameraId,
    kRendererId,
  };

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
    CustomSession session = CreateSession(1);

    session.Apply(scenic::NewCreateCompositorCmd(kCompositorId));
    session.Apply(scenic::NewCreateLayerStackCmd(kLayerStackId));
    session.Apply(scenic::NewSetLayerStackCmd(kCompositorId, kLayerStackId));
    session.Apply(scenic::NewCreateLayerCmd(kLayerId));
    session.Apply(scenic::NewSetSizeCmd(kLayerId, (float[]){layer_width, layer_height}));
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
    const auto& layers = layer_stack()->layers();
    FXL_CHECK(layers.size() == 1);
    const Layer* layer = layers.begin()->get();
    FXL_CHECK(layer->renderer());
    FXL_CHECK(layer->renderer()->camera());
    FXL_CHECK(layer->renderer()->camera()->scene());
    return layer->renderer()->camera()->scene().get();
  }

  // Models input subsystem's access to Engine internals.
  // For simplicity, we use the first (and only) compositor and layer stack.
  LayerStackPtr layer_stack() {
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
  {
    sess.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token),
                                              "MyViewHolder"));
    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "MyView"));
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
    sess.Apply(scenic::NewSetTranslationCmd(kShapeId, (float[]){8, 4.5f, -1}));

    // Graph
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShapeId));
  }

  {
    // Hit from (1, 1.5) should be at (1, 1.5, -1) in view coordinates.
    // Depth should be 1.999:
    // * hit ray originates in device space (clip space with -z)
    // * geometry is at z = -1 in global space
    // * orthographic projection maps z [0, 1000] to [0, 1] in clip space (now at z = .999)
    // * hit ray is at z = 1 (1 length behind the camera) with direction z = -1
    //   (result: hit z = 1.999)
    // TODO(38389): See if we can simplify this. At the very least we should be able to get rid of
    // the -z and 1-offset, but it may also be possible to redefine device-space z in terms of the
    // view volume depth (though the relative scale isn't used for anything user facing so it
    // doesn't actually matter).
    TestHitAccumulator<ViewHit> accumulator;
    const escher::ray4 ray = HitRay(1, 1.5f);
    layer_stack()->HitTest(ray, &accumulator);
    ASSERT_FALSE(accumulator.hits().empty());

    const ViewHit& hit = accumulator.hits().front();
    EXPECT_EQ(hit.view->global_id(), GlobalId(1, kViewId));
    // TODO(38389): .999f (ray origin is currently 1 length behind the camera)
    EXPECT_NEAR(hit.distance, 1.999f, std::numeric_limits<float>::epsilon());
    const glm::vec4 view = hit.transform * ray.At(hit.distance);
    static const glm::vec4 expected = {1, 1.5f, -1, 1};
    // We need to use 1000 * epsilon as the projection transform scales by 1000.
    static constexpr float epsilon = std::numeric_limits<float>::epsilon() * 1000;
    EXPECT_TRUE(glm::all(glm::epsilonEqual(view, expected, epsilon)))
        << "View hit coordinates " << glm::to_string(view) << " should be approximately "
        << glm::to_string(expected);
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
  {
    sess.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token),
                                              "MyViewHolder"));
    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "MyView"));
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
    sess.Apply(scenic::NewSetTranslationCmd(kShapeId, (float[]){8, 4.5f, -1}));
    sess.Apply(scenic::NewSetScaleCmd(kShapeId, (float[]){2, 2, 2}));

    // Graph
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShapeId));
  }

  {
    // Hit from (1, 1.5) should be at (1, 1.5, -1) in view coordinates and depth should be 1.999 (z
    // = -1 in 1000-space, + 1 due to the ray origin). Although the rectangle is scaled, the view is
    // not.
    TestHitAccumulator<ViewHit> accumulator;
    const escher::ray4 ray = HitRay(1, 1.5f);
    layer_stack()->HitTest(ray, &accumulator);
    ASSERT_FALSE(accumulator.hits().empty());

    const ViewHit& hit = accumulator.hits().front();
    EXPECT_EQ(hit.view->global_id(), GlobalId(1, kViewId));
    // TODO(38389): .999f (ray origin is currently 1 length behind the camera)
    EXPECT_NEAR(hit.distance, 1.999f, std::numeric_limits<float>::epsilon());
    const glm::vec4 view = hit.transform * ray.At(hit.distance);
    static const glm::vec4 expected = {1, 1.5f, -1, 1};
    // We need to use 1000 * epsilon as the projection transform scales by 1000.
    static constexpr float epsilon = std::numeric_limits<float>::epsilon() * 1000;
    EXPECT_TRUE(glm::all(glm::epsilonEqual(view, expected, epsilon)))
        << "View hit coordinates " << glm::to_string(view) << " should be approximately "
        << glm::to_string(expected);
  }
}

// Makes sure view-space hit coordinates are correct under view transformation.
//
// This scene includes a centered 5 x 3 rectangle at z = -1 in a 16 x 9 x 1000 viewing volume where
// the view is translated by (3, 2, 1) and scaled by 3x. So, the resulting rectangle is from (3, 2,
// -2) to (18, 11, -2) global.
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
  {
    sess.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token),
                                              "MyViewHolder"));
    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "MyView"));
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
    sess.Apply(scenic::NewSetTranslationCmd(kShapeId, (float[]){2.5, 1.5f, -1}));

    // Graph
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShapeId));
    sess.Apply(scenic::NewSetTranslationCmd(kViewHolderId, (float[]){3, 2, 1}));
    sess.Apply(scenic::NewSetScaleCmd(kViewHolderId, (float[]){3, 3, 3}));
  }

  {
    // Hit from (5, 6) should be at (2/3, 4/3, -1) in view coordinates and depth should be 1.998 (z
    // = -2 in 1000-space, + 1 due to the ray origin).
    TestHitAccumulator<ViewHit> accumulator;
    const escher::ray4 ray = HitRay(5, 6);
    layer_stack()->HitTest(ray, &accumulator);
    ASSERT_FALSE(accumulator.hits().empty());

    const ViewHit& hit = accumulator.hits().front();
    EXPECT_EQ(hit.view->global_id(), GlobalId(1, kViewId));
    // TODO(38389): .998f (ray origin is currently 1 length behind the camera)
    EXPECT_NEAR(hit.distance, 1.998f, std::numeric_limits<float>::epsilon());
    const glm::vec4 view = hit.transform * ray.At(hit.distance);
    static const glm::vec4 expected = {2.f / 3, 4.f / 3, -1, 1};
    // We need to use 1000 * epsilon as the projection transform scales by 1000.
    static constexpr float epsilon = std::numeric_limits<float>::epsilon() * 1000;
    EXPECT_TRUE(glm::all(glm::epsilonEqual(view, expected, epsilon)))
        << "View hit coordinates " << glm::to_string(view) << " should be approximately "
        << glm::to_string(expected);
  }
}

// Makes sure view-space hit coordinates are correct under camera transformation.
//
// This scene includes a full-screen rectangle at z = -1 in a 16 x 9 x 1000 viewing volume. The
// camera clip space is translated (.25, 2/3) and scaled by 3x. In terms of the viewing volume, this
// is a 2 x 3 translation.
TEST_F(SingleSessionHitTestTest, CameraTransform) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  enum : uint32_t {
    kViewHolderId = 10,
    kViewId,
    kShapeId,
    kRectId,
    kMaterialId,
  };

  CustomSession sess = CreateRootSession(16, 9);
  {
    sess.Apply(scenic::NewCreateViewHolderCmd(kViewHolderId, std::move(view_holder_token),
                                              "MyViewHolder"));
    sess.Apply(scenic::NewCreateViewCmd(kViewId, std::move(view_token), "MyView"));
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
    sess.Apply(scenic::NewSetTranslationCmd(kShapeId, (float[]){8, 4.5f, -1}));

    // Graph
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShapeId));

    // Camera
    sess.Apply(scenic::NewSetCameraClipSpaceTransformCmd(kCameraId, .25f, 2.f / 3, 3));
    // After this, the original (-1, -1) x (1, 1) NDC is mapped to (-2.75, -2 1/3) x (3.25, 3 2/3),
    // i.e. the original (0, 0) x (16, 9) input space is mapped to (-14, -6) x (34, 21) as it scales
    // to (-16, -9) x (32, 18) + the 2 x 3 translation.
  }

  {
    // Hit from (1, 1.5) should be at (5, 7.5 / 3, -1) in view coordinates (i.e. (15, 7.5) in the
    // effective input space, scaled down 3x to view space).
    // Depth should still be 1.999 (the clip-space scaling is not applied to Z).
    TestHitAccumulator<ViewHit> accumulator;
    const escher::ray4 ray = HitRay(1, 1.5f);
    layer_stack()->HitTest(ray, &accumulator);
    ASSERT_FALSE(accumulator.hits().empty());

    const ViewHit& hit = accumulator.hits().front();
    EXPECT_EQ(hit.view->global_id(), GlobalId(1, kViewId));
    // TODO(38389): .999f (ray origin is currently 1 length behind the camera)
    EXPECT_NEAR(hit.distance, 1.999f, std::numeric_limits<float>::epsilon());
    const glm::vec4 view = hit.transform * ray.At(hit.distance);
    static const glm::vec4 expected = {5, 7.5f / 3, -1, 1};
    // We need to use 1000 * epsilon as the projection transform scales by 1000.
    static constexpr float epsilon = std::numeric_limits<float>::epsilon() * 1000;
    EXPECT_TRUE(glm::all(glm::epsilonEqual(view, expected, epsilon)))
        << "View hit coordinates " << glm::to_string(view) << " should be approximately "
        << glm::to_string(expected);
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

    sess.Apply(scenic::NewAddChildCmd(kSceneId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShapeNodeId));
  }

  // Perform two hit tests on either side of the display.
  {
    // First hit test should intersect the view's bounding box.
    TestHitAccumulator<ViewHit> accumulator;
    layer_stack()->HitTest(HitRay(5, layer_height() / 2), &accumulator);
    EXPECT_EQ(accumulator.hits().size(), 1u) << "Should see a hit on the rectangle";
  }
  {
    // Second hit test should completely miss the view's bounding box.
    TestHitAccumulator<ViewHit> accumulator;
    layer_stack()->HitTest(HitRay(layer_width() / 2 + 50, layer_height() / 2), &accumulator);
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

  {
    TestHitAccumulator<NodeHit> accumulator;
    HitTest(scene(), WorldSpaceHitRay(layer_width() / 2, layer_height() / 2), &accumulator);

    ASSERT_EQ(accumulator.hits().size(), 1u);
    EXPECT_EQ(accumulator.hits().front().node->id(), kHittableShapeNodeId);
  }
}

// TODO(40161): This is fragile but we don't want this to regress if we can help it before
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
    sess.Apply(scenic::NewSetTranslationCmd(kShape1Id, (float[]){4, 4.5f, -1}));

    sess.Apply(scenic::NewCreateShapeNodeCmd(kShape2Id));
    sess.Apply(scenic::NewSetShapeCmd(kShape2Id, kRectId));
    sess.Apply(scenic::NewSetMaterialCmd(kShape2Id, kMaterialId));
    sess.Apply(scenic::NewSetTranslationCmd(kShape2Id, (float[]){12, 4.5f, 1}));

    // Graph
    sess.Apply(scenic::NewAddChildCmd(kSceneId, kViewHolderId));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShape1Id));
    sess.Apply(scenic::NewAddChildCmd(kViewId, kShape2Id));
  }

  {
    TestHitAccumulator<ViewHit> accumulator;
    layer_stack()->HitTest(HitRay(4, 4.5f), &accumulator);
    EXPECT_FALSE(accumulator.hits().empty());
  }
  {
    TestHitAccumulator<ViewHit> accumulator;
    layer_stack()->HitTest(HitRay(12, 4.5f), &accumulator);
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
    const float bbox_min[3] = {(layer_width() - width) / 2, (layer_height() - height) / 2, -6};
    const float bbox_max[3] = {(layer_width() + width) / 2, (layer_height() + height) / 2, -4};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
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
    const float bbox_min2[3] = {0, 0, -9};
    const float bbox_max2[3] = {layer_width(), layer_height(), 0};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
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

  {
    TestHitAccumulator<NodeHit> accumulator;
    HitTest(scene(), WorldSpaceHitRay(layer_width() / 2, layer_height() / 2), &accumulator);
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
    const float bbox_min[3] = {0, 0, -9};
    const float bbox_max[3] = {layer_width() / 2, layer_height() / 2, 0};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
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
    const float bbox_min2[3] = {layer_width() / 2, layer_height() / 2, -9};
    const float bbox_max2[3] = {layer_width(), layer_height(), 0};
    const float inset_min[3] = {0, 0, 0};
    const float inset_max[3] = {0, 0, 0};
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
        kShapeNodeId, (float[3]){3.f * layer_width() / 4.f, 3.f * layer_height() / 4.f, -5.f}));
    sess2.Apply(scenic::NewAddChildCmd(kViewId2, kShapeNodeId));
  }

  {
    TestHitAccumulator<ViewHit> accumulator;
    layer_stack()->HitTest(HitRay(3 * layer_width() / 4, 3 * layer_height() / 4), &accumulator);
    EXPECT_TRUE(accumulator.hits().empty());
  }
}

// A comprehensive test that sets up a root session and two view sessions, with a ShapeNode in the
// root scene and in each View, and checks if both view hits are produced by the
// |SessionHitAccumulator|.
TEST_F(MultiSessionHitTestTest, GlobalHits) {
  // Create our tokens for View/ViewHolder creation.
  auto [view_token_1, view_holder_token_1] = scenic::ViewTokenPair::New();
  auto [view_token_2, view_holder_token_2] = scenic::ViewTokenPair::New();

  // Create bounds for the views.
  const float bbox_min[3] = {0, 0, -4};
  const float bbox_max[3] = {10, 10, 0};
  const float inset_min[3] = {0, 0, 0};
  const float inset_max[3] = {0, 0, 0};

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
    s_r.Apply(scenic::NewSetTranslationCmd(kShapeNodeId, (float[3]){4.f, 4.f, /*z*/ -1.f}));

    const uint32_t kShapeId = 2004;
    s_r.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_r.Apply(scenic::NewSetShapeCmd(kShapeNodeId, kShapeId));
  }

  // Two sessions (s_1 and s_2) create an overlapping and hittable surface.
  const uint32_t kViewId1 = 2001;
  CustomSession s_1(2, engine()->session_context());
  {
    s_1.Apply(scenic::NewCreateViewCmd(kViewId1, std::move(view_token_1), "view_1"));

    const uint32_t kRootNodeId = 2002;
    s_1.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_1.Apply(scenic::NewAddChildCmd(kViewId1, kRootNodeId));

    const uint32_t kShapeNodeId = 2003;
    s_1.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId));
    s_1.Apply(scenic::NewAddChildCmd(kRootNodeId, kShapeNodeId));
    s_1.Apply(scenic::NewSetTranslationCmd(kShapeNodeId, (float[3]){4.f, 4.f, /*z*/ -2.f}));

    const uint32_t kShapeId = 2004;  // Hit
    s_1.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_1.Apply(scenic::NewSetShapeCmd(kShapeNodeId, kShapeId));
  }

  const uint32_t kViewId2 = 3001;
  CustomSession s_2(3, engine()->session_context());
  {
    s_2.Apply(scenic::NewCreateViewCmd(kViewId2, std::move(view_token_2), "view_2"));

    const uint32_t kRootNodeId = 3002;
    s_2.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_2.Apply(scenic::NewAddChildCmd(kViewId2, kRootNodeId));

    const uint32_t kShapeNodeId = 3003;
    s_2.Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId));
    s_2.Apply(scenic::NewAddChildCmd(kRootNodeId, kShapeNodeId));
    s_2.Apply(scenic::NewSetTranslationCmd(kShapeNodeId, (float[3]){4.f, 4.f, /*z*/ -3.f}));

    const uint32_t kShapeId = 3004;  // Hit
    s_2.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_2.Apply(scenic::NewSetShapeCmd(kShapeNodeId, kShapeId));
  }

  {
    SessionHitAccumulator accumulator;
    layer_stack()->HitTest(HitRay(4, 4), &accumulator);

    const auto& hits = accumulator.hits();

    // All that for this!
    ASSERT_EQ(hits.size(), 2u) << "Should see two hits across two view sessions.";
    EXPECT_EQ(hits[0].view->id(), kViewId2);
    EXPECT_EQ(hits[1].view->id(), kViewId1);
  }
}

}  // namespace
}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
