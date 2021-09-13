// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

namespace flatland {
namespace test {

namespace {

// Helper function to generate an escher::Rectangle2D from a glm::mat3 for tests that are strictly
// testing the conversion math.
escher::Rectangle2D GetRectangleForMatrix(const glm::mat3& matrix) {
  // Compute the global rectangle vector and return the first entry.
  allocation::ImageMetadata image = {.width = 1, .height = 1};
  const auto rectangles = ComputeGlobalRectangles({matrix}, {ImageSampleRegion{0, 0, 1, 1}},
                                                  {kUnclippedRegion}, {image});
  EXPECT_EQ(rectangles.size(), 1ul);
  return rectangles[0];
}

// Helper function to generate an escher::Rectangle2D from a glm::mat3 for tests that are strictly
// testing the conversion math.
escher::Rectangle2D GetRectangleForMatrixAndClip(const glm::mat3& matrix,
                                                 const TransformClipRegion& clip) {
  // Compute the global rectangle vector and return the first entry.
  allocation::ImageMetadata image = {.width = 1, .height = 1};
  const auto rectangles =
      ComputeGlobalRectangles({matrix}, {ImageSampleRegion{0, 0, 1, 1}}, {clip}, {image});
  EXPECT_EQ(rectangles.size(), 1ul);
  return rectangles[0];
}

}  // namespace

// The following tests ensure the transform hierarchy is properly reflected in the list of global
// rectangles.

TEST(GlobalMatrixDataTest, EmptyTopologyReturnsEmptyMatrices) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::TopologyVector topology_vector;
  GlobalTopologyData::ParentIndexVector parent_indices;

