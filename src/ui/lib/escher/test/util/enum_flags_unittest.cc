// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include "src/ui/lib/escher/util/enum_flags.h"

#include <array>

#include <gtest/gtest.h>

#include "src/ui/lib/escher/impl/vulkan_utils.h"

namespace escher {

// Simple enum class with only a few members
// in sequential order with no gaps.
enum class TestEnumBits : uint8_t {
  test1 = 1,
  test2 = 2,
  test3 = 4,
  kAllFlags = 7,  // test1 | test2 | test3
};
ESCHER_DECLARE_ENUM_FLAGS(TestEnumFlags, TestEnumBits);

// More complex enum class with many more members
// but likewise in sequential order with no gaps.
enum class LargeEnumBits : uint16_t {
  large1 = 1U << 0,
  large2 = 1U << 1,
  large3 = 1U << 2,
  large4 = 1U << 3,
  large5 = 1U << 4,
  large6 = 1U << 5,
  large7 = 1U << 6,
  large8 = 1U << 7,
  large9 = 1U << 8,
  large10 = 1U << 9,
  large11 = 1U << 10,
  large12 = 1U << 11,
  large13 = 1U << 12,
  kAllFlags = 0x1FFF,
};
ESCHER_DECLARE_ENUM_FLAGS(LargeEnumFlags, LargeEnumBits);

// Enum class where there are gaps between members.
enum class SparseEnumBits : uint32_t {
  sparse1 = 1U << 2,
  sparse2 = 1U << 5,
  sparse3 = 1U << 9,
  sparse4 = 1U << 12,
  sparse5 = 1U << 17,
  sparse6 = 1U << 29,
  kAllFlags = sparse1 | sparse2 | sparse3 | sparse4 | sparse5 | sparse6,
};
ESCHER_DECLARE_ENUM_FLAGS(SparseEnumFlags, SparseEnumBits);

// Arrays to iterate over during testing.
static const std::array<LargeEnumBits, 13> large_enum_array = {
    LargeEnumBits::large1,  LargeEnumBits::large2,  LargeEnumBits::large3,  LargeEnumBits::large4,
    LargeEnumBits::large5,  LargeEnumBits::large6,  LargeEnumBits::large7,  LargeEnumBits::large8,
    LargeEnumBits::large9,  LargeEnumBits::large10, LargeEnumBits::large11, LargeEnumBits::large12,
    LargeEnumBits::large13,
};

static const std::array<SparseEnumBits, 6> sparse_enum_array = {
    SparseEnumBits::sparse1, SparseEnumBits::sparse2, SparseEnumBits::sparse3,
    SparseEnumBits::sparse4, SparseEnumBits::sparse5, SparseEnumBits::sparse6,
};

}  // namespace escher

// Basic test to make sure we can actually construct
// and assign the various enum classes.
TEST(EnumTest, Construction) {
  using namespace escher;
  TestEnumFlags flags;
  LargeEnumFlags large_flags;
  SparseEnumFlags sparse_flags;

  // Assert masks have the correct C++ type.
  static_assert(std::is_same<TestEnumFlags::MaskType, uint8_t>::value, "MaskType Mismatch");
  static_assert(std::is_same<LargeEnumFlags::MaskType, uint16_t>::value, "Masktype Mismatch");
  static_assert(std::is_same<SparseEnumFlags::MaskType, uint32_t>::value, "Masktype Mismatch");

  // Assert default mask value is 0.
  EXPECT_TRUE(TestEnumFlags::MaskType(flags) == 0);
  EXPECT_TRUE(LargeEnumFlags::MaskType(large_flags) == 0);
  EXPECT_TRUE(SparseEnumFlags::MaskType(sparse_flags) == 0);

  // Assert that construction with an argument results in a
  // flag with a value equal to the argument that was passed in.
  flags = TestEnumFlags(TestEnumBits::test1);
  EXPECT_TRUE(TestEnumFlags::MaskType(flags) == static_cast<uint8_t>(TestEnumBits::test1));

  large_flags = LargeEnumFlags(LargeEnumBits::large7);
  EXPECT_TRUE(LargeEnumFlags::MaskType(large_flags) ==
              static_cast<uint16_t>(LargeEnumBits::large7));

  sparse_flags = SparseEnumFlags(SparseEnumBits::sparse3);
  EXPECT_TRUE(SparseEnumFlags::MaskType(sparse_flags) ==
              static_cast<uint32_t>(SparseEnumBits::sparse3));
}

// Tests to see if the bitwise or (|) operator is working as intended.
TEST(EnumTest, BitwiseORTest) {
  using namespace escher;
  TestEnumFlags flags = TestEnumBits::test1 | TestEnumBits::test2;

  EXPECT_TRUE(TestEnumFlags::MaskType(flags) == static_cast<uint8_t>(TestEnumBits::test1) +
                                                    static_cast<uint8_t>(TestEnumBits::test2));

  flags |= TestEnumBits::test3;
  EXPECT_TRUE(flags == TestEnumFlags(TestEnumBits::kAllFlags));

  for (uint64_t i = 0; i < large_enum_array.size() - 1; i++) {
    LargeEnumFlags flag1(large_enum_array[i]);
    LargeEnumFlags flag2(large_enum_array[i + 1]);
    LargeEnumFlags result = flag1 | flag2;
    EXPECT_TRUE(LargeEnumFlags::MaskType(result) ==
                (LargeEnumFlags::MaskType(flag1) | LargeEnumFlags::MaskType(flag2)));
  }

  // Or-ing all the bits together should equal 'kAllFlags'.
  LargeEnumFlags large_result;
  for (uint64_t i = 0; i < large_enum_array.size(); i++) {
    large_result |= large_enum_array[i];
  }
  EXPECT_TRUE(large_result == LargeEnumFlags(LargeEnumBits::kAllFlags));

  SparseEnumFlags sparse_result;
  for (uint64_t i = 0; i < sparse_enum_array.size(); i++) {
    sparse_result |= sparse_enum_array[i];
  }
  EXPECT_TRUE(sparse_result == SparseEnumFlags(SparseEnumBits::kAllFlags));
}

