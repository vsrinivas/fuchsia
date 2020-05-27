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
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

namespace flatland {
namespace test {

namespace {
static const float kDegreesToRadians = glm::pi<float>() / 180.f;

// Wraper function for glm::all(glm::epsilonEqual()) to keep the call
// site cleaner.
bool Equal(const glm::vec2& a, const glm::vec2& b) {
  return glm::all(glm::epsilonEqual(a, b, 0.001f));
}

const escher::Rectangle2D CreateRectangleTest(const glm::mat3& matrix) {
  return CreateRectangle2D(matrix,
                           {glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1)});
}

}  // namespace

TEST(GlobalMatrixDataTest, EmptyTopologyReturnsEmptyMatrices) {
  UberStruct::InstanceMap uber_structs;
  GlobalTopologyData::TopologyVector topology_vector;
  GlobalTopologyData::ParentIndexVector parent_indices;

  auto global_matrices = ComputeGlobalMatrixData(topology_vector, parent_indices, uber_structs);
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

  auto global_matrices = ComputeGlobalMatrixData(topology_vector, parent_indices, uber_structs);
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

  auto global_matrices = ComputeGlobalMatrixData(topology_vector, parent_indices, uber_structs);
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

  auto global_matrices = ComputeGlobalMatrixData(topology_vector, parent_indices, uber_structs);
  EXPECT_THAT(global_matrices, ::testing::ElementsAreArray(expected_matrices));
}

TEST(Rectangle2DTest, ScaleAndRotate90DegreesTest) {
  glm::vec2 extent(100, 50);
  glm::mat3 matrix = glm::rotate(glm::mat3(), 90.f * kDegreesToRadians);
  matrix = glm::scale(matrix, extent);

  auto rectangle = CreateRectangleTest(matrix);
  EXPECT_TRUE(Equal(rectangle.origin, glm::vec2(0, 100)));
  EXPECT_TRUE(Equal(rectangle.extent, glm::vec2(50, 100)));
  EXPECT_EQ(rectangle.clockwise_uvs[0], glm::vec2(1, 0));
  EXPECT_EQ(rectangle.clockwise_uvs[1], glm::vec2(1, 1));
  EXPECT_EQ(rectangle.clockwise_uvs[2], glm::vec2(0, 1));
  EXPECT_EQ(rectangle.clockwise_uvs[3], glm::vec2(0, 0));
}

TEST(Rectangle2DTest, ScaleAndRotate180DegreesTest) {
  glm::vec2 extent(100, 50);
  glm::mat3 matrix = glm::rotate(glm::mat3(), 180.f * kDegreesToRadians);
  matrix = glm::scale(matrix, extent);

  auto rectangle = CreateRectangleTest(matrix);
  EXPECT_TRUE(Equal(rectangle.origin, glm::vec2(-100, 50)));
  EXPECT_TRUE(Equal(rectangle.extent, glm::vec2(100, 50)));
  EXPECT_EQ(rectangle.clockwise_uvs[0], glm::vec2(1, 1));
  EXPECT_EQ(rectangle.clockwise_uvs[1], glm::vec2(0, 1));
  EXPECT_EQ(rectangle.clockwise_uvs[2], glm::vec2(0, 0));
  EXPECT_EQ(rectangle.clockwise_uvs[3], glm::vec2(1, 0));
}

TEST(Rectangle2DTest, ScaleAndRotate270DegreesTest) {
  glm::vec2 extent(100, 50);
  glm::mat3 matrix = glm::rotate(glm::mat3(), 270.f * kDegreesToRadians);
  matrix = glm::scale(matrix, extent);

  auto rectangle = CreateRectangleTest(matrix);
  EXPECT_TRUE(Equal(rectangle.origin, glm::vec2(-50, 0)));
  EXPECT_TRUE(Equal(rectangle.extent, glm::vec2(50, 100)));
  EXPECT_EQ(rectangle.clockwise_uvs[0], glm::vec2(0, 1));
  EXPECT_EQ(rectangle.clockwise_uvs[1], glm::vec2(0, 0));
  EXPECT_EQ(rectangle.clockwise_uvs[2], glm::vec2(1, 0));
  EXPECT_EQ(rectangle.clockwise_uvs[3], glm::vec2(1, 1));
}

// Make sure that floating point transform values that aren't exactly
// integers are also respected.
TEST(Rectangle2DTest, FloatingPointTranslateAndScaleTest) {
  glm::vec2 offset(10.9, 20.5);
  glm::vec2 extent(100.3, 200.7);
  glm::mat3 matrix = glm::translate(glm::mat3(), offset);
  matrix = glm::scale(matrix, extent);

  auto rectangle = CreateRectangleTest(matrix);
  EXPECT_EQ(rectangle.origin, offset);
  EXPECT_EQ(rectangle.extent, extent);
}

TEST(Rectangle2DTest, NegativeScaleTest) {
  // If both the x and y scale components are negative, this is equivalent
  // to a positive scale rotated by 180 degrees (PI radians).
  {
    glm::vec2 extent(-10, -5);
    glm::mat3 matrix = glm::scale(glm::mat3(), extent);
    auto rectangle = CreateRectangleTest(matrix);
    EXPECT_EQ(rectangle.extent, glm::vec2(10, 5));

    // These are the expected UVs for a 180 degree rotation.
    EXPECT_EQ(rectangle.clockwise_uvs[0], glm::vec2(1, 1));
    EXPECT_EQ(rectangle.clockwise_uvs[1], glm::vec2(0, 1));
    EXPECT_EQ(rectangle.clockwise_uvs[2], glm::vec2(0, 0));
    EXPECT_EQ(rectangle.clockwise_uvs[3], glm::vec2(1, 0));
  }

  // If just the x scale component is negative and the y component is positive,
  // this is equivalent to a flip about the y axis (horiziontal).
  {
    glm::vec2 extent(-10, 5);
    glm::mat3 matrix = glm::scale(glm::mat3(), extent);
    auto rectangle = CreateRectangleTest(matrix);
    EXPECT_TRUE(Equal(rectangle.origin, glm::vec2(-10, 0)));
    EXPECT_TRUE(Equal(rectangle.extent, glm::vec2(10, 5)));

    // These are the expected UVs for a horizontal flip.
    EXPECT_EQ(rectangle.clockwise_uvs[0], glm::vec2(1, 0));
    EXPECT_EQ(rectangle.clockwise_uvs[1], glm::vec2(0, 0));
    EXPECT_EQ(rectangle.clockwise_uvs[2], glm::vec2(0, 1));
    EXPECT_EQ(rectangle.clockwise_uvs[3], glm::vec2(1, 1));
  }

  // If just the y scale component is negative and the x component is positive,
  // this is equivalent to a vertical flip about the x axis.
  {
    glm::vec2 extent(10, -5);
    glm::mat3 matrix = glm::scale(glm::mat3(), extent);
    auto rectangle = CreateRectangleTest(matrix);
    EXPECT_TRUE(Equal(rectangle.origin, glm::vec2(0, 5)));
    EXPECT_TRUE(Equal(rectangle.extent, glm::vec2(10, 5)));

    // These are the expected UVs for a vertical flip.
    EXPECT_EQ(rectangle.clockwise_uvs[0], glm::vec2(0, 1));
    EXPECT_EQ(rectangle.clockwise_uvs[1], glm::vec2(1, 1));
    EXPECT_EQ(rectangle.clockwise_uvs[2], glm::vec2(1, 0));
    EXPECT_EQ(rectangle.clockwise_uvs[3], glm::vec2(0, 0));
  }
}

// The same operations of translate/rotate/scale on a single matrix.
TEST(Rectangle2DTest, OrderOfOperationsTest) {
  // First subtest tests swapping scaling and translation.
  {
    // Here we scale and then translate. The origin should be at (10,5) and the extent should also
    // still be (2,2) since the scale is being applied on the untranslated coordinates.
    glm::mat3 test_1 = glm::scale(glm::translate(glm::mat3(), glm::vec2(10, 5)), glm::vec2(2, 2));
    auto rectangle_1 = CreateRectangleTest(test_1);
    EXPECT_TRUE(Equal(rectangle_1.origin, glm::vec2(10, 5)));
    EXPECT_TRUE(Equal(rectangle_1.extent, glm::vec2(2, 2)));

    // Here we translate first, and then scale the translation, resulting in the origin point
    // doubling from (10, 5) to (20, 10).
    glm::mat3 test_2 = glm::translate(glm::scale(glm::mat3(), glm::vec2(2, 2)), glm::vec2(10, 5));
    auto rectangle_2 = CreateRectangleTest(test_2);
    EXPECT_TRUE(Equal(rectangle_2.origin, glm::vec2(20, 10)));
    EXPECT_TRUE(Equal(rectangle_2.extent, glm::vec2(2, 2)));
  }

  {
    // Since the rotation is applied first, the origin point rotates around (0,0) and then we
    // translate and wind up at (10, 5).
    glm::mat3 test_1 =
        glm::rotate(glm::translate(glm::mat3(), glm::vec2(10, 5)), 90.f * kDegreesToRadians);
    auto rectangle_1 = CreateRectangleTest(test_1);
    EXPECT_TRUE(Equal(rectangle_1.origin, glm::vec2(10, 6)));

    // Since we translated first here, the point goes from (0,0) to (10,5) and then rotates
    // 90 degrees counterclockwise and winds up at (-5, 10).
    glm::mat3 test_2 =
        glm::translate(glm::rotate(glm::mat3(), 90.f * kDegreesToRadians), glm::vec2(10, 5));
    auto rectangle_2 = CreateRectangleTest(test_2);
    EXPECT_TRUE(Equal(rectangle_2.origin, glm::vec2(-5, 11)));
  }

  // Third subtest tests swapping non-uniform scaling and rotation.
  {
    // We rotate first and then scale, so the scaling isn't affected by the rotation.
    glm::mat3 test_1 =
        glm::rotate(glm::scale(glm::mat3(), glm::vec2(9, 7)), 90.f * kDegreesToRadians);
    auto rectangle_1 = CreateRectangleTest(test_1);
    EXPECT_TRUE(Equal(rectangle_1.extent, glm::vec2(9, 7)));

    // Here we scale and then rotate so the scale winds up rotated.
    glm::mat3 test_2 =
        glm::scale(glm::rotate(glm::mat3(), 90.f * kDegreesToRadians), glm::vec2(9, 7));
    auto rectangle_2 = CreateRectangleTest(test_2);
    EXPECT_TRUE(Equal(rectangle_2.extent, glm::vec2(7, 9)));
  }
}

#undef CHECK_GLOBAL_TOPOLOGY_DATA

}  // namespace test
}  // namespace flatland