  auto global_matrices = ComputeGlobalMatrices(topology_vector, parent_indices, uber_structs);
  EXPECT_TRUE(global_matrices.empty());
}

TEST(GlobalMatrixDataTest, EmptyLocalMatricesAreIdentity) {
  UberStruct::InstanceMap uber_structs;

  // Make a global topology representing the following graph:
  //
  // 1:0 - 1:1
  GlobalTopologyData::TopologyVector topology_vector = {{1, 0}, {1, 1}};
  GlobalTopologyData::ParentIndexVector parent_indices = {0, 0};

  // The UberStruct for instance ID 1 must exist, but it contains no local matrices.
  auto uber_struct = std::make_unique<UberStruct>();
  uber_structs[1] = std::move(uber_struct);

  // The root matrix is set to the identity matrix, and the second inherits that.
  std::vector<glm::mat3> expected_matrices = {
      glm::mat3(),
      glm::mat3(),
  };

  auto global_matrices = ComputeGlobalMatrices(topology_vector, parent_indices, uber_structs);
  EXPECT_THAT(global_matrices, ::testing::ElementsAreArray(expected_matrices));
}

TEST(GlobalMatrixDataTest, GlobalMatricesIncludeParentMatrix) {
  UberStruct::InstanceMap uber_structs;

  // Make a global topology representing the following graph:
  //
  // 1:0 - 1:1 - 1:2
  //     \
  //       1:3 - 1:4
  GlobalTopologyData::TopologyVector topology_vector = {{1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 4}};
  GlobalTopologyData::ParentIndexVector parent_indices = {0, 0, 1, 0, 3};

  auto uber_struct = std::make_unique<UberStruct>();

  static const glm::vec2 kTranslation = {1.f, 2.f};
  static const float kRotation = glm::half_pi<float>();
  static const glm::vec2 kScale = {3.f, 5.f};

  // All transforms will get the translation from 1:0
  uber_struct->local_matrices[{1, 0}] = glm::translate(glm::mat3(), kTranslation);

  // The 1:1 - 1:2 branch rotates, then scales.
  uber_struct->local_matrices[{1, 1}] = glm::rotate(glm::mat3(), kRotation);
  uber_struct->local_matrices[{1, 2}] = glm::scale(glm::mat3(), kScale);

  // The 1:3 - 1:4 branch scales, then rotates.
  uber_struct->local_matrices[{1, 3}] = glm::scale(glm::mat3(), kScale);
  uber_struct->local_matrices[{1, 4}] = glm::rotate(glm::mat3(), kRotation);

  uber_structs[1] = std::move(uber_struct);

  // The expected matrices apply the operations in the correct order. The translation always comes
  // first, followed by the operations of the children.
  std::vector<glm::mat3> expected_matrices = {
      glm::translate(glm::mat3(), kTranslation),
      glm::rotate(glm::translate(glm::mat3(), kTranslation), kRotation),
      glm::scale(glm::rotate(glm::translate(glm::mat3(), kTranslation), kRotation), kScale),
      glm::scale(glm::translate(glm::mat3(), kTranslation), kScale),
      glm::rotate(glm::scale(glm::translate(glm::mat3(), kTranslation), kScale), kRotation),
  };

  auto global_matrices = ComputeGlobalMatrices(topology_vector, parent_indices, uber_structs);
  EXPECT_THAT(global_matrices, ::testing::ElementsAreArray(expected_matrices));
}

TEST(GlobalMatrixDataTest, GlobalMatricesMultipleUberStructs) {
  UberStruct::InstanceMap uber_structs;

  // Make a global topology representing the following graph:
  //
  // 1:0 - 2:0
  //     \
  //       1:1
  GlobalTopologyData::TopologyVector topology_vector = {{1, 0}, {2, 0}, {1, 1}};
  GlobalTopologyData::ParentIndexVector parent_indices = {0, 0, 0};

  auto uber_struct1 = std::make_unique<UberStruct>();
  auto uber_struct2 = std::make_unique<UberStruct>();

  // Each matrix scales by a different prime number to distinguish the branches.
  uber_struct1->local_matrices[{1, 0}] = glm::scale(glm::mat3(), {2.f, 2.f});
  uber_struct1->local_matrices[{1, 1}] = glm::scale(glm::mat3(), {3.f, 3.f});

  uber_struct2->local_matrices[{2, 0}] = glm::scale(glm::mat3(), {5.f, 5.f});

  uber_structs[1] = std::move(uber_struct1);
  uber_structs[2] = std::move(uber_struct2);

  std::vector<glm::mat3> expected_matrices = {
      glm::scale(glm::mat3(), glm::vec2(2.f)),   // 1:0 = 2
      glm::scale(glm::mat3(), glm::vec2(10.f)),  // 1:0 * 2:0 = 2 * 5 = 10
      glm::scale(glm::mat3(), glm::vec2(6.f)),   // 1:0 * 1:1 = 2 * 3 = 6
  };

  auto global_matrices = ComputeGlobalMatrices(topology_vector, parent_indices, uber_structs);
  EXPECT_THAT(global_matrices, ::testing::ElementsAreArray(expected_matrices));
}

// The following tests ensure that different clip boundaries affect rectangles in the proper manner.

// Test that if a clip region is completely larger than the rectangle, it has no effect on the
// rectangle.
TEST(Rectangle2DTest, ParentCompletelyBiggerThanChildClipTest) {
  const glm::vec2 extent(100.f, 50.f);
  auto matrix = glm::scale(glm::mat3(), extent);

  TransformClipRegion clip = {0, 0, 120, 60};

  const escher::Rectangle2D expected_rectangle(
      glm::vec2(0, 0), extent,
      {glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1)});

  const auto rectangle = GetRectangleForMatrixAndClip(matrix, clip);
  EXPECT_EQ(rectangle, expected_rectangle);
}

// Test that if the child is completely bigger on all sides than the clip, that it gets clamped
// exactly to the clip region.
TEST(Rectangle2DTest, ChildCompletelyBiggerThanParentClipTest) {
  const glm::vec2 extent(100.f, 90.f);
  auto matrix = glm::scale(glm::mat3(), extent);

  TransformClipRegion clip = {20, 30, 35, 40};

  const escher::Rectangle2D expected_rectangle(glm::vec2(clip.x, clip.y),
                                               glm::vec2(clip.width, clip.height),
                                               {glm::vec2(.2, .3333), glm::vec2(.55, 0.333333),
                                                glm::vec2(.55, 0.777777), glm::vec2(.2, 0.777777)});

  const auto rectangle = GetRectangleForMatrixAndClip(matrix, clip);
  EXPECT_EQ(rectangle, expected_rectangle);
}

