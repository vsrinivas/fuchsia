// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/nop.h>

#include <string_view>

#include <gtest/gtest.h>

namespace {

#define EXPECT_BYTES_EQ(expected, actual, size)              \
  EXPECT_EQ(std::basic_string_view<uint8_t>(expected, size), \
            std::basic_string_view<uint8_t>(actual, size))

cpp20::span<std::byte> AsBytes(uint8_t* ptr, size_t size) {
  return {reinterpret_cast<std::byte*>(ptr), size};
}

TEST(NopFillTests, Arm64) {
  constexpr size_t kInsnSize = 4;

  // 1 instruction.
  {
    constexpr size_t kSize = kInsnSize;
    uint8_t expected[kSize] = {0x1f, 0x20, 0x03, 0xd5};
    uint8_t actual[kSize];
    arch::NopFill<arch::Arm64NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 2 instructions.
  {
    constexpr size_t kSize = 2 * kInsnSize;
    uint8_t expected[kSize] = {
        0x1f, 0x20, 0x03, 0xd5,  //
        0x1f, 0x20, 0x03, 0xd5,
    };
    uint8_t actual[kSize];
    arch::NopFill<arch::Arm64NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 5 instructions.
  {
    constexpr size_t kSize = 5 * kInsnSize;
    uint8_t expected[kSize] = {
        0x1f, 0x20, 0x03, 0xd5,  //
        0x1f, 0x20, 0x03, 0xd5,  //
        0x1f, 0x20, 0x03, 0xd5,  //
        0x1f, 0x20, 0x03, 0xd5,  //
        0x1f, 0x20, 0x03, 0xd5,
    };
    uint8_t actual[kSize];
    arch::NopFill<arch::Arm64NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 10 instructions.
  {
    constexpr size_t kSize = 10 * kInsnSize;
    uint8_t expected[kSize] = {
        0x1f, 0x20, 0x03, 0xd5,  //
        0x1f, 0x20, 0x03, 0xd5,  //
        0x1f, 0x20, 0x03, 0xd5,  //
        0x1f, 0x20, 0x03, 0xd5,  //
        0x1f, 0x20, 0x03, 0xd5,  //
        0x1f, 0x20, 0x03, 0xd5,  //
        0x1f, 0x20, 0x03, 0xd5,  //
        0x1f, 0x20, 0x03, 0xd5,  //
        0x1f, 0x20, 0x03, 0xd5,  //
        0x1f, 0x20, 0x03, 0xd5,
    };
    uint8_t actual[kSize];
    arch::NopFill<arch::Arm64NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }
}

TEST(NopFillTests, X86) {
  constexpr size_t kInsnSize = 1;

  // 1 instruction.
  {
    constexpr size_t kSize = kInsnSize;
    uint8_t expected[kSize] = {0x90};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 2 instructions.
  {
    constexpr size_t kSize = 2 * kInsnSize;
    uint8_t expected[kSize] = {0x66, 0x90};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 3 instructions.
  {
    constexpr size_t kSize = 3 * kInsnSize;
    uint8_t expected[kSize] = {0x0f, 0x1f, 0x00};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 4 instructions.
  {
    constexpr size_t kSize = 4 * kInsnSize;
    uint8_t expected[kSize] = {0x0f, 0x1f, 0x40, 0x00};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 5 instructions.
  {
    constexpr size_t kSize = 5 * kInsnSize;
    uint8_t expected[kSize] = {0x0f, 0x1f, 0x44, 0x00, 0x00};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 6 instructions.
  {
    constexpr size_t kSize = 6 * kInsnSize;
    uint8_t expected[kSize] = {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 7 instructions.
  {
    constexpr size_t kSize = 7 * kInsnSize;
    uint8_t expected[kSize] = {0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 8 instructions.
  {
    constexpr size_t kSize = 8 * kInsnSize;
    uint8_t expected[kSize] = {0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 9 instructions.
  {
    constexpr size_t kSize = 9 * kInsnSize;
    uint8_t expected[kSize] = {0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 10 instructions.
  {
    constexpr size_t kSize = 10 * kInsnSize;
    uint8_t expected[kSize] = {0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 11 instructions.
  {
    constexpr size_t kSize = 11 * kInsnSize;
    uint8_t expected[kSize] = {0x66, 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 12 instructions.
  {
    constexpr size_t kSize = 12 * kInsnSize;
    uint8_t expected[kSize] = {0x66, 0x66, 0x66, 0x66, 0x0f, 0x1f,
                               0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 13 instructions.
  {
    constexpr size_t kSize = 13 * kInsnSize;
    uint8_t expected[kSize] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x0f, 0x1f,
                               0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 14 instructions.
  {
    constexpr size_t kSize = 14 * kInsnSize;
    uint8_t expected[kSize] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x0f,
                               0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 15 instructions.
  {
    constexpr size_t kSize = 15 * kInsnSize;
    uint8_t expected[kSize] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x0f,
                               0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }

  // 50 instructions.
  {
    constexpr size_t kSize = 50 * kInsnSize;
    uint8_t expected[kSize] = {
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x0f,
        0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,  // Size-15 nop.
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x0f,
        0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,  // Size-15 nop.
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x0f,
        0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,  // Size-15 nop.
        0x0f, 0x1f, 0x44, 0x00, 0x00,              // Size-5 nop.
    };
    uint8_t actual[kSize];
    arch::NopFill<arch::X86NopTraits>(AsBytes(actual, kSize));
    EXPECT_BYTES_EQ(expected, actual, kSize);
  }
}

}  // namespace
