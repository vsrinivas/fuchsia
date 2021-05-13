// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/field.h>
#include <lib/stdcompat/bit.h>

#include <zxtest/zxtest.h>

namespace {

TEST(ElfldltlFieldTests, UnsignedField) {
  struct TestStruct {
    elfldltl::UnsignedField<uint64_t, false> u64;
    elfldltl::UnsignedField<uint32_t, false> u32;
    elfldltl::UnsignedField<uint16_t, false> u16;
    elfldltl::UnsignedField<uint8_t, false> u8[2];
  };
  static_assert(sizeof(TestStruct) == 8 + 4 + 2 + 1 + 1);
  static_assert(alignof(TestStruct) == alignof(uint64_t));

  struct TestData {
    uint64_t u64{0xfeedfacedeadbeef};
    uint32_t u32{0xabc1def2};
    uint16_t u16{0xabcd};
    uint8_t u8[2]{1, 2};
  };
  TestData data;

  TestStruct& s = *reinterpret_cast<TestStruct*>(&data);

  EXPECT_EQ(s.u64.get(), uint64_t{0xfeedfacedeadbeef});
  EXPECT_EQ(s.u64(), uint64_t{0xfeedfacedeadbeef});
  EXPECT_EQ([](uint64_t x) { return x; }(s.u64), uint64_t{0xfeedfacedeadbeef});

  EXPECT_EQ(s.u32.get(), uint32_t{0xabc1def2});
  EXPECT_EQ(s.u32(), uint32_t{0xabc1def2});
  EXPECT_EQ([](uint32_t x) { return x; }(s.u32), uint32_t{0xabc1def2});

  EXPECT_EQ(s.u16.get(), uint16_t{0xabcd});
  EXPECT_EQ(s.u16(), uint16_t{0xabcd});
  EXPECT_EQ([](uint16_t x) { return x; }(s.u16), uint16_t{0xabcd});

  EXPECT_EQ(s.u8[0].get(), uint8_t{1});
  EXPECT_EQ(s.u8[0](), uint8_t{1});
  EXPECT_EQ([](uint8_t x) { return x; }(s.u8[0]), uint8_t{1});

  EXPECT_EQ(s.u8[1].get(), uint8_t{2});
  EXPECT_EQ(s.u8[1](), uint8_t{2});
  EXPECT_EQ([](uint8_t x) { return x; }(s.u8[1]), uint8_t{2});

  s.u64 = 0x1234;
  EXPECT_EQ(data.u64, uint64_t{0x1234});
  EXPECT_EQ(s.u64(), uint64_t{0x1234});

  s.u32 = 0x1234;
  EXPECT_EQ(data.u32, uint32_t{0x1234});
  EXPECT_EQ(s.u32(), uint32_t{0x1234});

  s.u16 = 0x1234;
  EXPECT_EQ(data.u16, uint16_t{0x1234});
  EXPECT_EQ(s.u16(), uint16_t{0x1234});

  s.u8[0] = 0xaa;
  EXPECT_EQ(data.u8[0], uint8_t{0xaa});
  EXPECT_EQ(s.u8[0](), uint8_t{0xaa});
}

TEST(ElfldltlFieldTests, SignedField) {
  struct TestStruct {
    elfldltl::SignedField<uint64_t, false> s64;
    elfldltl::SignedField<uint32_t, false> s32;
    elfldltl::SignedField<uint16_t, false> s16;
    elfldltl::SignedField<uint8_t, false> s8[2];
  };
  static_assert(sizeof(TestStruct) == 8 + 4 + 2 + 1 + 1);
  static_assert(alignof(TestStruct) == alignof(int64_t));

  struct TestData {
    int64_t s64{-1234567890123456789};
    int32_t s32{-123456};
    int16_t s16{-1234};
    int8_t s8[2]{1, -2};
  };
  TestData data;

  TestStruct& s = *reinterpret_cast<TestStruct*>(&data);

  EXPECT_EQ(s.s64.get(), int64_t{-1234567890123456789});
  EXPECT_EQ(s.s64(), int64_t{-1234567890123456789});
  EXPECT_EQ([](int64_t x) { return x; }(s.s64), int64_t{-1234567890123456789});
  EXPECT_LT(s.s64(), int64_t{0});

  EXPECT_EQ(s.s32.get(), int32_t{-123456});
  EXPECT_EQ(s.s32(), int32_t{-123456});
  EXPECT_EQ([](int32_t x) { return x; }(s.s32), int32_t{-123456});
  EXPECT_LT(s.s32(), int32_t{0});

  EXPECT_EQ(s.s16.get(), int16_t{-1234});
  EXPECT_EQ(s.s16(), int16_t{-1234});
  EXPECT_EQ([](int16_t x) { return x; }(s.s16), int16_t{-1234});
  EXPECT_LT(s.s16(), int16_t{0});

  EXPECT_EQ(s.s8[0].get(), int8_t{1});
  EXPECT_EQ(s.s8[0](), int8_t{1});
  EXPECT_EQ([](int8_t x) { return x; }(s.s8[0]), int8_t{1});

  EXPECT_EQ(s.s8[1].get(), int8_t{-2});
  EXPECT_EQ(s.s8[1](), int8_t{-2});
  EXPECT_EQ([](int8_t x) { return x; }(s.s8[1]), int8_t{-2});
  EXPECT_LT(s.s8[1](), int8_t{0});

  s.s64 = -1234;
  EXPECT_EQ(data.s64, int64_t{-1234});
  EXPECT_EQ(s.s64(), int64_t{-1234});

  s.s32 = -1234;
  EXPECT_EQ(data.s32, int32_t{-1234});
  EXPECT_EQ(s.s32(), int32_t{-1234});

  s.s16 = -1234;
  EXPECT_EQ(data.s16, int16_t{-1234});
  EXPECT_EQ(s.s16(), int16_t{-1234});

  s.s8[0] = -123;
  EXPECT_EQ(data.s8[0], int8_t{-123});
  EXPECT_EQ(s.s8[0](), int8_t{-123});
}

TEST(ElfldltlFieldTests, EnumField) {
  enum class E64 : uint64_t { k0 = 0xabcdef123, k1, k2 };
  enum class E32 : uint32_t { k0 = 0xabcd, k1, k2 };
  enum class E16 : uint16_t { k0 = 0xff, k1, k2 };
  enum class E8 : uint8_t { k0, k1, k2 };

  struct TestStruct {
    elfldltl::EnumField<E64, false> e64;
    elfldltl::EnumField<E32, false> e32;
    elfldltl::EnumField<E16, false> e16;
    elfldltl::EnumField<E8, false> e8[2];
  };
  static_assert(sizeof(TestStruct) == 8 + 4 + 2 + 1 + 1);
  static_assert(alignof(TestStruct) == alignof(uint64_t));

  struct TestData {
    uint64_t e64{0xabcdef124};
    uint32_t e32{0xabce};
    uint16_t e16{0x100};
    uint8_t e8[2]{1, 2};
  };
  TestData data;

  TestStruct& s = *reinterpret_cast<TestStruct*>(&data);

  EXPECT_EQ(s.e64.get(), E64::k1);
  EXPECT_EQ(s.e64(), E64::k1);
  EXPECT_EQ([](E64 x) { return x; }(s.e64), E64::k1);
  switch (s.e64) {
    case E64::k1:
      break;
    default:
      EXPECT_TRUE(false, "switch on enum");
  }

  EXPECT_EQ(s.e32.get(), E32::k1);
  EXPECT_EQ(s.e32(), E32::k1);
  EXPECT_EQ([](E32 x) { return x; }(s.e32), E32::k1);
  switch (s.e32) {
    case E32::k1:
      break;
    default:
      EXPECT_TRUE(false, "switch on enum");
  }

  EXPECT_EQ(s.e16.get(), E16::k1);
  EXPECT_EQ(s.e16(), E16::k1);
  EXPECT_EQ([](E16 x) { return x; }(s.e16), E16::k1);
  switch (s.e16) {
    case E16::k1:
      break;
    default:
      EXPECT_TRUE(false, "switch on enum");
  }

  EXPECT_EQ(s.e8[0].get(), E8::k1);
  EXPECT_EQ(s.e8[0](), E8::k1);
  EXPECT_EQ([](E8 x) { return x; }(s.e8[0]), E8::k1);
  switch (s.e8[0]) {
    case E8::k1:
      break;
    default:
      EXPECT_TRUE(false, "switch on enum");
  }

  EXPECT_EQ(s.e8[1].get(), E8::k2);
  EXPECT_EQ(s.e8[1](), E8::k2);
  EXPECT_EQ([](E8 x) { return x; }(s.e8[1]), E8::k2);

  s.e64 = E64::k0;
  EXPECT_EQ(data.e64, static_cast<uint64_t>(E64::k0));
  EXPECT_EQ(s.e64(), E64::k0);

  s.e32 = E32::k0;
  EXPECT_EQ(data.e32, static_cast<uint32_t>(E32::k0));
  EXPECT_EQ(s.e32(), E32::k0);

  s.e16 = E16::k0;
  EXPECT_EQ(data.e16, static_cast<uint16_t>(E16::k0));
  EXPECT_EQ(s.e16(), E16::k0);

  s.e8[0] = E8::k0;
  EXPECT_EQ(data.e8[0], static_cast<uint8_t>(E8::k0));
  EXPECT_EQ(s.e8[0](), E8::k0);
}

TEST(ElfldltlFieldTests, ByteSwap) {
  struct TestStruct {
    elfldltl::UnsignedField<uint64_t, true> u64;
    elfldltl::UnsignedField<uint32_t, true> u32;
    elfldltl::UnsignedField<uint16_t, true> u16;
    elfldltl::UnsignedField<uint8_t, true> u8[2];
  };
  static_assert(sizeof(TestStruct) == 8 + 4 + 2 + 1 + 1);
  static_assert(alignof(TestStruct) == alignof(uint64_t));

  struct TestData {
    uint64_t u64{0xefbeaddecefaedfe};
    uint32_t u32{0xf2dec1ab};
    uint16_t u16{0xcdab};
    uint8_t u8[2]{1, 2};
  };
  TestData data;

  TestStruct& s = *reinterpret_cast<TestStruct*>(&data);

  EXPECT_EQ(s.u64.get(), uint64_t{0xfeedfacedeadbeef});
  EXPECT_EQ(s.u64(), uint64_t{0xfeedfacedeadbeef});
  EXPECT_EQ([](uint64_t x) { return x; }(s.u64), uint64_t{0xfeedfacedeadbeef});

  EXPECT_EQ(s.u32.get(), uint32_t{0xabc1def2});
  EXPECT_EQ(s.u32(), uint32_t{0xabc1def2});
  EXPECT_EQ([](uint32_t x) { return x; }(s.u32), uint32_t{0xabc1def2});

  EXPECT_EQ(s.u16.get(), uint16_t{0xabcd});
  EXPECT_EQ(s.u16(), uint16_t{0xabcd});
  EXPECT_EQ([](uint16_t x) { return x; }(s.u16), uint16_t{0xabcd});

  EXPECT_EQ(s.u8[0].get(), uint8_t{1});
  EXPECT_EQ(s.u8[0](), uint8_t{1});
  EXPECT_EQ([](uint8_t x) { return x; }(s.u8[0]), uint8_t{1});

  EXPECT_EQ(s.u8[1].get(), uint8_t{2});
  EXPECT_EQ(s.u8[1](), uint8_t{2});
  EXPECT_EQ([](uint8_t x) { return x; }(s.u8[1]), uint8_t{2});

  s.u64 = 0x1234;
  EXPECT_EQ(data.u64, uint64_t{0x3412000000000000});
  EXPECT_EQ(s.u64(), uint64_t{0x1234});

  s.u32 = 0x1234;
  EXPECT_EQ(data.u32, uint32_t{0x34120000});
  EXPECT_EQ(s.u32(), uint32_t{0x1234});

  s.u16 = 0x1234;
  EXPECT_EQ(data.u16, uint16_t{0x3412});
  EXPECT_EQ(s.u16(), uint16_t{0x1234});

  s.u8[0] = 0xaa;
  EXPECT_EQ(data.u8[0], uint8_t{0xaa});
  EXPECT_EQ(s.u8[0](), uint8_t{0xaa});

  constexpr bool kLittle = cpp20::endian::native == cpp20::endian::little;
  constexpr elfldltl::UnsignedField<uint32_t, !kLittle> kConst{{4, 3, 2, 1}};
  constexpr elfldltl::UnsignedField<uint32_t, kLittle> kBigConst{{1, 2, 3, 4}};
  constexpr uint32_t kConstVal = kConst;
  constexpr uint32_t kBigConstVal = kBigConst;
  EXPECT_EQ(kConstVal, 0x01020304);
  EXPECT_EQ(kBigConstVal, 0x01020304);
}

}  // namespace
