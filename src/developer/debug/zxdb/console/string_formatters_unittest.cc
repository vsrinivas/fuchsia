// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/string_formatters.h"

#include <gtest/gtest.h>

namespace zxdb {

std::vector<uint8_t> CreateData(size_t length) {
  std::vector<uint8_t> data;
  data.reserve(length);
  // So that we get the number backwards (0x0102...).
  uint8_t base = length;
  for (size_t i = 0; i < length; i++) {
    data.emplace_back(base - i);
  }
  return data;
}

TEST(GetLittleEndianHexOutput, Lengths) {
  EXPECT_EQ("", GetLittleEndianHexOutput(containers::array_view<uint8_t>()));

  EXPECT_EQ("00000001", GetLittleEndianHexOutput(CreateData(1)));
  EXPECT_EQ("00000102", GetLittleEndianHexOutput(CreateData(2)));
  EXPECT_EQ("00010203", GetLittleEndianHexOutput(CreateData(3)));
  EXPECT_EQ("01020304", GetLittleEndianHexOutput(CreateData(4)));
  EXPECT_EQ("00000001 02030405", GetLittleEndianHexOutput(CreateData(5)));
  EXPECT_EQ("00000102 03040506", GetLittleEndianHexOutput(CreateData(6)));
  EXPECT_EQ("01020304 05060708", GetLittleEndianHexOutput(CreateData(8)));
  EXPECT_EQ("00000102 03040506 0708090a", GetLittleEndianHexOutput(CreateData(10)));
  EXPECT_EQ("01020304 05060708 090a0b0c", GetLittleEndianHexOutput(CreateData(12)));
  EXPECT_EQ("00000001 02030405 06070809 0a0b0c0d 0e0f1011",
            GetLittleEndianHexOutput(CreateData(17)));
}

}  // namespace zxdb
