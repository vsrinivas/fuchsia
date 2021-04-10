// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>

#include <gtest/gtest.h>

#include "lib/ui/scenic/cpp/commands.h"
#include "src/ui/lib/escher/geometry/intersection.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/entity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/view_node.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using NodeTest = SessionTest;

// Testing class to avoid having to setup a proper View and all the state that comes with it just to
// inject a bounding box.
class ViewNodeForTest : public ViewNode {
 public:
  ViewNodeForTest() : ViewNode(/*session=*/nullptr, /*session_id=*/1, fxl::WeakPtr<View>()) {}
  void SetBoundingBox(escher::vec3 min, escher::vec3 max) { bbox_ = escher::BoundingBox(min, max); }

 private:
  escher::BoundingBox GetBoundingBox() const override { return bbox_; }
  escher::BoundingBox bbox_;
};

TEST_F(NodeTest, ShapeNodeMaterialAndShape) {
  const ResourceId kNodeId = 1;
  const ResourceId kMaterialId = 2;
  const ResourceId kShapeId = 3;

  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(kNodeId)));
  EXPECT_TRUE(Apply(scenic::NewCreateMaterialCmd(kMaterialId)));
  EXPECT_TRUE(Apply(scenic::NewSetTextureCmd(kMaterialId, 0)));
  EXPECT_TRUE(Apply(scenic::NewSetColorCmd(kMaterialId, 255, 100, 100, 255)));
  EXPECT_TRUE(Apply(scenic::NewCreateCircleCmd(kShapeId, 50.f)));
  EXPECT_TRUE(Apply(scenic::NewSetMaterialCmd(kNodeId, kMaterialId)));
  EXPECT_TRUE(Apply(scenic::NewSetShapeCmd(kNodeId, kShapeId)));
  auto shape_node = FindResource<ShapeNode>(kNodeId);
  auto material = FindResource<Material>(kMaterialId);
  auto circle = FindResource<Shape>(kShapeId);
  ASSERT_NE(nullptr, shape_node.get());
  ASSERT_NE(nullptr, material.get());
  ASSERT_NE(nullptr, circle.get());
  EXPECT_EQ(shape_node->material(), material);
  EXPECT_EQ(shape_node->shape(), circle);
}

TEST_F(NodeTest, InvalidFloatVector) {
  const ResourceId kNodeId = 1;

  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(kNodeId)));
  // Valid values.
  EXPECT_TRUE(Apply(scenic::NewSetTranslationCmd(kNodeId, {1.f, 2.f, 3.f})));
  EXPECT_TRUE(Apply(scenic::NewSetScaleCmd(kNodeId, {1.f, 1.f, 1.f})));
  EXPECT_TRUE(Apply(scenic::NewSetAnchorCmd(kNodeId, {4.f, 5.f, 6.f})));
  constexpr float PI = 3.14159;
  EXPECT_TRUE(Apply(
      scenic::NewSetRotationCmd(kNodeId, {0.f, 0.f, std::sin(PI / 2.f), std::cos(PI / 2.f)})));
  // Invalid values.
  EXPECT_FALSE(Apply(scenic::NewSetTranslationCmd(kNodeId, {INFINITY, 0.f, 0.f})));
  EXPECT_FALSE(Apply(scenic::NewSetTranslationCmd(kNodeId, {NAN, 0.f, 0.f})));
  EXPECT_FALSE(Apply(scenic::NewSetAnchorCmd(kNodeId, {INFINITY, 0.f, 0.f})));
  EXPECT_FALSE(Apply(scenic::NewSetAnchorCmd(kNodeId, {NAN, 0.f, 0.f})));
  EXPECT_FALSE(Apply(scenic::NewSetRotationCmd(kNodeId, {0.f, 0.f, 0.f, 2.f})));
  EXPECT_FALSE(Apply(scenic::NewSetRotationCmd(kNodeId, {0.f, 0.f, INFINITY, 1.f})));
  EXPECT_FALSE(Apply(scenic::NewSetScaleCmd(kNodeId, {1.f, 1.f, INFINITY})));
  EXPECT_FALSE(Apply(scenic::NewSetScaleCmd(kNodeId, {1.f, 1.f, NAN})));
  EXPECT_FALSE(Apply(scenic::NewSetScaleCmd(kNodeId, {1.f, 0.f, 1.f})));

  auto shape_node = FindResource<ShapeNode>(kNodeId);
  ASSERT_NE(nullptr, shape_node.get());
  EXPECT_EQ(shape_node->translation(), escher::vec3(1.f, 2.f, 3.f));
  EXPECT_EQ(shape_node->scale(), escher::vec3(1.f, 1.f, 1.f));
  EXPECT_EQ(shape_node->anchor(), escher::vec3(4.f, 5.f, 6.f));
  // fuchsia::ui::gfx::Quaternion {x, y, z, w} corresponds to escher::quat
  // {w, x, y, z}.
  EXPECT_EQ(shape_node->rotation(), escher::quat(std::cos(PI / 2.f), 0.f, 0.f, std::sin(PI / 2.f)));
}

