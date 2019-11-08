// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/ui/scenic/lib/gfx/engine/hit.h"
#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"
#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"

namespace {

using namespace scenic_impl;
using namespace gfx;

// Empty accumulator should report no collisions.
TEST(CollisionAccumulatorTest, Empty) { EXPECT_TRUE(CollisionAccumulator().Report().empty()); }

std::vector<fxl::RefPtr<ShapeNode>> SetUpFakeNodes() {
  std::vector<fxl::RefPtr<ShapeNode>> nodes;
  static constexpr size_t num_nodes = 6;
  nodes.reserve(num_nodes);
  for (size_t i = 0; i < num_nodes; ++i) {
    nodes.push_back(fxl::MakeRefCounted<ShapeNode>(/*session=*/nullptr,
                                                   /*session_id=*/num_nodes - i,
                                                   /*node_id=*/i));
  }
  return nodes;
}

// Nodes at same distance should collide.
TEST(CollisionAccumulatorTest, Warning) {
  auto nodes = SetUpFakeNodes();

  CollisionAccumulator accumulator;

  static constexpr float distance1 = 100.0f, distance2 = 200.0f, distance3 = 300.0f;
  accumulator.Add({.node = nodes[0].get(), .distance = distance1});
  accumulator.Add({.node = nodes[1].get(), .distance = distance1});
  accumulator.Add({.node = nodes[2].get(), .distance = distance1});
  accumulator.Add({.node = nodes[3].get(), .distance = distance2});
  accumulator.Add({.node = nodes[4].get(), .distance = distance3});
  accumulator.Add({.node = nodes[5].get(), .distance = distance3});

  EXPECT_THAT(accumulator.Report(),
              testing::UnorderedElementsAre(
                  testing::UnorderedElementsAre(GlobalId{6, 0}, GlobalId{5, 1}, GlobalId{4, 2}),
                  testing::UnorderedElementsAre(GlobalId{2, 4}, GlobalId{1, 5})));
}

// Nodes at different distances should have no collisions.
TEST(CollisionAccumulatorTest, NoCollisions) {
  const auto nodes = SetUpFakeNodes();

  CollisionAccumulator accumulator;
  for (size_t i = 0; i < nodes.size(); ++i) {
    accumulator.Add({.node = nodes[i].get(), .distance = 100 + i * 1.0f});
  }

  EXPECT_TRUE(accumulator.Report().empty());
}

}  // namespace
