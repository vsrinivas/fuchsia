// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/unwinder/memory.h"

#include <cstdint>

#include <gtest/gtest.h>

namespace unwinder {

TEST(Memory, Read) {
  LocalMemory mem;

  uint8_t data[] = {0x01, 0x02, 0x03};
  auto p = reinterpret_cast<uint64_t>(data);

  uint8_t u8;
  ASSERT_TRUE(mem.Read(p, u8).ok());
  ASSERT_EQ(1, u8);

  int16_t i16;
  ASSERT_TRUE(mem.Read(p, i16).ok());
  ASSERT_EQ(0x0302, i16);
}

TEST(Memory, ReadULEB128) {
  LocalMemory mem;

  uint8_t data[] = {0xE5, 0x8E, 0x26, 0x04};
  auto p = reinterpret_cast<uint64_t>(data);

  uint64_t res;
  ASSERT_TRUE(mem.ReadULEB128(p, res).ok());

  ASSERT_EQ(624485UL, res);
  ASSERT_EQ(3UL, p - reinterpret_cast<uint64_t>(data));

  ASSERT_TRUE(mem.ReadULEB128(p, res).ok());
  ASSERT_EQ(4UL, res);
}

TEST(Memory, ReadSLEB128) {
  LocalMemory mem;

  uint8_t data[] = {0xC0, 0xBB, 0x78, 0x7F};
  auto p = reinterpret_cast<uint64_t>(data);

  int64_t res;
  ASSERT_TRUE(mem.ReadSLEB128(p, res).ok());

  ASSERT_EQ(-123456L, res);
  ASSERT_EQ(3UL, p - reinterpret_cast<uint64_t>(data));

  ASSERT_TRUE(mem.ReadSLEB128(p, res).ok());
  ASSERT_EQ(-1L, res);
}

TEST(Memory, ReadEncoded) {
  LocalMemory mem;

  uint8_t data[] = {0x7F, 0x02, 0x03, 0x04};
  auto p = reinterpret_cast<uint64_t>(data);

  uint64_t res;
  ASSERT_TRUE(mem.ReadEncoded(p, res, 0x19).ok());
  ASSERT_EQ(reinterpret_cast<uint64_t>(data) - 1, res);

  ASSERT_TRUE(mem.ReadEncoded(p, res, 0x02).ok());
  ASSERT_EQ(0x0302UL, res);

  ASSERT_TRUE(mem.ReadEncoded(p, res, 0x31, 0x1000).ok());
  ASSERT_EQ(0x1004UL, res);
}

}  // namespace unwinder