// Test that if the child doesn't overlap the clip region at all, that the
// rectangle has zero size.
TEST(Rectangle2DTest, RectangleAndClipNoOverlap) {
  const glm::vec2 offset(5, 10);
  const glm::vec2 extent(100.f, 50.f);
  glm::mat3 matrix = glm::translate(glm::mat3(), offset);
  matrix = glm::scale(matrix, extent);

  TransformClipRegion clip = {0, 0, 2, 2};

  const escher::Rectangle2D expected_rectangle(
      glm::vec2(0, 0), glm::vec2(0, 0),
      {glm::vec2(0, 0), glm::vec2(0, 0), glm::vec2(0, 0), glm::vec2(0, 0)});

  const auto rectangle = GetRectangleForMatrixAndClip(matrix, clip);
  EXPECT_EQ(rectangle, expected_rectangle);
}

// Test that clipping works in the case of partial overlap.
TEST(Rectangle2DTest, RectangleAndClipPartialOverlap) {
  const glm::vec2 offset(20, 30);
  const glm::vec2 extent(100.f, 50.f);
  glm::mat3 matrix = glm::translate(glm::mat3(), offset);
  matrix = glm::scale(matrix, extent);

  TransformClipRegion clip = {10, 30, 80, 40};

  const escher::Rectangle2D expected_rectangle(
      glm::vec2(20, 30), glm::vec2(70, 40),
      {glm::vec2(0, 0), glm::vec2(0.7, 0), glm::vec2(0.7, 0.8), glm::vec2(0, 0.8)});

  const auto rectangle = GetRectangleForMatrixAndClip(matrix, clip);
  EXPECT_EQ(rectangle, expected_rectangle);
}

// The following tests ensure that different geometric attributes (translation, rotation, scale)
// modify the final rectangle as expected.

TEST(Rectangle2DTest, ScaleAndRotate90DegreesTest) {
  const glm::vec2 extent(100.f, 50.f);
  glm::mat3 matrix = glm::rotate(glm::mat3(), glm::half_pi<float>());
  matrix = glm::scale(matrix, extent);

  const escher::Rectangle2D expected_rectangle(
      glm::vec2(0.f, 100.f), glm::vec2(50.f, 100.f),
      {glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1), glm::vec2(0, 0)});

  const auto rectangle = GetRectangleForMatrix(matrix);
  EXPECT_EQ(rectangle, expected_rectangle);
}

TEST(Rectangle2DTest, ScaleAndRotate180DegreesTest) {
  const glm::vec2 extent(100.f, 50.f);
  glm::mat3 matrix = glm::rotate(glm::mat3(), glm::pi<float>());
  matrix = glm::scale(matrix, extent);

  const escher::Rectangle2D expected_rectangle(
      glm::vec2(-100.f, 50.f), glm::vec2(100.f, 50.f),
      {glm::vec2(1, 1), glm::vec2(0, 1), glm::vec2(0, 0), glm::vec2(1, 0)});

  const auto rectangle = GetRectangleForMatrix(matrix);
  EXPECT_EQ(rectangle, expected_rectangle);
}

TEST(Rectangle2DTest, ScaleAndRotate270DegreesTest) {
  const glm::vec2 extent(100.f, 50.f);
  glm::mat3 matrix = glm::rotate(glm::mat3(), glm::three_over_two_pi<float>());
  matrix = glm::scale(matrix, extent);

  const escher::Rectangle2D expected_rectangle(
      glm::vec2(-50.f, 0.f), glm::vec2(50.f, 100.f),
      {glm::vec2(0, 1), glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1)});

  const auto rectangle = GetRectangleForMatrix(matrix);
  EXPECT_EQ(rectangle, expected_rectangle);
}

// Make sure that floating point transform values that aren't exactly
// integers are also respected.
TEST(Rectangle2DTest, FloatingPointTranslateAndScaleTest) {
  const glm::vec2 offset(10.9f, 20.5f);
  const glm::vec2 extent(100.3f, 200.7f);
  glm::mat3 matrix = glm::translate(glm::mat3(), offset);
  matrix = glm::scale(matrix, extent);

  const escher::Rectangle2D expected_rectangle(
      offset, extent, {glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1)});

  const auto rectangle = GetRectangleForMatrix(matrix);
  EXPECT_EQ(rectangle, expected_rectangle);
}

