// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/util/bitmap.h"

#include "gtest/gtest.h"

namespace {
using namespace escher;

void TestBitmap(Bitmap* bm) {
  uint32_t size = bm->GetSize();
  for (uint32_t i = 0; i < size; ++i) {
    EXPECT_FALSE(bm->Get(i));
  }

  constexpr uint32_t kFiftySix = 56;
  constexpr uint32_t kOneHundredTwentyTwo = 122;
  constexpr uint32_t kSevenHundredSixtyEight = 768;

  bm->Set(kFiftySix);
  bm->Set(kOneHundredTwentyTwo);
  bm->Set(kSevenHundredSixtyEight);

  // Test that we managed to set the 3 bits.
  EXPECT_TRUE(bm->Get(kFiftySix));
  EXPECT_TRUE(bm->Get(kOneHundredTwentyTwo));
  EXPECT_TRUE(bm->Get(kSevenHundredSixtyEight));

  // Test that all of the bits that we didn't set are still cleared.
  for (uint32_t i = 0; i < kFiftySix; ++i) {
    EXPECT_FALSE(bm->Get(i));
  }
  for (uint32_t i = kFiftySix + 1; i < kOneHundredTwentyTwo; ++i) {
    EXPECT_FALSE(bm->Get(i));
  }
  for (uint32_t i = kOneHundredTwentyTwo + 1; i < kSevenHundredSixtyEight;
       ++i) {
    EXPECT_FALSE(bm->Get(i));
  }
  for (uint32_t i = kSevenHundredSixtyEight + 1; i < size; ++i) {
    EXPECT_FALSE(bm->Get(i));
  }

  bm->Clear(kFiftySix);
  bm->Clear(kOneHundredTwentyTwo);
  bm->Clear(kSevenHundredSixtyEight);

  EXPECT_FALSE(bm->Get(kFiftySix));
  EXPECT_FALSE(bm->Get(kOneHundredTwentyTwo));
  EXPECT_FALSE(bm->Get(kSevenHundredSixtyEight));

  // Test that all bits are now clear.
  for (uint32_t i = 0; i < size; ++i) {
    EXPECT_FALSE(bm->Get(i));
  }
}

void TestBitsWithinSameWord(Bitmap* bm) {
  ASSERT_GT(bm->GetSize(), 32U);

  for (uint32_t i = 0; i < 32; ++i) {
    EXPECT_FALSE(bm->Get(i));
  }

  bm->Set(7);

  bm->Set(12);
  bm->Set(14);

  bm->Set(25);
  bm->Set(26);
  bm->Set(27);

  EXPECT_FALSE(bm->Get(6));
  EXPECT_TRUE(bm->Get(7));
  EXPECT_FALSE(bm->Get(8));

  EXPECT_FALSE(bm->Get(11));
  EXPECT_TRUE(bm->Get(12));
  EXPECT_FALSE(bm->Get(13));
  EXPECT_TRUE(bm->Get(14));
  EXPECT_FALSE(bm->Get(15));

  EXPECT_FALSE(bm->Get(24));
  EXPECT_TRUE(bm->Get(25));
  EXPECT_TRUE(bm->Get(26));
  EXPECT_TRUE(bm->Get(27));
  EXPECT_FALSE(bm->Get(28));

  bm->Clear(26);

  EXPECT_FALSE(bm->Get(24));
  EXPECT_TRUE(bm->Get(25));
  EXPECT_FALSE(bm->Get(26));
  EXPECT_TRUE(bm->Get(27));
  EXPECT_FALSE(bm->Get(28));
}

TEST(BitMap, Raw) {
  constexpr uint32_t kRawArraySize = 1000;
  uint32_t raw_array[kRawArraySize];

  Bitmap bm(raw_array, kRawArraySize);
  EXPECT_TRUE(bm.GetSize() == 32 * kRawArraySize);
  bm.ClearAll();

  TestBitmap(&bm);
  bm.ClearAll();
  TestBitsWithinSameWord(&bm);
}

TEST(BitMap, WithStorage) {
  BitmapWithStorage bm;
  bm.SetSize(1000);
  EXPECT_TRUE(bm.GetSize() == (1000 / 32 + 1) * 32);

  TestBitmap(&bm);
  bm.ClearAll();
  TestBitsWithinSameWord(&bm);
}

TEST(BitMap, Resize) {
  BitmapWithStorage bm;
  bm.SetSize(30);

  bm.Set(2);
  bm.Set(4);

  auto old_size = bm.GetSize();
  bm.SetSize(1000);
  EXPECT_NE(old_size, bm.GetSize());
  EXPECT_TRUE(bm.Get(2));
  EXPECT_FALSE(bm.Get(3));
  EXPECT_TRUE(bm.Get(4));
}

}  // namespace
