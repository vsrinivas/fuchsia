// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/page-table/arch/arm64/mmu.h"

#include <stdlib.h>

#include <gtest/gtest.h>

namespace page_table::arm64 {

TEST(PageTableLayout, NumLevels) {
  struct TestCase {
    GranuleSize size;
    uint8_t region_size_bits;
    uint8_t expected;
  };
  for (const TestCase& test : (TestCase[]){
           // 4 kiB granules resolve up to 9 bits per level, and 12 bits on the final level.
           {GranuleSize::k4KiB, 48, 4},  // 48 == (9 + 9 + 9 + 9) + 12
           {GranuleSize::k4KiB, 47, 4},  // 47 == (8 + 9 + 9 + 9) + 12
           {GranuleSize::k4KiB, 41, 4},  // 41 == (1 + 9 + 9 + 9) + 12
           {GranuleSize::k4KiB, 39, 3},  // 39 == (    9 + 9 + 9) + 12
           {GranuleSize::k4KiB, 21, 1},  // 21 == (            9) + 12
           {GranuleSize::k4KiB, 13, 1},  // 13 == (            1) + 12

           // 16 kiB granules resolve up to 11 bits per level, and 14 bits on the final level.
           {GranuleSize::k16KiB, 48, 4},  // 48 == (1 + 11 + 11 + 11) + 14
           {GranuleSize::k16KiB, 47, 3},  // 47 == (    11 + 11 + 11) + 14
           {GranuleSize::k16KiB, 15, 1},  // 15 == (               1) + 14

           // 64 kiB granules resolve up to 13 bits per level, and 16 bits on the final level.
           {GranuleSize::k64KiB, 48, 3},  // 48 == (6 + 13 + 13) + 16
           {GranuleSize::k64KiB, 43, 3},  // 43 == (1 + 13 + 13) + 16
           {GranuleSize::k64KiB, 42, 2},  // 42 == (    13 + 13) + 16
           {GranuleSize::k64KiB, 17, 1},  // 17 == (          1) + 16
       }) {
    EXPECT_EQ((PageTableLayout{
                   .granule_size = test.size,
                   .region_size_bits = test.region_size_bits,
               })
                  .NumLevels(),
              test.expected)
        << "Granule size bits: " << static_cast<int>(test.size)
        << ", Region size bits: " << static_cast<int>(test.region_size_bits);
  }
}

TEST(PageTableLayout, AddressSpaceSize) {
  constexpr PageTableLayout full_sized_vspace{
      .granule_size = GranuleSize::k4KiB,
      .region_size_bits = 48,
  };
  EXPECT_EQ(full_sized_vspace.AddressSpaceSize(), 0x1'0000'0000'0000u);
}

TEST(PageTableLayout, FromTcrTtbr0) {
  {
    PageTableLayout settings =
        PageTableLayout::FromTcrTtbr0(arch::ArmTcrEl1{}
                                          .set_tg0(arch::ArmTcrTg0Value::k4KiB)  // 4 kiB granules
                                          .set_t0sz(16)  // ignore first 16 bits of vaddrs
        );
    EXPECT_EQ(settings.granule_size, GranuleSize::k4KiB);
    EXPECT_EQ(settings.region_size_bits, 48u);
  }

  {
    PageTableLayout settings =
        PageTableLayout::FromTcrTtbr0(arch::ArmTcrEl1{}
                                          .set_tg0(arch::ArmTcrTg0Value::k64KiB)  // 64 kiB granules
                                          .set_t0sz(20)  // ignore first 20 bits of vaddrs
        );
    EXPECT_EQ(settings.granule_size, GranuleSize::k64KiB);
    EXPECT_EQ(settings.region_size_bits, 44u);
  }
}

}  // namespace page_table::arm64
