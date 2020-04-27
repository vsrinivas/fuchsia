// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/global_matrix_data.h"

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

namespace flatland {
namespace test {

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

#undef CHECK_GLOBAL_TOPOLOGY_DATA

}  // namespace test
}  // namespace flatland