TEST(Rectangle2DTest, NegativeScaleTest) {
  // If both the x and y scale components are negative, this is equivalent
  // to a positive scale rotated by 180 degrees (PI radians).
  {
    const glm::vec2 extent(-10.f, -5.f);
    glm::mat3 matrix = glm::scale(glm::mat3(), extent);

    // These are the expected UVs for a 180 degree rotation.
    const escher::Rectangle2D expected_rectangle(
        glm::vec2(-10.f, 5.f), glm::vec2(10.f, 5.f),
        {glm::vec2(1, 1), glm::vec2(0, 1), glm::vec2(0, 0), glm::vec2(1, 0)});

    const auto rectangle = GetRectangleForMatrix(matrix);
    EXPECT_EQ(rectangle, expected_rectangle);
  }

  // If just the x scale component is negative and the y component is positive,
  // this is equivalent to a flip about the y axis (horiziontal).
  {
    const glm::vec2 extent(-10.f, 5.f);
    glm::mat3 matrix = glm::scale(glm::mat3(), extent);

    // These are the expected UVs for a horizontal flip.
    const escher::Rectangle2D expected_rectangle(
        glm::vec2(-10.f, 0.f), glm::vec2(10.f, 5.f),
        {glm::vec2(1, 0), glm::vec2(0, 0), glm::vec2(0, 1), glm::vec2(1, 1)});

    const auto rectangle = GetRectangleForMatrix(matrix);
    EXPECT_EQ(rectangle, expected_rectangle);
  }

  // If just the y scale component is negative and the x component is positive,
  // this is equivalent to a vertical flip about the x axis.
  {
    const glm::vec2 extent(10.f, -5.f);
    glm::mat3 matrix = glm::scale(glm::mat3(), extent);

    // These are the expected UVs for a vertical flip.
    const escher::Rectangle2D expected_rectangle(
        glm::vec2(0.f, 5.f), glm::vec2(10.f, 5.f),
        {glm::vec2(0, 1), glm::vec2(1, 1), glm::vec2(1, 0), glm::vec2(0, 0)});

    const auto rectangle = GetRectangleForMatrix(matrix);
    EXPECT_EQ(rectangle, expected_rectangle);
  }
}