TEST_F(NodeTest, NodesWithChildren) {
  // Child node that we will attach to various types of nodes.
  const ResourceId kChildNodeId = 1;
  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(kChildNodeId)));
  auto child_node = FindResource<Node>(kChildNodeId);

  // OK to detach a child that hasn't been attached.
  EXPECT_TRUE(Apply(scenic::NewDetachCmd(kChildNodeId)));

  const ResourceId kEntityNodeId = 10;
  const ResourceId kShapeNodeId = 11;
  // TODO: const ResourceId kClipNodeId = 12;
  EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(kEntityNodeId)));
  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId)));
  // TODO:
  // EXPECT_TRUE(Apply(scenic::NewCreateClipNodeCmd(kClipNodeId)));
  auto entity_node = FindResource<EntityNode>(kEntityNodeId);
  auto shape_node = FindResource<ShapeNode>(kShapeNodeId);
  // auto clip_node = FindResource<ClipNode>(kClipNodeId);

  // We expect to be able to add children to these types.
  EXPECT_TRUE(Apply(scenic::NewAddChildCmd(kEntityNodeId, kChildNodeId)));
  EXPECT_EQ(entity_node.get(), child_node->parent());
  EXPECT_TRUE(Apply(scenic::NewDetachCmd(kChildNodeId)));
  // EXPECT_TRUE(Apply(scenic::NewDetachCmd(kChildNodeId)));

  // We do not expect to be able to add children to these types.
  // TODO:
  // EXPECT_FALSE(Apply(scenic::NewAddChildCmd(kClipNodeId,
  // kChildNodeId))); EXPECT_EQ(nullptr, child_node->parent());
  // EXPECT_EQ(nullptr, child_node->parent());
  EXPECT_FALSE(Apply(scenic::NewAddChildCmd(kShapeNodeId, kChildNodeId)));
  EXPECT_EQ(nullptr, child_node->parent());
}

TEST_F(NodeTest, SettingHitTestBehavior) {
  const ResourceId kNodeId = 1;

  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(kNodeId)));

  auto shape_node = FindResource<ShapeNode>(kNodeId);
  EXPECT_EQ(::fuchsia::ui::gfx::HitTestBehavior::kDefault, shape_node->hit_test_behavior());

  EXPECT_TRUE(Apply(
      scenic::NewSetHitTestBehaviorCmd(kNodeId, ::fuchsia::ui::gfx::HitTestBehavior::kSuppress)));
  EXPECT_EQ(::fuchsia::ui::gfx::HitTestBehavior::kSuppress, shape_node->hit_test_behavior());
}

