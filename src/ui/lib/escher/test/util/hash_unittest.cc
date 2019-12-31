// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/ui/lib/escher/impl/model_pipeline_spec.h"
#include "src/ui/lib/escher/util/hash_map.h"
#include "src/ui/lib/escher/vk/image.h"

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
  HashMapHasher<Hashee> hash;
  EXPECT_EQ(hash(hashee0), hash(hashee1));

  // Hash shouldn't be zero (some objects may cache their own hash, and a value
  // of zero can be used to represent a dirty hash).
  EXPECT_NE(hash(hashee), 0U);

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
  // MeshSpec and ModelPipelineSpec.
  {
    MeshSpec mesh_spec{{MeshAttribute::kPosition2D, MeshAttribute::kUV}};

    impl::ModelPipelineSpec model_pipeline_spec;
    model_pipeline_spec.mesh_spec = mesh_spec;

    TestHashForValue(mesh_spec);
    TestHashForValue(model_pipeline_spec);
  }

  // ImageInfo.
  {
    ImageInfo info;
    info.format = vk::Format::eR32G32Sfloat;
    info.width = 1024;
    info.height = 768;
    info.sample_count = 2;
    info.usage = vk::ImageUsageFlagBits::eStorage;
    info.memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;

    TestHashForValue(info);
  }
}

}  // namespace
