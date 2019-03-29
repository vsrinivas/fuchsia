// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/console/string_formatters.h"

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

TEST(GetLittleEndianHexOutput, Lenghts) {
  Err err;
  std::string out;
  std::vector<uint8_t> data;

  err = GetLittleEndianHexOutput(data, &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ(err.msg(), "Invalid size for hex printing: 0");

  data = CreateData(1);
  err = GetLittleEndianHexOutput(data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(out, "00000001");

  data = CreateData(2);
  err = GetLittleEndianHexOutput(data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(out, "00000102");

  data = CreateData(3);
  err = GetLittleEndianHexOutput(data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(out, "00010203");

  data = CreateData(4);
  err = GetLittleEndianHexOutput(data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(out, "01020304");

  data = CreateData(5);
  err = GetLittleEndianHexOutput(data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(out, "00000001 02030405");

  data = CreateData(6);
  err = GetLittleEndianHexOutput(data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(out, "00000102 03040506");

  data = CreateData(8);
  err = GetLittleEndianHexOutput(data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(out, "01020304 05060708");

  data = CreateData(10);
  err = GetLittleEndianHexOutput(data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(out, "00000102 03040506 0708090a");

  data = CreateData(12);
  err = GetLittleEndianHexOutput(data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(out, "01020304 05060708 090a0b0c");

  data = CreateData(17);
  err = GetLittleEndianHexOutput(data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(out, "00000001 02030405 06070809 0a0b0c0d 0e0f1011");
}

TEST(GetLittleEndianHexOutput, LimitOutput) {
  Err err;
  std::string out;
  std::vector<uint8_t> data = CreateData(17);

  err = GetLittleEndianHexOutput(data, &out, 4);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(out, "0e0f1011");

  err = GetLittleEndianHexOutput(data, &out, 6);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(out, "0a0b0c0d 0e0f1011");

  err = GetLittleEndianHexOutput(data, &out, 8);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(out, "0a0b0c0d 0e0f1011");

  err = GetLittleEndianHexOutput(data, &out, 12);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(out, "06070809 0a0b0c0d 0e0f1011");
}

}  // namespace zxdb
