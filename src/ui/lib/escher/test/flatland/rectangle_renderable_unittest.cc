// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flatland/rectangle_renderable.h"

#include <gtest/gtest.h>

#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/flatland/flatland_static_config.h"
#include "src/ui/lib/escher/flatland/rectangle_compositor.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/renderer/render_funcs.h"
#include "src/ui/lib/escher/resources/resource.h"
#include "src/ui/lib/escher/resources/resource_manager.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/common/readback_test.h"
#include "src/ui/lib/escher/types/color.h"
#include "src/ui/lib/escher/types/color_histogram.h"
#include "src/ui/lib/escher/vk/texture.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

namespace escher {

namespace {

static const float kDegreesToRadians = glm::pi<float>() / 180.f;

// For these unit tests we only care about the matrices, so use this
// wrapper function to simplify construction.
RectangleRenderable CreateRenderable(const glm::mat4& matrix) {
  return RectangleRenderable::Create(
      matrix, {glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1)}, nullptr,
      glm::vec4(1.f), false);
}

// Helper function for ensuring that two vectors are equal while taking into
// account floating point discrepancies via an epsilon term.
bool Equal(const glm::vec2 a, const glm::vec2 b) {
  return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec2(0.001f)));
}

}  // anonymous namespace

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

TEST(RectangleRenderableTest, ScaleAndRotate90DegreesTest) {
  glm::vec2 extent(100, 50);
  glm::mat3 matrix = glm::rotate(glm::mat3(), 90.f * kDegreesToRadians);
  matrix = glm::scale(matrix, extent);

  glm::vec2 v0 = matrix * glm::vec3(0, 0, 1);
  glm::vec2 v1 = matrix * glm::vec3(1, 0, 1);
  glm::vec2 v2 = matrix * glm::vec3(1, -1, 1);
  glm::vec2 v3 = matrix * glm::vec3(0, -1, 1);

  auto renderable = CreateRenderable(matrix);
  EXPECT_TRUE(Equal(renderable.dest.origin, glm::vec2(0, 100)));
  EXPECT_TRUE(Equal(renderable.dest.extent, glm::vec2(50, 100)));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[0], vec2(1, 0));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[1], vec2(1, 1));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[2], vec2(0, 1));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[3], vec2(0, 0));
}

TEST(RectangleRenderableTest, ScaleAndRotate180DegreesTest) {
  glm::vec2 extent(100, 50);
  glm::mat3 matrix = glm::rotate(glm::mat3(), 180.f * kDegreesToRadians);
  matrix = glm::scale(matrix, extent);

  auto renderable = CreateRenderable(matrix);
  EXPECT_TRUE(Equal(renderable.dest.origin, glm::vec2(-100, 50)));
  EXPECT_TRUE(Equal(renderable.dest.extent, glm::vec2(100, 50)));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[0], vec2(1, 1));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[1], vec2(0, 1));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[2], vec2(0, 0));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[3], vec2(1, 0));
}

TEST(RectangleRenderableTest, ScaleAndRotate270DegreesTest) {
  glm::vec2 extent(100, 50);
  glm::mat3 matrix = glm::rotate(glm::mat3(), 270.f * kDegreesToRadians);
  matrix = glm::scale(matrix, extent);

  auto renderable = CreateRenderable(matrix);
  EXPECT_TRUE(Equal(renderable.dest.origin, glm::vec2(-50, 0)));
  EXPECT_TRUE(Equal(renderable.dest.extent, glm::vec2(50, 100)));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[0], vec2(0, 1));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[1], vec2(0, 0));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[2], vec2(1, 0));
  EXPECT_EQ(renderable.source.uv_coordinates_clockwise[3], vec2(1, 1));
}

// Make sure that floating point transform values that aren't exactly
// integers are also respected.
TEST(RectangleRenderableTest, FloatingPointTranslateAndScaleTest) {
  glm::vec2 offset(10.9, 20.5);
  glm::vec2 extent(100.3, 200.7);
  glm::mat3 matrix = glm::translate(glm::mat3(), offset);
  matrix = glm::scale(matrix, extent);

  auto renderable = CreateRenderable(matrix);
  EXPECT_EQ(renderable.dest.origin, offset);
  EXPECT_EQ(renderable.dest.extent, extent);
}