// The same operations of translate/rotate/scale on a single matrix.
TEST(Rectangle2DTest, OrderOfOperationsTest) {
  // First subtest tests swapping scaling and translation.
  {
    // Here we scale and then translate. The origin should be at (10,5) and the extent should also
    // still be (2,2) since the scale is being applied on the untranslated coordinates.
    const glm::mat3 test_1 =
        glm::scale(glm::translate(glm::mat3(), glm::vec2(10.f, 5.f)), glm::vec2(2.f, 2.f));

    const escher::Rectangle2D expected_rectangle_1(glm::vec2(10.f, 5.f), glm::vec2(2.f, 2.f));

    const auto rectangle_1 = GetRectangleForMatrix(test_1);
    EXPECT_EQ(rectangle_1, expected_rectangle_1);

    // Here we translate first, and then scale the translation, resulting in the origin point
    // doubling from (10, 5) to (20, 10).
    const glm::mat3 test_2 =
        glm::translate(glm::scale(glm::mat3(), glm::vec2(2.f, 2.f)), glm::vec2(10.f, 5.f));

    const escher::Rectangle2D expected_rectangle_2(glm::vec2(20.f, 10.f), glm::vec2(2.f, 2.f));

    const auto rectangle_2 = GetRectangleForMatrix(test_2);
    EXPECT_EQ(rectangle_2, expected_rectangle_2);
  }

  // Second subtest tests swapping translation and rotation.
  {
    // Since the rotation is applied first, the origin point rotates around (0,0) and then we
    // translate and wind up at (10, 5).
    const glm::mat3 test_1 =
        glm::rotate(glm::translate(glm::mat3(), glm::vec2(10.f, 5.f)), glm::half_pi<float>());

    const escher::Rectangle2D expected_rectangle_1(
        glm::vec2(10.f, 6.f), glm::vec2(1.f, 1.f),
        {glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1), glm::vec2(0, 0)});

    const auto rectangle_1 = GetRectangleForMatrix(test_1);
    EXPECT_EQ(rectangle_1, expected_rectangle_1);

    // Since we translated first here, the point goes from (0,0) to (10,5) and then rotates
    // 90 degrees counterclockwise and winds up at (-5, 10).
    const glm::mat3 test_2 =
        glm::translate(glm::rotate(glm::mat3(), glm::half_pi<float>()), glm::vec2(10.f, 5.f));

    const escher::Rectangle2D expected_rectangle_2(
        glm::vec2(-5.f, 11.f), glm::vec2(1.f, 1.f),
        {glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1), glm::vec2(0, 0)});

    const auto rectangle_2 = GetRectangleForMatrix(test_2);
    EXPECT_EQ(rectangle_2, expected_rectangle_2);
  }

  // Third subtest tests swapping non-uniform scaling and rotation.
  {
    // We rotate first and then scale, so the scaling isn't affected by the rotation.
    const glm::mat3 test_1 =
        glm::rotate(glm::scale(glm::mat3(), glm::vec2(9.f, 7.f)), glm::half_pi<float>());

    const escher::Rectangle2D expected_rectangle_1(
        glm::vec2(0.f, 7.f), glm::vec2(9.f, 7.f),
        {glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1), glm::vec2(0, 0)});

    const auto rectangle_1 = GetRectangleForMatrix(test_1);
    EXPECT_EQ(rectangle_1, expected_rectangle_1);

    // Here we scale and then rotate so the scale winds up rotated.
    const glm::mat3 test_2 =
        glm::scale(glm::rotate(glm::mat3(), glm::half_pi<float>()), glm::vec2(9.f, 7.f));

    const escher::Rectangle2D expected_rectangle_2(
        glm::vec2(0.f, 9.f), glm::vec2(7.f, 9.f),
        {glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1), glm::vec2(0, 0)});

    const auto rectangle_2 = GetRectangleForMatrix(test_2);
    EXPECT_EQ(rectangle_2, expected_rectangle_2);
  }
}

// We recreate several of the matrix tests above with opacity values here,
// since the logic for calculating opacities is largely the same as calculating
// matrices, where child values are the product of their local values and their
// ancestors' values.
//
// TODO(fxbug.dev/73516): Since the logic between matrices and opacity is very similar, in the
// future we may want to consolidate |ComputeGlobalMatrices| and |ComputeGlobalOpacityValues| into a
// single (potentially templated) function, which would allow us to consolidate these tests into
// one. But for now, we have to keep them separate.

TEST(GlobalImageDataTest, EmptyTopologyReturnsEmptyOpacityValues) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::TopologyVector topology_vector;
  GlobalTopologyData::ParentIndexVector parent_indices;

  auto global_opacity_values =
      ComputeGlobalOpacityValues(topology_vector, parent_indices, uber_structs);
  EXPECT_TRUE(global_opacity_values.empty());
}

// Check that if there are no opacity values provided, they default to 1.0 for
// parent and child.
TEST(GlobalImageDataTest, EmptyLocalOpacitiesAreOpaque) {
  UberStruct::InstanceMap uber_structs;

  // Make a global topology representing the following graph:
  //
  // 1:0 - 1:1
  GlobalTopologyData::TopologyVector topology_vector = {{1, 0}, {1, 1}};
  GlobalTopologyData::ParentIndexVector parent_indices = {0, 0};

  // The UberStruct for instance ID 1 must exist, but it contains no local opacity values.
  auto uber_struct = std::make_unique<UberStruct>();
  uber_structs[1] = std::move(uber_struct);

  // The root opacity value is set to 1.0, and the second inherits that.
  std::vector<float> expected_opacities = {
      1.f,
      1.f,
  };

  auto global_opacities = ComputeGlobalOpacityValues(topology_vector, parent_indices, uber_structs);
  EXPECT_THAT(global_opacities, ::testing::ElementsAreArray(expected_opacities));
}

