// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/align.h"

#include <gtest/gtest.h>

namespace {
using namespace escher;

TEST(Alignment, AlignedToNext) {
  for (size_t alignment = 1; alignment < 100; ++alignment) {
    for (size_t input = 0; input < 1000; ++input) {
      size_t output = AlignedToNext(input, alignment);
      EXPECT_GE(output, input);
      EXPECT_LT(output - input, alignment);
      EXPECT_EQ(output % alignment, 0U);
    }
  }
}

template <typename T, size_t N>
struct NVals {
  T vals[N];
  uint8_t padding_maker;
};

template <typename T, size_t N>
void TestNextAlignedPtr() {
  using NV = NVals<T, N>;

  // No deep thought was put into choice of |base| and |kUntil|, other than
  // "not too big and not too small".
  uint8_t* base = reinterpret_cast<uint8_t*>(987654321U);
  constexpr size_t kUntil = 5 * sizeof(NV);
  for (size_t offset = 0; offset < kUntil; ++offset) {
    uint8_t* unaligned = base + offset;
    NV* aligned_nv = NextAlignedPtr<NV>(unaligned);
    uint8_t* aligned = reinterpret_cast<uint8_t*>(aligned_nv);

    EXPECT_GE(aligned, unaligned);
    EXPECT_LT(aligned, unaligned + alignof(NV));
    EXPECT_EQ(reinterpret_cast<size_t>(aligned) % alignof(NV), 0U);
    EXPECT_EQ(static_cast<void*>(aligned), static_cast<void*>(aligned_nv));
  }
}

TEST(Alignment, NextAlignedPtr) {
  TestNextAlignedPtr<uint8_t, 1>();
  TestNextAlignedPtr<uint8_t, 5>();
  TestNextAlignedPtr<uint8_t, 100>();
  TestNextAlignedPtr<uint16_t, 1>();
  TestNextAlignedPtr<uint16_t, 5>();
  TestNextAlignedPtr<uint16_t, 100>();
  TestNextAlignedPtr<float, 1>();
  TestNextAlignedPtr<float, 1>();
  TestNextAlignedPtr<float, 5>();
  TestNextAlignedPtr<double, 5>();
  TestNextAlignedPtr<double, 100>();
  TestNextAlignedPtr<double, 100>();
}

}  // namespace