TEST(RectangleRenderableTest, NegativeScaleTest) {
  // If both the x and y scale components are negative, this is equivalent
  // to a positive scale rotated by 180 degrees (PI radians).
  {
    glm::vec2 extent(-10, -5);
    glm::mat3 matrix = glm::scale(glm::mat3(), extent);
    auto renderable = CreateRenderable(matrix);
    EXPECT_EQ(renderable.dest.extent, glm::vec2(10, 5));

    // These are the expected UVs for a 180 degree rotation.
    EXPECT_EQ(renderable.source.uv_coordinates_clockwise[0], vec2(1, 1));
    EXPECT_EQ(renderable.source.uv_coordinates_clockwise[1], vec2(0, 1));
    EXPECT_EQ(renderable.source.uv_coordinates_clockwise[2], vec2(0, 0));
    EXPECT_EQ(renderable.source.uv_coordinates_clockwise[3], vec2(1, 0));
  }

  // If just the x scale component is negative and the y component is positive,
  // this is equivalent to a flip about the y axis (horiziontal).
  {
    glm::vec2 extent(-10, 5);
    glm::mat3 matrix = glm::scale(glm::mat3(), extent);
    auto renderable = CreateRenderable(matrix);
    EXPECT_TRUE(Equal(renderable.dest.origin, glm::vec2(-10, 0)));
    EXPECT_TRUE(Equal(renderable.dest.extent, glm::vec2(10, 5)));

    // These are the expected UVs for a horizontal flip.
    EXPECT_EQ(renderable.source.uv_coordinates_clockwise[0], vec2(1, 0));
    EXPECT_EQ(renderable.source.uv_coordinates_clockwise[1], vec2(0, 0));
    EXPECT_EQ(renderable.source.uv_coordinates_clockwise[2], vec2(0, 1));
    EXPECT_EQ(renderable.source.uv_coordinates_clockwise[3], vec2(1, 1));
  }

  // If just the y scale component is negative and the x component is positive,
  // this is equivalent to a vertical flip about the x axis.
  {
    glm::vec2 extent(10, -5);
    glm::mat3 matrix = glm::scale(glm::mat3(), extent);
    auto renderable = CreateRenderable(matrix);
    EXPECT_TRUE(Equal(renderable.dest.origin, glm::vec2(0, 5)));
    EXPECT_TRUE(Equal(renderable.dest.extent, glm::vec2(10, 5)));

    // These are the expected UVs for a vertical flip.
    EXPECT_EQ(renderable.source.uv_coordinates_clockwise[0], vec2(0, 1));
    EXPECT_EQ(renderable.source.uv_coordinates_clockwise[1], vec2(1, 1));
    EXPECT_EQ(renderable.source.uv_coordinates_clockwise[2], vec2(1, 0));
    EXPECT_EQ(renderable.source.uv_coordinates_clockwise[3], vec2(0, 0));
  }
}

// The same operations of translate/rotate/scale on a single matrix.
TEST(RectangleRenderableTest, OrderOfOperationsTest) {
  // First subtest tests swapping scaling and translation.
  {
    // Here we scale and then translate. The origin should be at (10,5) and the extent should also
    // still be (2,2) since the scale is being applied on the untranslated coordinates.
    glm::mat3 test_1 = glm::scale(glm::translate(glm::mat3(), glm::vec2(10, 5)), glm::vec2(2, 2));
    auto renderable_1 = CreateRenderable(test_1);
    EXPECT_TRUE(Equal(renderable_1.dest.origin, glm::vec2(10, 5)));
    EXPECT_TRUE(Equal(renderable_1.dest.extent, glm::vec2(2, 2)));

    // Here we translate first, and then scale the translation, resulting in the origin point
    // doubling from (10, 5) to (20, 10).
    glm::mat3 test_2 = glm::translate(glm::scale(glm::mat3(), glm::vec2(2, 2)), glm::vec2(10, 5));
    auto renderable_2 = CreateRenderable(test_2);
    EXPECT_TRUE(Equal(renderable_2.dest.origin, glm::vec2(20, 10)));
    EXPECT_TRUE(Equal(renderable_2.dest.extent, glm::vec2(2, 2)));
  }

  {
    // Since the rotation is applied first, the origin point rotates around (0,0) and then we
    // translate and wind up at (10, 5).
    glm::mat3 test_1 =
        glm::rotate(glm::translate(glm::mat3(), glm::vec2(10, 5)), 90.f * kDegreesToRadians);
    auto renderable_1 = CreateRenderable(test_1);
    EXPECT_TRUE(Equal(renderable_1.dest.origin, glm::vec2(10, 6)));

    // Since we translated first here, the point goes from (0,0) to (10,5) and then rotates
    // 90 degrees counterclockwise and winds up at (-5, 10).
    glm::mat3 test_2 =
        glm::translate(glm::rotate(glm::mat3(), 90.f * kDegreesToRadians), glm::vec2(10, 5));
    auto renderable_2 = CreateRenderable(test_2);
    EXPECT_TRUE(Equal(renderable_2.dest.origin, glm::vec2(-5, 11)));
  }

  // Third subtest tests swapping non-uniform scaling and rotation.
  {
    // We rotate first and then scale, so the scaling isn't affected by the rotation.
    glm::mat3 test_1 =
        glm::rotate(glm::scale(glm::mat3(), glm::vec2(9, 7)), 90.f * kDegreesToRadians);
    auto renderable_1 = CreateRenderable(test_1);
    EXPECT_TRUE(Equal(renderable_1.dest.extent, glm::vec2(9, 7)));

    // Here we scale and then rotate so the scale winds up rotated.
    glm::mat3 test_2 =
        glm::scale(glm::rotate(glm::mat3(), 90.f * kDegreesToRadians), glm::vec2(9, 7));
    auto renderable_2 = CreateRenderable(test_2);
    EXPECT_TRUE(Equal(renderable_2.dest.extent, glm::vec2(7, 9)));
  }
}

}  // namespace escher