// Test a more complicated scenario with multiple parent-child relationships and make
// sure all of the opacity values are being inherited properly.
TEST(GlobalImageDataTest, GlobalImagesIncludeParentImage) {
  UberStruct::InstanceMap uber_structs;

  // Make a global topology representing the following graph:
  //
  // 1:0 - 1:1 - 1:2
  //     \
  //       1:3 - 1:4
  GlobalTopologyData::TopologyVector topology_vector = {{1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 4}};
  GlobalTopologyData::ParentIndexVector parent_indices = {0, 0, 1, 0, 3};

  auto uber_struct = std::make_unique<UberStruct>();

  const float opacities[] = {0.9f, 0.8f, 0.7f, 0.6f, 0.5f};

  uber_struct->local_opacity_values[{1, 0}] = opacities[0];

  uber_struct->local_opacity_values[{1, 1}] = opacities[1];
  uber_struct->local_opacity_values[{1, 2}] = opacities[2];

  uber_struct->local_opacity_values[{1, 3}] = opacities[3];
  uber_struct->local_opacity_values[{1, 4}] = opacities[4];

  uber_structs[1] = std::move(uber_struct);

  std::vector<float> expected_opacities = {
      opacities[0],
      opacities[0] * opacities[1],
      opacities[0] * opacities[1] * opacities[2],
      opacities[0] * opacities[3],
      opacities[0] * opacities[3] * opacities[4],
  };

  auto global_opacities = ComputeGlobalOpacityValues(topology_vector, parent_indices, uber_structs);
  EXPECT_THAT(global_opacities, ::testing::ElementsAreArray(expected_opacities));
}

TEST(GlobalImageDataTest, GlobalImagesMultipleUberStructs) {
  UberStruct::InstanceMap uber_structs;

  // Make a global topology representing the following graph:
  //
  // 1:0 - 2:0
  //     \
  //       1:1
  GlobalTopologyData::TopologyVector topology_vector = {{1, 0}, {2, 0}, {1, 1}};
  GlobalTopologyData::ParentIndexVector parent_indices = {0, 0, 0};

  auto uber_struct1 = std::make_unique<UberStruct>();
  auto uber_struct2 = std::make_unique<UberStruct>();

  const float opacity_values[] = {0.5f, 0.3f, 0.9f};

  uber_struct1->local_opacity_values[{1, 0}] = opacity_values[0];
  uber_struct2->local_opacity_values[{2, 0}] = opacity_values[1];
  uber_struct1->local_opacity_values[{1, 1}] = opacity_values[2];

  uber_structs[1] = std::move(uber_struct1);
  uber_structs[2] = std::move(uber_struct2);

  std::vector<float> expected_opacity_values = {opacity_values[0],
                                                opacity_values[0] * opacity_values[1],
                                                opacity_values[0] * opacity_values[2]};

  auto global_opacity_values =
      ComputeGlobalOpacityValues(topology_vector, parent_indices, uber_structs);
  EXPECT_THAT(global_opacity_values, ::testing::ElementsAreArray(expected_opacity_values));
}

// The following tests test for image sample regions.

// Test that an empty uber struct returns empty sample regions.
TEST(GlobalImageDataTest, EmptyTopologyReturnsEmptyImageSampleRegions) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::TopologyVector topology_vector;
  GlobalTopologyData::ParentIndexVector parent_indices;

  auto global_sample_regions =
      ComputeGlobalImageSampleRegions(topology_vector, parent_indices, uber_structs);
  EXPECT_TRUE(global_sample_regions.empty());
}

// Check that if there are no sample regions provided, they default to
// empty ImageSampleRegion structs.
TEST(GlobalImageDataTest, EmptySampleRegionsAreInvalid) {
  UberStruct::InstanceMap uber_structs;

  // Make a global topology representing the following graph:
  //
  // 1:0 - 1:1
  GlobalTopologyData::TopologyVector topology_vector = {{1, 0}, {1, 1}};
  GlobalTopologyData::ParentIndexVector parent_indices = {0, 0};

  // The UberStruct for instance ID 1 must exist, but it contains no local opacity values.
  auto uber_struct = std::make_unique<UberStruct>();
  uber_structs[1] = std::move(uber_struct);

  GlobalImageSampleRegionVector expected_sample_regions = {kInvalidSampleRegion,
                                                           kInvalidSampleRegion};

  auto global_sample_regions =
      ComputeGlobalImageSampleRegions(topology_vector, parent_indices, uber_structs);
  EXPECT_EQ(expected_sample_regions.size(), global_sample_regions.size());
  for (uint32_t i = 0; i < global_sample_regions.size(); i++) {
    EXPECT_EQ(expected_sample_regions[i].x, global_sample_regions[i].x);
    EXPECT_EQ(expected_sample_regions[i].y, global_sample_regions[i].y);
    EXPECT_EQ(expected_sample_regions[i].width, global_sample_regions[i].width);
    EXPECT_EQ(expected_sample_regions[i].height, global_sample_regions[i].height);
  }
}

