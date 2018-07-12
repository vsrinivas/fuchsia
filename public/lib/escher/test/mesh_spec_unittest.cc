// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/shape/mesh_spec.h"

#include "lib/escher/geometry/types.h"

#include "gtest/gtest.h"

#include <iostream>

namespace {
using namespace escher;

// This test should be updated to include all hashed types used by Escher.
TEST(MeshSpec, SingleAttributeOffsetAndStride) {
  {
    MeshSpec spec{MeshAttribute::kPosition2D};
    EXPECT_EQ(0U, spec.GetAttributeOffset(MeshAttribute::kPosition2D));
    EXPECT_EQ(sizeof(vec2), spec.GetStride());
  }

  {
    MeshSpec spec{MeshAttribute::kPosition3D};
    EXPECT_EQ(0U, spec.GetAttributeOffset(MeshAttribute::kPosition3D));
    EXPECT_EQ(sizeof(vec3), spec.GetStride());
  }

  {
    MeshSpec spec{MeshAttribute::kPositionOffset};
    EXPECT_EQ(0U, spec.GetAttributeOffset(MeshAttribute::kPositionOffset));
    EXPECT_EQ(sizeof(vec2), spec.GetStride());
  }

  {
    MeshSpec spec{MeshAttribute::kUV};
    EXPECT_EQ(0U, spec.GetAttributeOffset(MeshAttribute::kUV));
    EXPECT_EQ(sizeof(vec2), spec.GetStride());
  }

  {
    MeshSpec spec{MeshAttribute::kPerimeterPos};
    EXPECT_EQ(0U, spec.GetAttributeOffset(MeshAttribute::kPerimeterPos));
    EXPECT_EQ(sizeof(float), spec.GetStride());
  }
}

TEST(MeshSpec, MultiAttributeOffsetAndStride) {
  // All attributes.
  {
    MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kPositionOffset |
                  MeshAttribute::kUV | MeshAttribute::kPerimeterPos};
    size_t expected_offset = 0;
    EXPECT_EQ(0U, spec.GetAttributeOffset(MeshAttribute::kPosition2D));
    expected_offset += sizeof(vec2);
    EXPECT_EQ(expected_offset,
              spec.GetAttributeOffset(MeshAttribute::kPositionOffset));
    expected_offset += sizeof(vec2);
    EXPECT_EQ(expected_offset, spec.GetAttributeOffset(MeshAttribute::kUV));
    expected_offset += sizeof(vec2);
    EXPECT_EQ(expected_offset,
              spec.GetAttributeOffset(MeshAttribute::kPerimeterPos));
    expected_offset += sizeof(float);
    EXPECT_EQ(expected_offset, spec.GetStride());
  }

  // Leave out kUV.  This should affect the offset of kPerimeterPos.
  {
    MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kPositionOffset |
                  MeshAttribute::kPerimeterPos};
    size_t expected_offset = 0;
    EXPECT_EQ(0U, spec.GetAttributeOffset(MeshAttribute::kPosition2D));
    expected_offset += sizeof(vec2);
    EXPECT_EQ(expected_offset,
              spec.GetAttributeOffset(MeshAttribute::kPositionOffset));
    expected_offset += sizeof(vec2);
    EXPECT_EQ(expected_offset,
              spec.GetAttributeOffset(MeshAttribute::kPerimeterPos));
    expected_offset += sizeof(float);
    EXPECT_EQ(expected_offset, spec.GetStride());
  }
}

TEST(MeshSpec, NumAttributes) {
  EXPECT_EQ(
      4U, MeshSpec{MeshAttribute::kPosition2D | MeshAttribute::kPositionOffset |
                   MeshAttribute::kUV | MeshAttribute::kPerimeterPos}
              .GetNumAttributes());

  EXPECT_EQ(
      4U, MeshSpec{MeshAttribute::kPosition3D | MeshAttribute::kPositionOffset |
                   MeshAttribute::kUV | MeshAttribute::kPerimeterPos}
              .GetNumAttributes());

  EXPECT_EQ(2U, MeshSpec{MeshAttribute::kPosition2D | MeshAttribute::kUV}
                    .GetNumAttributes());

  EXPECT_EQ(2U, MeshSpec{MeshAttribute::kPosition3D | MeshAttribute::kUV}
                    .GetNumAttributes());

  EXPECT_EQ(1U, MeshSpec{MeshAttribute::kPosition2D}.GetNumAttributes());

  EXPECT_EQ(1U, MeshSpec{MeshAttribute::kPosition3D}.GetNumAttributes());

  EXPECT_EQ(0U, MeshSpec{MeshAttributes()}.GetNumAttributes());
}

TEST(MeshSpec, Validity) {
  // Meshs must have either 2D positions or 3D positions, not both.
  EXPECT_TRUE(MeshSpec{MeshAttribute::kPosition2D}.IsValid());
  EXPECT_TRUE(MeshSpec{MeshAttribute::kPosition3D}.IsValid());
  EXPECT_FALSE(MeshSpec{MeshAttributes()}.IsValid());
  EXPECT_FALSE(MeshSpec{MeshAttribute::kPosition2D | MeshAttribute::kPosition3D}
                   .IsValid());
}

}  // namespace
