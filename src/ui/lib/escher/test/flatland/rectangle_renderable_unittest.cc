// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flatland/rectangle_renderable.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/flatland/flatland_static_config.h"
#include "src/ui/lib/escher/flatland/rectangle_compositor.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/renderer/render_funcs.h"
#include "src/ui/lib/escher/resources/resource.h"
#include "src/ui/lib/escher/resources/resource_manager.h"
#include "src/ui/lib/escher/test/fixtures/readback_test.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/types/color.h"
#include "src/ui/lib/escher/types/color_histogram.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace escher {

TEST(RectangleRenderableTest, ValidityTest) {
  // A default renderable with no texture is invalid.
  RectangleRenderable renderable;
  EXPECT_FALSE(RectangleRenderable::IsValid(renderable));

  // Is valid except for texture.
  EXPECT_TRUE(RectangleRenderable::IsValid(renderable, true));

  // Check each of the color components: they should fail if above 1 or less than 0.
  for (uint32_t i = 0; i < 4; i++) {
    renderable.color[i] = 1.5;
    EXPECT_FALSE(RectangleRenderable::IsValid(renderable, true));
    renderable.color[i] = -0.5;
    EXPECT_FALSE(RectangleRenderable::IsValid(renderable, true));
    renderable.color = vec4(1.f);
  }
  // Should be valid again here, since we reset the color to vec4(1).
  EXPECT_TRUE(RectangleRenderable::IsValid(renderable, true));

  // Check to see that the rectangle is not valid if the uv coordinates are
  // outside of the range [0,1].
  for (uint32_t i = 0; i < 4; i++) {
    auto old_uv = renderable.source.uv_coordinates_clockwise[i];
    renderable.source.uv_coordinates_clockwise[i] = vec2(1.1);
    EXPECT_FALSE(RectangleRenderable::IsValid(renderable, true));
    renderable.source.uv_coordinates_clockwise[i] = vec2(-0.5);
    EXPECT_FALSE(RectangleRenderable::IsValid(renderable, true));
    renderable.source.uv_coordinates_clockwise[i] = old_uv;
  }
  // Should be valid again here since we reset the uv coordinates.
  EXPECT_TRUE(RectangleRenderable::IsValid(renderable, true));

  // The extent cannot be negative.
  renderable.dest.extent = vec2(-1, -1);
  EXPECT_FALSE(RectangleRenderable::IsValid(renderable, true));
  renderable.dest.extent = vec2(0);
  EXPECT_TRUE(RectangleRenderable::IsValid(renderable, true));
}

TEST(RectangleRenderableTest, Rotate90Test) {
  const vec2 kInitialExtent(100, 200);
  const vec2 kSwappedExtent(200, 100);
  RectangleRenderable renderable;
  renderable.dest.extent = kInitialExtent;
  RectangleRenderable::Rotate(&renderable, 90U);

  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[0], vec2(0, 1));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[1], vec2(0, 0));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[2], vec2(1, 0));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[3], vec2(1, 1));
  EXPECT_EQ(renderable.dest.extent, kSwappedExtent);
}

TEST(RectangleRenderableTest, Rotate180Test) {
  const vec2 kInitialExtent(100, 200);
  RectangleRenderable renderable;
  renderable.dest.extent = kInitialExtent;
  RectangleRenderable::Rotate(&renderable, 180U);

  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[0], vec2(1, 1));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[1], vec2(0, 1));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[2], vec2(0, 0));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[3], vec2(1, 0));
  EXPECT_EQ(renderable.dest.extent, kInitialExtent);
}

TEST(RectangleRenderableTest, Rotate270Test) {
  const vec2 kInitialExtent(100, 200);
  const vec2 kSwappedExtent(200, 100);
  RectangleRenderable renderable;
  renderable.dest.extent = kInitialExtent;
  RectangleRenderable::Rotate(&renderable, 270U);

  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[0], vec2(1, 0));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[1], vec2(1, 1));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[2], vec2(0, 1));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[3], vec2(0, 0));
  EXPECT_EQ(renderable.dest.extent, kSwappedExtent);
}

TEST(RectangleRenderableTest, FlipHorizontalTest) {
  const vec2 kInitialExtent(100, 200);

  RectangleRenderable renderable;
  renderable.dest.extent = kInitialExtent;
  RectangleRenderable::FlipHorizontally(&renderable);

  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[0], vec2(1, 0));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[1], vec2(0, 0));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[2], vec2(0, 1));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[3], vec2(1, 1));
  EXPECT_EQ(renderable.dest.extent, kInitialExtent);
}

TEST(RectangleRenderableTest, FlipVerticalTest) {
  const vec2 kInitialExtent(100, 200);
  RectangleRenderable renderable;
  renderable.dest.extent = kInitialExtent;
  RectangleRenderable::FlipVertically(&renderable);

  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[0], vec2(0, 1));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[1], vec2(1, 1));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[2], vec2(1, 0));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[3], vec2(0, 0));
  EXPECT_EQ(renderable.dest.extent, kInitialExtent);
}

}  // namespace escher