// Test a more complicated scenario with multiple transforms, each with its own
// set of image sample regions, and make sure that they all get calculated correctly.
TEST(GlobalImageDataTest, ComplicatedGraphImageSampleRegions) {
  UberStruct::InstanceMap uber_structs;

  // Make a global topology representing the following graph:
  //
  // 1:0 - 1:1 - 1:2
  //     \
  //       1:3 - 1:4
  GlobalTopologyData::TopologyVector topology_vector = {{1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 4}};
  GlobalTopologyData::ParentIndexVector parent_indices = {0, 0, 1, 0, 3};

  auto uber_struct = std::make_unique<UberStruct>();

  GlobalImageSampleRegionVector expected_sample_regions = {
      {0, 0, 81, 15}, {5, 18, 100, 145}, {10, 4, 10, 667}, {33, 99, 910, 783}, {90, 76, 392, 991},
  };

  uber_struct->local_image_sample_regions[{1, 0}] = expected_sample_regions[0];

  uber_struct->local_image_sample_regions[{1, 1}] = expected_sample_regions[1];
  uber_struct->local_image_sample_regions[{1, 2}] = expected_sample_regions[2];

  uber_struct->local_image_sample_regions[{1, 3}] = expected_sample_regions[3];
  uber_struct->local_image_sample_regions[{1, 4}] = expected_sample_regions[4];

  uber_structs[1] = std::move(uber_struct);

  auto global_sample_regions =
      ComputeGlobalImageSampleRegions(topology_vector, parent_indices, uber_structs);
  EXPECT_EQ(expected_sample_regions.size(), global_sample_regions.size());
  for (uint32_t i = 0; i < global_sample_regions.size(); i++) {
    EXPECT_EQ(expected_sample_regions[i].x, global_sample_regions[i].x);
    EXPECT_EQ(expected_sample_regions[i].y, global_sample_regions[i].y);
    EXPECT_EQ(expected_sample_regions[i].width, global_sample_regions[i].width);
    EXPECT_EQ(expected_sample_regions[i].height, global_sample_regions[i].height);
  }
}

// The following tests test for transform clip regions

// Test that an empty uber struct returns empty clip regions.
TEST(GlobalTransformClipTest, EmptyTopologyReturnsEmptyClipRegions) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::TopologyVector topology_vector;
  GlobalTopologyData::ParentIndexVector parent_indices;

  auto global_clip_regions =
      ComputeGlobalTransformClipRegions(topology_vector, parent_indices, uber_structs);
  EXPECT_TRUE(global_clip_regions.empty());
}

// Check that if there are no clip regions provided, they default to
// non-clipped regions.
TEST(GlobalTransformClipTest, EmptyClipRegionsAreInvalid) {
  UberStruct::InstanceMap uber_structs;

  // Make a global topology representing the following graph:
  //
  // 1:0 - 1:1
  GlobalTopologyData::TopologyVector topology_vector = {{1, 0}, {1, 1}};
  GlobalTopologyData::ParentIndexVector parent_indices = {0, 0};

  // The UberStruct for instance ID 1 must exist, but it contains no local opacity values.
  auto uber_struct = std::make_unique<UberStruct>();
  uber_structs[1] = std::move(uber_struct);

  GlobalTransformClipRegionVector expected_clip_regions = {kUnclippedRegion, kUnclippedRegion};

  auto global_clip_regions =
      ComputeGlobalTransformClipRegions(topology_vector, parent_indices, uber_structs);
  EXPECT_EQ(expected_clip_regions.size(), global_clip_regions.size());
  for (uint32_t i = 0; i < global_clip_regions.size(); i++) {
    EXPECT_EQ(expected_clip_regions[i].x, global_clip_regions[i].x);
    EXPECT_EQ(expected_clip_regions[i].y, global_clip_regions[i].y);
    EXPECT_EQ(expected_clip_regions[i].width, global_clip_regions[i].width);
    EXPECT_EQ(expected_clip_regions[i].height, global_clip_regions[i].height);
  }
}

