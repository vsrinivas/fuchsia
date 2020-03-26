// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/shape/rounded_rect.h"

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/shape/mesh_spec.h"

namespace {
using namespace escher;

struct Vertex {
  vec2 pos;
  vec2 uv;
};

TEST(RoundedRect, Tessellation) {
  RoundedRectSpec rect_spec(100, 500, 20, 20, 20, 20);
  MeshSpec mesh_spec{MeshAttribute::kPosition2D | MeshAttribute::kUV};
  ASSERT_EQ(sizeof(Vertex), mesh_spec.stride(0));

  auto counts = GetRoundedRectMeshVertexAndIndexCounts(rect_spec);
  const uint32_t vertex_count = counts.first;
  const uint32_t index_count = counts.second;

  std::vector<Vertex> vertices;
  vertices.resize(vertex_count);
  GenerateRoundedRectVertices(rect_spec, mesh_spec, vertices.data(), vertex_count * sizeof(Vertex));

  std::vector<uint32_t> indices;
  indices.resize(index_count);
  GenerateRoundedRectIndices(rect_spec, mesh_spec, indices.data(), index_count * sizeof(uint32_t));

  // Guarantee that all vertices are referenced by index at least once, and no
  // non-existent vertices are referenced.
  {
    std::set<uint32_t> vertex_indices;
    uint32_t lowest_index = indices[0];
    uint32_t highest_index = indices[0];
    for (auto i : indices) {
      vertex_indices.insert(i);
      lowest_index = i < lowest_index ? i : lowest_index;
      highest_index = i > highest_index ? i : highest_index;
    }
    EXPECT_EQ(0U, lowest_index);
    EXPECT_EQ(vertex_count - 1, highest_index);
    EXPECT_EQ(vertex_count, vertex_indices.size());
  }
}

TEST(RoundedRect, HitTesting) {
  {
    // Degenerate rounded-rect: corner radii are zero.
    RoundedRectSpec spec(100, 100, 0, 0, 0, 0);

    // Test points completely outside of the rectangle.
    EXPECT_FALSE(spec.ContainsPoint(vec2(51, 51)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(51, -51)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(-51, -51)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(-51, 51)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(0, -51)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(0, 51)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(51, 0)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(-51, 0)));

    // Test corners.
    EXPECT_TRUE(spec.ContainsPoint(vec2(50, 50)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(50, -50)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(-50, -50)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(-50, 50)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(49, 49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(49, -49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(-49, -49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(-49, 49)));
  }

  {
    // Increase corner radii by 1.
    RoundedRectSpec spec(100, 100, 1, 1, 1, 1);
    EXPECT_FALSE(spec.ContainsPoint(vec2(50, 50)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(50, -50)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(-50, -50)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(-50, 50)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(49, 49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(49, -49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(-49, -49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(-49, 49)));
  }

  {
    // Increase corner radii by 1, again.
    RoundedRectSpec spec(100, 100, 2, 2, 2, 2);
    EXPECT_TRUE(spec.ContainsPoint(vec2(49, 49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(49, -49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(-49, -49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(-49, 49)));
  }

  {
    // Increase corner radii by 1, again.
    RoundedRectSpec spec(100, 100, 3, 3, 3, 3);
    EXPECT_TRUE(spec.ContainsPoint(vec2(49, 49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(49, -49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(-49, -49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(-49, 49)));
  }

  {
    // Increase corner radii by 1, again.
    RoundedRectSpec spec(100, 100, 4, 4, 4, 4);
    EXPECT_FALSE(spec.ContainsPoint(vec2(49, 49)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(49, -49)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(-49, -49)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(-49, 49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(48, 48)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(48, -48)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(-48, -48)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(-48, 48)));
  }

  {
    // Test with smaller top-left corner radius.
    RoundedRectSpec spec(100, 100, 2, 4, 4, 4);
    EXPECT_FALSE(spec.ContainsPoint(vec2(49, 49)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(49, -49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(-49, -49)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(-49, 49)));
  }

  {
    // Test with smaller top-right corner radius.
    RoundedRectSpec spec(100, 100, 4, 2, 4, 4);
    EXPECT_FALSE(spec.ContainsPoint(vec2(49, 49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(49, -49)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(-49, -49)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(-49, 49)));
  }

  {
    // Test with smaller bottom-right corner radius.
    RoundedRectSpec spec(100, 100, 4, 4, 2, 4);
    EXPECT_TRUE(spec.ContainsPoint(vec2(49, 49)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(49, -49)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(-49, -49)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(-49, 49)));
  }

  {
    // Test with smaller bottom-left corner radius.
    RoundedRectSpec spec(100, 100, 4, 4, 4, 2);
    EXPECT_FALSE(spec.ContainsPoint(vec2(49, 49)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(49, -49)));
    EXPECT_FALSE(spec.ContainsPoint(vec2(-49, -49)));
    EXPECT_TRUE(spec.ContainsPoint(vec2(-49, 49)));
  }
}

}  // namespace
