// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/global_matrix_data.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

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
  const auto rectangles = ComputeGlobalRectangles({matrix});
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

}  // namespace test
}  // namespace flatland

#undef EXPECT_VEC2