// The parent and child regions do not overlap, so the child region should
// be completely empty.
TEST(GlobalTransformClipTest, NoOverlapClipRegions) {
  UberStruct::InstanceMap uber_structs;

  // Make a global topology representing the following graph:
  //
  // 1:0 - 1:1
  GlobalTopologyData::TopologyVector topology_vector = {{1, 0}, {1, 1}};
  GlobalTopologyData::ParentIndexVector parent_indices = {0, 0};

  auto uber_struct = std::make_unique<UberStruct>();

  // The two regions do not overlap.
  GlobalTransformClipRegionVector clip_regions = {{0, 0, 100, 200}, {200, 300, 100, 200}};

  uber_struct->local_clip_regions[{1, 0}] = clip_regions[0];
  uber_struct->local_clip_regions[{1, 1}] = clip_regions[1];

  uber_structs[1] = std::move(uber_struct);

  GlobalTransformClipRegionVector expected_clip_regions = {clip_regions[0], {0, 0, 0, 0}};

  auto global_clip_regions =
      ComputeGlobalTransformClipRegions(topology_vector, parent_indices, uber_structs);
  EXPECT_EQ(global_clip_regions.size(), expected_clip_regions.size());
  for (uint64_t i = 0; i < global_clip_regions.size(); i++) {
    const auto& a = global_clip_regions[i];
    const auto& b = expected_clip_regions[i];
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
    EXPECT_FLOAT_EQ(a.width, b.width);
    EXPECT_FLOAT_EQ(a.height, b.height);
  }
}

// Test a more complicated scenario with multiple transforms, each with its own
// clip region set, and make sure that they all get calculated correctly.
TEST(GlobalTransformClipTest, ComplicatedGraphClipRegions) {
  UberStruct::InstanceMap uber_structs;

  // Make a global topology representing the following graph:
  //
  // 1:0 - 1:1 - 1:2
  //     \
  //       1:3 - 1:4
  GlobalTopologyData::TopologyVector topology_vector = {{1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 4}};
  GlobalTopologyData::ParentIndexVector parent_indices = {0, 0, 1, 0, 3};

  auto uber_struct = std::make_unique<UberStruct>();

  GlobalTransformClipRegionVector clip_regions = {
      {5, 10, 100, 200}, kUnclippedRegion, {20, 30, 110, 300}, {0, 0, 300, 400}, {-10, -20, 20, 30},
  };

  uber_struct->local_clip_regions[{1, 0}] = clip_regions[0];

  uber_struct->local_clip_regions[{1, 1}] = clip_regions[1];
  uber_struct->local_clip_regions[{1, 2}] = clip_regions[2];

  uber_struct->local_clip_regions[{1, 3}] = clip_regions[3];
  uber_struct->local_clip_regions[{1, 4}] = clip_regions[4];

  uber_structs[1] = std::move(uber_struct);

  GlobalTransformClipRegionVector expected_clip_regions = {
      clip_regions[0], clip_regions[0], {20, 30, 85, 180}, {5, 10, 100, 200}, {5, 10, 5, 0},
  };

  auto global_clip_regions =
      ComputeGlobalTransformClipRegions(topology_vector, parent_indices, uber_structs);
  EXPECT_EQ(global_clip_regions.size(), expected_clip_regions.size());
  for (uint64_t i = 0; i < global_clip_regions.size(); i++) {
    const auto& a = global_clip_regions[i];
    const auto& b = expected_clip_regions[i];
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
    EXPECT_FLOAT_EQ(a.width, b.width);
    EXPECT_FLOAT_EQ(a.height, b.height);
  }
}

}  // namespace test
}  // namespace flatland

#undef EXPECT_VEC2
