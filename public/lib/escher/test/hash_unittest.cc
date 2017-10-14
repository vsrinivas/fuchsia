// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/util/hash.h"
#include "lib/escher/impl/model_pipeline_spec.h"

#include "gtest/gtest.h"

namespace {
using namespace escher;

// Ensure that our generic hash function works on the specified type.
template <typename Hashee>
void TestHashForValue(const Hashee& hashee) {
  uint8_t zeros[sizeof(Hashee)];
  uint8_t ones[sizeof(Hashee)];
  std::memset(zeros, 0, sizeof(Hashee));
  std::memset(ones, 255, sizeof(Hashee));

  Hashee& hashee0 = *reinterpret_cast<Hashee*>(zeros);
  Hashee& hashee1 = *reinterpret_cast<Hashee*>(ones);

  hashee0 = hashee;
  hashee1 = hashee;

  // Verify that there is no padding in Hashee (otherwise the padding bytes are
  // undefined garbage that will break the hash algorithm).
  for (size_t i = 0; i < sizeof(Hashee); ++i) {
    EXPECT_EQ(zeros[i], ones[i]);
  }

  // This is a bit paranoid... if the Hashees are bit-identical, then there
  // should be no way for the hash to fail, since it works only on the bits.
  Hash<Hashee> hash;
  EXPECT_EQ(hash(hashee1), hash(hashee1));

  // Paranoid check that equality is commutative.
  EXPECT_EQ(hashee, hashee0);
  EXPECT_EQ(hashee0, hashee);
  EXPECT_EQ(hashee, hashee1);
  EXPECT_EQ(hashee1, hashee);
  EXPECT_EQ(hashee0, hashee1);
  EXPECT_EQ(hashee1, hashee0);
}

// This test should be updated to include all hashed types used by Escher.
TEST(Hash, AllHashedTypes) {
  impl::ModelPipelineSpec model_pipeline_spec;
  MeshSpec mesh_spec;

  mesh_spec.flags = MeshAttribute::kPosition2D | MeshAttribute::kUV;

  model_pipeline_spec.mesh_spec = mesh_spec;
  model_pipeline_spec.shape_modifiers = ShapeModifier::kWobble;

  FXL_CHECK((model_pipeline_spec.shape_modifiers & ShapeModifier::kWobble) ==
            ShapeModifier::kWobble);

  TestHashForValue(model_pipeline_spec);
}

}  // namespace
