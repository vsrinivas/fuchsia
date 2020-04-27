// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "pts_manager.h"

// This particular test could reasonably be made to run in a non-driver process, but keeping it with
// the rest of the driver's unit tests for now.  This keeps the unit tests together, avoids adding
// a whole binary just for this test code, and lets any LOG() macro output work normally for easier
// test failure diagnosis.
class PtsManagerUnitTest {
 public:
  static void SetLookupBitWidth() {
    // H264Decoder uses 28.  Vp9Decoder uses 32.
    uint32_t bit_widths[] = {28, 32};
    for (uint32_t bit_width : bit_widths) {
      PtsManager pts_manager;
      pts_manager.SetLookupBitWidth(bit_width);
      uint64_t only_offset = static_cast<uint64_t>(1) << 63;
      only_offset += (static_cast<uint64_t>(1) << bit_width) - 1;
      pts_manager.InsertPts(only_offset, /*has_pts=*/true, 42);
      PtsManager::LookupResult result1 = pts_manager.Lookup(0);

      // Without bit width extension, the lookup would see that all offsets are > 0, and not return
      // anything, but instead the 0 is interpreted as being an overflow back to 0 from all FFs of
      // width bit_width.
      EXPECT_TRUE(result1.has_pts());
      EXPECT_EQ(42u, result1.pts());

      // Without bit width extension, the lookup would see that all offsets are > than this value,
      // and not return anything.  As is, the only_offset is logically == to this value, despite
      // this value lacking the top order 1 bit that only_offset has.
      PtsManager::LookupResult result2 =
          pts_manager.Lookup((static_cast<uint64_t>(1) << bit_width) - 1);
      EXPECT_TRUE(result2.has_pts());
      EXPECT_EQ(42u, result2.pts());

      // This value is logically below only_offset, so should not return anything.
      PtsManager::LookupResult result3 =
          pts_manager.Lookup((static_cast<uint64_t>(1) << bit_width) - 2);
      EXPECT_FALSE(result3.has_pts());
    }
  }
};

TEST(PtsManagerUnitTest, SetLookupBitWidth) { PtsManagerUnitTest::SetLookupBitWidth(); }
