// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/shape/mesh_spec.h"

#include <iostream>

#include <gtest/gtest.h>

#include "src/ui/lib/escher/geometry/types.h"

namespace {
using namespace escher;

TEST(MeshSpec, SingleAttributeOffsetAndStride) {
  {
    MeshSpec spec{MeshAttribute::kPosition2D};
    EXPECT_EQ(0U, spec.attribute_offset(0, MeshAttribute::kPosition2D));
    EXPECT_EQ(sizeof(vec2), spec.stride(0));
  }

  {
    MeshSpec spec{MeshAttribute::kPosition3D};
    EXPECT_EQ(0U, spec.attribute_offset(0, MeshAttribute::kPosition3D));
    EXPECT_EQ(sizeof(vec3), spec.stride(0));
  }

  {
    MeshSpec spec{MeshAttribute::kPositionOffset};
    EXPECT_EQ(0U, spec.attribute_offset(0, MeshAttribute::kPositionOffset));
    EXPECT_EQ(sizeof(vec2), spec.stride(0));
  }

  {
    MeshSpec spec{MeshAttribute::kUV};
    EXPECT_EQ(0U, spec.attribute_offset(0, MeshAttribute::kUV));
    EXPECT_EQ(sizeof(vec2), spec.stride(0));
  }

  {
    MeshSpec spec{MeshAttribute::kPerimeterPos};
    EXPECT_EQ(0U, spec.attribute_offset(0, MeshAttribute::kPerimeterPos));
    EXPECT_EQ(sizeof(float), spec.stride(0));
  }
}

TEST(MeshSpec, MultiAttributeOffsetAndStride) {
  // All attributes.
  {
    MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kPositionOffset | MeshAttribute::kUV |
                  MeshAttribute::kPerimeterPos};
    size_t expected_offset = 0;
    EXPECT_EQ(0U, spec.attribute_offset(0, MeshAttribute::kPosition2D));
    expected_offset += sizeof(vec2);
    EXPECT_EQ(expected_offset, spec.attribute_offset(0, MeshAttribute::kPositionOffset));
    expected_offset += sizeof(vec2);
    EXPECT_EQ(expected_offset, spec.attribute_offset(0, MeshAttribute::kUV));
    expected_offset += sizeof(vec2);
    EXPECT_EQ(expected_offset, spec.attribute_offset(0, MeshAttribute::kPerimeterPos));
    expected_offset += sizeof(float);
    EXPECT_EQ(expected_offset, spec.stride(0));
  }

  // Leave out kUV.  This should affect the offset of kPerimeterPos.
  {
    MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kPositionOffset |
                  MeshAttribute::kPerimeterPos};
    size_t expected_offset = 0;
    EXPECT_EQ(0U, spec.attribute_offset(0, MeshAttribute::kPosition2D));
    expected_offset += sizeof(vec2);
    EXPECT_EQ(expected_offset, spec.attribute_offset(0, MeshAttribute::kPositionOffset));
    expected_offset += sizeof(vec2);
    EXPECT_EQ(expected_offset, spec.attribute_offset(0, MeshAttribute::kPerimeterPos));
    expected_offset += sizeof(float);
    EXPECT_EQ(expected_offset, spec.stride(0));
  }
}

TEST(MeshSpec, NumAttributes) {
  EXPECT_EQ(4U, MeshSpec{MeshAttribute::kPosition2D | MeshAttribute::kPositionOffset |
                         MeshAttribute::kUV | MeshAttribute::kPerimeterPos}
                    .attribute_count(0));

  EXPECT_EQ(4U, MeshSpec{MeshAttribute::kPosition3D | MeshAttribute::kPositionOffset |
                         MeshAttribute::kUV | MeshAttribute::kPerimeterPos}
                    .attribute_count(0));

  EXPECT_EQ(2U, MeshSpec{MeshAttribute::kPosition2D | MeshAttribute::kUV}.attribute_count(0));

  EXPECT_EQ(2U, MeshSpec{MeshAttribute::kPosition3D | MeshAttribute::kUV}.attribute_count(0));

  EXPECT_EQ(1U, MeshSpec{MeshAttribute::kPosition2D}.attribute_count(0));

  EXPECT_EQ(1U, MeshSpec{MeshAttribute::kPosition3D}.attribute_count(0));

  EXPECT_EQ(0U, MeshSpec{MeshAttributes()}.attribute_count(0));
}

TEST(MeshSpec, Validity) {
  // Meshs must have either 2D positions or 3D positions, not both.
  EXPECT_TRUE(MeshSpec{MeshAttribute::kPosition2D}.IsValidOneBufferMesh());
  EXPECT_TRUE(MeshSpec{MeshAttribute::kPosition3D}.IsValidOneBufferMesh());
  EXPECT_FALSE(MeshSpec{MeshAttributes()}.IsValidOneBufferMesh());
  EXPECT_FALSE(
      MeshSpec{MeshAttribute::kPosition2D | MeshAttribute::kPosition3D}.IsValidOneBufferMesh());
}

}  // namespace