TEST_F(NodeTest, SettingClipPlanes) {
  const ResourceId kNodeId = 1;

  EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(kNodeId)));

  auto node = FindResource<EntityNode>(kNodeId);
  EXPECT_EQ(0U, node->clip_planes().size());

  std::vector<fuchsia::ui::gfx::Plane3> planes;
  planes.push_back({.dir = {.x = 1.f, .y = 0.f, .z = 0.f}, .dist = -1.f});
  planes.push_back({.dir = {.x = 0.f, .y = 1.f, .z = 0.f}, .dist = -2.f});
  EXPECT_TRUE(Apply(scenic::NewSetClipPlanesCmd(kNodeId, planes)));
  EXPECT_EQ(2U, node->clip_planes().size());

  // Setting clip planes replaces the previous ones.
  planes.push_back({.dir = {.x = 0.f, .y = 0.f, .z = 1.f}, .dist = -3.f});
  EXPECT_TRUE(Apply(scenic::NewSetClipPlanesCmd(kNodeId, planes)));
  EXPECT_EQ(3U, node->clip_planes().size());

  // Verify the the planes have the values set by the Cmd.
  for (size_t i = 0; i < planes.size(); ++i) {
    EXPECT_EQ(planes[i].dir.x, node->clip_planes()[i].dir().x);
    EXPECT_EQ(planes[i].dir.y, node->clip_planes()[i].dir().y);
    EXPECT_EQ(planes[i].dir.z, node->clip_planes()[i].dir().z);
    EXPECT_EQ(planes[i].dist, node->clip_planes()[i].dist());
  }

  // Clear clip planes by setting empty vector of planes.
  EXPECT_TRUE(Apply(scenic::NewSetClipPlanesCmd(kNodeId, {})));
  EXPECT_EQ(0U, node->clip_planes().size());
}

TEST(ViewNodeTest, GetIntersection_MissOnBoundingBoxByRay) {
  auto view_node = fxl::MakeRefCounted<ViewNodeForTest>();
  view_node->SetBoundingBox({0, 0, 20}, {100, 100, 100});

  // Ray outside bounding box, interval has Z-dimension overlap with box.
  escher::ray4 ray{.origin = {1000, 0, 0, 1}, .direction = {0, 0, 1, 0}};
  Node::IntersectionInfo parent_intersection;
  parent_intersection.interval = {0.f, 1000000.0f};

  Node::IntersectionInfo result = view_node->GetIntersection(ray, parent_intersection);
  EXPECT_FALSE(result.did_hit);
  EXPECT_FALSE(result.continue_with_children);
  EXPECT_TRUE(result.interval.is_empty());
}

TEST(ViewNodeTest, GetIntersection_MissOnBoundingBoxByInterval) {
  auto view_node = fxl::MakeRefCounted<ViewNodeForTest>();
  view_node->SetBoundingBox({0, 0, 20}, {100, 100, 100});

  // Ray outside bounding box, interval does not overlap with box.
  escher::ray4 ray{.origin = {50, 50, 0, 1}, .direction = {0, 0, 1, 0}};
  Node::IntersectionInfo parent_intersection;
  parent_intersection.interval = {1000, 5000};

  Node::IntersectionInfo result = view_node->GetIntersection(ray, parent_intersection);
  EXPECT_FALSE(result.did_hit);
  EXPECT_FALSE(result.continue_with_children);
  EXPECT_TRUE(result.interval.is_empty());
}

TEST(ViewNodeTest, GetIntersection_HitOnBoundingBox) {
  auto view_node = fxl::MakeRefCounted<ViewNodeForTest>();
  view_node->SetBoundingBox({0, 0, 20}, {100, 100, 100});

  // Ray intersects bounding box, interval has Z-dimension overlap with box.
  escher::ray4 ray{.origin = {50, 50, 0, 1}, .direction = {0, 0, 1, 0}};
  Node::IntersectionInfo parent_intersection;
  parent_intersection.interval = {0.f, 1000000.0f};

  Node::IntersectionInfo result = view_node->GetIntersection(ray, parent_intersection);

  // Should still not register as a hit, but should tell us to continue with its children.
  EXPECT_FALSE(result.did_hit);
  EXPECT_TRUE(result.continue_with_children);
  EXPECT_EQ(result.interval.min(), 20.0f - escher::kIntersectionEpsilon);
  EXPECT_EQ(result.interval.max(), 100.0f + escher::kIntersectionEpsilon);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