// Tests to see if the bitwise and (&) operators are working as intended.
TEST(EnumTest, BitwiseANDTest) {
  using namespace escher;
  TestEnumFlags flags;
  LargeEnumFlags large_flags;
  SparseEnumFlags sparse_flags;

  // A flag & itself should result in the flag, a flag & another flag should
  // always result in a mask tht equals 0.
  for (uint64_t i = 0; i < large_enum_array.size() - 1; i++) {
    LargeEnumFlags flag1(large_enum_array[i]);
    LargeEnumFlags flag2(large_enum_array[i + 1]);
    LargeEnumFlags result1 = flag1 & flag1;
    LargeEnumFlags result2 = flag1 & flag2;
    EXPECT_TRUE(LargeEnumFlags::MaskType(result1) == static_cast<uint16_t>(flag1));
    EXPECT_TRUE(LargeEnumFlags::MaskType(result2) == 0);
  }

  for (uint64_t i = 0; i < sparse_enum_array.size() - 1; i++) {
    SparseEnumFlags flag1(sparse_enum_array[i]);
    SparseEnumFlags flag2(sparse_enum_array[i + 1]);
    SparseEnumFlags result1 = flag1 & flag1;
    SparseEnumFlags result2 = flag1 & flag2;
    EXPECT_TRUE(SparseEnumFlags::MaskType(result1) == static_cast<uint32_t>(flag1));
    EXPECT_TRUE(SparseEnumFlags::MaskType(result2) == 0);
  }
}

// Tests to see if the bitwise XOR (^) operators are working as intended.
TEST(EnumTest, BitwiseXORTest) {
  using namespace escher;
  TestEnumFlags flags;
  LargeEnumFlags large_flags;
  SparseEnumFlags sparse_flags;

  // Should be equal to zero after all of these operations.
  flags = TestEnumFlags(TestEnumBits::kAllFlags);
  flags ^= TestEnumBits::test1;
  flags ^= TestEnumBits::test2;
  flags ^= TestEnumBits::test3;
  EXPECT_TRUE(flags == TestEnumFlags());

  flags = TestEnumFlags();
  flags ^= TestEnumBits::test1;
  flags ^= TestEnumBits::test2;
  flags ^= TestEnumBits::test3;
  EXPECT_TRUE(flags == TestEnumFlags(TestEnumBits::kAllFlags));

  for (uint64_t i = 0; i < large_enum_array.size(); i++) {
    large_flags ^= large_enum_array[i];
  }
  EXPECT_TRUE(large_flags == LargeEnumFlags(LargeEnumBits::kAllFlags));

  for (uint64_t i = 0; i < sparse_enum_array.size(); i++) {
    sparse_flags ^= sparse_enum_array[i];
  }
  EXPECT_TRUE(sparse_flags == SparseEnumFlags(SparseEnumBits::kAllFlags));
}

// Tests that do more complicated and mixed operations.
TEST(EnumTest, StressTest) {
  using namespace escher;
  TestEnumFlags flags;
  LargeEnumFlags large_flags;
  SparseEnumFlags sparse_flags;

  flags = ~flags;
  large_flags = ~large_flags;
  sparse_flags = ~sparse_flags;

  EXPECT_TRUE(flags == TestEnumFlags(TestEnumBits::kAllFlags));
  EXPECT_TRUE(large_flags == LargeEnumFlags(LargeEnumBits::kAllFlags));
  EXPECT_TRUE(sparse_flags == SparseEnumFlags(SparseEnumBits::kAllFlags));

  large_flags = (LargeEnumBits::large1 | LargeEnumBits::large3 | LargeEnumBits::large5 |
                 LargeEnumBits::large7 | LargeEnumBits::large9 | LargeEnumBits::large11 |
                 LargeEnumBits::large13) ^
                LargeEnumBits::kAllFlags;
  LargeEnumFlags large_result = LargeEnumBits::large2 | LargeEnumBits::large4 |
                                LargeEnumBits::large6 | LargeEnumBits::large8 |
                                LargeEnumBits::large10 | LargeEnumBits::large12;
  EXPECT_TRUE(large_flags == large_result);

  // Mix and match a bunch of different operations.
  sparse_flags = (SparseEnumBits::sparse4 | SparseEnumBits::sparse6) &
                 (~(SparseEnumBits::sparse4 | SparseEnumBits::sparse3));
  EXPECT_TRUE(sparse_flags == SparseEnumFlags(SparseEnumBits::sparse6));

  sparse_flags = (~SparseEnumFlags(SparseEnumBits::sparse3) ^
                  SparseEnumFlags(SparseEnumBits::sparse1 | SparseEnumBits::sparse5));
  EXPECT_TRUE(sparse_flags == SparseEnumFlags(SparseEnumBits::sparse2 | SparseEnumBits::sparse4 |
                                              SparseEnumBits::sparse6));
}
