// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/composer/session_helpers.h"
#include "apps/mozart/src/composer/resources/nodes/tag_node.h"
#include "apps/mozart/src/composer/tests/session_test.h"
#include "gtest/gtest.h"

namespace mozart {
namespace composer {
namespace test {

using TagTest = SessionTest;

TEST_F(TagTest, TagCreation) {
  ResourceId resource_id = 1;
  int32_t tag_value = 999;
  ASSERT_TRUE(Apply(NewCreateTagNodeOp(resource_id, tag_value)));
  auto tag_node = FindResource<TagNode>(resource_id);
  ASSERT_TRUE(tag_node);
  ASSERT_EQ(tag_node->tag(), tag_value);
}

TEST_F(TagTest, SimpleHierarchyCreation) {
  // Create a tag node.
  ASSERT_TRUE(Apply(NewCreateTagNodeOp(1 /* id */, 1 /* tag */)));

  // Create an entity node.
  ASSERT_TRUE(Apply(NewCreateEntityNodeOp(2 /* id */)));

  // Create a shape node.
  ASSERT_TRUE(Apply(NewCreateShapeNodeOp(3 /* id */)));
  ASSERT_TRUE(Apply(NewCreateCircleOp(4 /* id */, 100.0f /* radius */)));
  ASSERT_TRUE(Apply(NewSetShapeOp(3 /* shape node id */, 4 /* shape */)));

  // Setup hierarchy.
  ASSERT_TRUE(Apply(NewAddChildOp(1 /* tag */, 2 /* entity */)));
  ASSERT_TRUE(Apply(NewAddChildOp(2 /* entity */, 3 /* shape */)));

  // 3 nodes + 1 shape.
  ASSERT_EQ(session_->GetMappedResourceCount(), 4u);
}

TEST_F(TagTest, SimpleHitTestOnCircle) {
  // Create a tag node.
  ASSERT_TRUE(Apply(NewCreateTagNodeOp(1 /* id */, 1 /* tag */)));

  // Create a shape node.
  ASSERT_TRUE(Apply(NewCreateShapeNodeOp(2 /* id */)));
  ASSERT_TRUE(Apply(NewCreateCircleOp(3 /* id */, 100.0f /* radius */)));
  ASSERT_TRUE(Apply(NewSetShapeOp(2 /* shape node id */, 3 /* shape */)));

  // Setup hierarchy.
  ASSERT_TRUE(Apply(NewAddChildOp(1 /* tag */, 2 /* shape */)));

  ASSERT_EQ(session_->GetMappedResourceCount(), 3u);

  // Get the root node.
  auto root = FindResource<TagNode>(1);
  ASSERT_TRUE(root);

  {
    // Value outside shape.
    escher::vec2 point;
    point.x = point.y = INT_MAX;
    auto hit_nodes = root->HitTest(point);
    ASSERT_EQ(hit_nodes.size(), 0u);
  }

  {
    // Value inside shape.
    escher::vec2 point;
    point.x = 49.0;
    point.y = 51.0f;
    auto hit_nodes = root->HitTest(point);
    ASSERT_EQ(hit_nodes.size(), 1u);
    ASSERT_EQ(hit_nodes[0].node, 1u /* node id of tag node */);
    ASSERT_FLOAT_EQ(hit_nodes[0].point.x, 49.0f /* point in tag space */);
    ASSERT_FLOAT_EQ(hit_nodes[0].point.y, 51.0f /* point in tag space */);
  }
}

TEST_F(TagTest, MultipleTagNodesReturnLastTagNodeInHierarchy) {
  // Create a tag node.
  ASSERT_TRUE(Apply(NewCreateTagNodeOp(1 /* id */, 1 /* tag */)));

  // Create another tag node.
  ASSERT_TRUE(Apply(NewCreateTagNodeOp(100 /* id */, 100 /* tag */)));

  // Create a shape node.
  ASSERT_TRUE(Apply(NewCreateShapeNodeOp(2 /* id */)));
  ASSERT_TRUE(Apply(NewCreateCircleOp(3 /* id */, 100.0f /* radius */)));
  ASSERT_TRUE(Apply(NewSetShapeOp(2 /* shape node id */, 3 /* shape */)));

  // Setup hierarchy.
  ASSERT_TRUE(Apply(NewAddChildOp(1 /* tag */, 100 /* tag */)));
  ASSERT_TRUE(Apply(NewAddChildOp(100 /* tag */, 2 /* shape */)));

  ASSERT_EQ(session_->GetMappedResourceCount(), 4u);

  // Get the root node.
  auto root = FindResource<TagNode>(1);
  ASSERT_TRUE(root);

  {
    // Value inside shape.
    escher::vec2 point;
    point.x = 49.0;
    point.y = 51.0f;
    auto hit_nodes = root->HitTest(point);
    ASSERT_EQ(hit_nodes.size(), 1u);
    // The bottom most tag node gets hit.
    ASSERT_EQ(hit_nodes[0].node, 100u /* node id of tag node */);
    ASSERT_FLOAT_EQ(hit_nodes[0].point.x, 49.0f /* point in tag space */);
    ASSERT_FLOAT_EQ(hit_nodes[0].point.y, 51.0f /* point in tag space */);
  }
}

TEST_F(TagTest, TagNodeWithOverlappingShapes) {
  // Create a tag node.
  ASSERT_TRUE(Apply(NewCreateTagNodeOp(1 /* id */, 1 /* tag */)));

  // Create a shape node 1.
  ASSERT_TRUE(Apply(NewCreateShapeNodeOp(2 /* id */)));
  ASSERT_TRUE(Apply(NewCreateCircleOp(3 /* id */, 100.0f /* radius */)));
  ASSERT_TRUE(Apply(NewSetShapeOp(2 /* shape node id */, 3 /* shape */)));

  // Create a shape node 2.
  ASSERT_TRUE(Apply(NewCreateShapeNodeOp(4 /* id */)));
  ASSERT_TRUE(Apply(NewCreateCircleOp(5 /* id */, 100.0f /* radius */)));
  ASSERT_TRUE(Apply(NewSetShapeOp(4 /* shape node id */, 5 /* shape */)));

  // Setup hierarchy.
  ASSERT_TRUE(Apply(NewAddChildOp(1 /* tag */, 2 /* shape */)));
  ASSERT_TRUE(Apply(NewAddChildOp(1 /* tag */, 4 /* shape */)));

  ASSERT_EQ(session_->GetMappedResourceCount(), 5u);

  // Get the root node.
  auto root = FindResource<TagNode>(1);
  ASSERT_TRUE(root);

  {
    // Value inside shape.
    escher::vec2 point;
    point.x = 49.0;
    point.y = 51.0f;
    auto hit_nodes = root->HitTest(point);
    ASSERT_EQ(hit_nodes.size(), 1u);
    ASSERT_EQ(hit_nodes[0].node, 1u /* node id of tag node */);
    ASSERT_FLOAT_EQ(hit_nodes[0].point.x, 49.0f /* point in tag space */);
    ASSERT_FLOAT_EQ(hit_nodes[0].point.y, 51.0f /* point in tag space */);
  }
}

TEST_F(TagTest, OverlappingTagNodesShowUpInResults) {
  // Create a tag nodes.
  ASSERT_TRUE(Apply(NewCreateTagNodeOp(1 /* id */, 1 /* tag */)));
  ASSERT_TRUE(Apply(NewCreateTagNodeOp(2 /* id */, 2 /* tag */)));
  ASSERT_TRUE(Apply(NewCreateTagNodeOp(3 /* id */, 3 /* tag */)));

  // Create a shape node 1.
  ASSERT_TRUE(Apply(NewCreateShapeNodeOp(4 /* id */)));
  ASSERT_TRUE(Apply(NewCreateCircleOp(5 /* id */, 100.0f /* radius */)));
  ASSERT_TRUE(Apply(NewSetShapeOp(4 /* shape node id */, 5 /* shape */)));

  // Create a shape node 2.
  ASSERT_TRUE(Apply(NewCreateShapeNodeOp(6 /* id */)));
  ASSERT_TRUE(Apply(NewCreateCircleOp(7 /* id */, 100.0f /* radius */)));
  ASSERT_TRUE(Apply(NewSetShapeOp(6 /* shape node id */, 7 /* shape */)));

  // Setup hierarchy.
  ASSERT_TRUE(Apply(NewAddChildOp(1 /* tag */, 2 /* tag */)));
  ASSERT_TRUE(Apply(NewAddChildOp(1 /* tag */, 3 /* tag */)));
  ASSERT_TRUE(Apply(NewAddChildOp(2 /* tag */, 4 /* shape */)));
  ASSERT_TRUE(Apply(NewAddChildOp(3 /* tag */, 6 /* shape */)));

  ASSERT_EQ(session_->GetMappedResourceCount(), 7u);

  // Get the root node.
  auto root = FindResource<TagNode>(1);
  ASSERT_TRUE(root);

  {
    // Value inside shape.
    escher::vec2 point;
    point.x = 49.0;
    point.y = 51.0f;
    auto hit_nodes = root->HitTest(point);
    ASSERT_EQ(hit_nodes.size(), 2u);
    ASSERT_EQ(hit_nodes[0].node, 2u /* node id of tag node */);
    ASSERT_EQ(hit_nodes[1].node, 3u /* node id of tag node */);
    ASSERT_FLOAT_EQ(hit_nodes[0].point.x, 49.0f /* point in tag space */);
    ASSERT_FLOAT_EQ(hit_nodes[0].point.y, 51.0f /* point in tag space */);
    ASSERT_FLOAT_EQ(hit_nodes[1].point.x, 49.0f /* point in tag space */);
    ASSERT_FLOAT_EQ(hit_nodes[1].point.y, 51.0f /* point in tag space */);
  }
}

}  // namespace test
}  // namespace composer
}  // namespace mozart
