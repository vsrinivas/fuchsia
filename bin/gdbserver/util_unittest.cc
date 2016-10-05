// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include "gtest/gtest.h"

namespace debugserver {
namespace util {
namespace {

const char kInvalidInput1[] = "1G";
const char kInvalidInput2[] = "G1";
const char kInvalidInput3[] = "1g";
const char kInvalidInput4[] = "g1";

const char kByteStr1[] = "01";
const char kByteStr2[] = "0B";
const char kByteStr3[] = "0F";
const char kByteStr4[] = "A0";
const char kByteStr5[] = "A9";
const char kByteStr6[] = "FF";
const char kByteStr7[] = "0b";
const char kByteStr8[] = "0f";
const char kByteStr9[] = "a0";
const char kByteStr10[] = "a9";
const char kByteStr11[] = "ff";

const uint8_t kByte1 = 0x01;
const uint8_t kByte2 = 0x0b;
const uint8_t kByte3 = 0x0f;
const uint8_t kByte4 = 0xa0;
const uint8_t kByte5 = 0xa9;
const uint8_t kByte6 = 0xff;

TEST(UtilTest, DecodeByteString) {
  uint8_t result;

  EXPECT_FALSE(DecodeByteString((const uint8_t*)kInvalidInput1, &result));
  EXPECT_FALSE(DecodeByteString((const uint8_t*)kInvalidInput2, &result));
  EXPECT_FALSE(DecodeByteString((const uint8_t*)kInvalidInput3, &result));
  EXPECT_FALSE(DecodeByteString((const uint8_t*)kInvalidInput4, &result));

  struct {
    const char* str;
    uint8_t byte;
  } kTestCases[] = {
    { kByteStr1, kByte1 },
    { kByteStr2, kByte2 },
    { kByteStr3, kByte3 },
    { kByteStr4, kByte4 },
    { kByteStr5, kByte5 },
    { kByteStr6, kByte6 },
    { kByteStr7, kByte2 },
    { kByteStr8, kByte3 },
    { kByteStr9, kByte4 },
    { kByteStr10, kByte5 },
    { kByteStr11, kByte6 },
    { }
  };

  for (int i = 0; kTestCases[i].str; ++i) {
    EXPECT_TRUE(DecodeByteString((const uint8_t*)kTestCases[i].str, &result));
    EXPECT_EQ(kTestCases[i].byte, result);
  }
}

TEST(UtilTest, EncodeByteString) {
  struct {
    const char* str;
    uint8_t byte;
  } kTestCases[] = {
    { kByteStr1, kByte1 },
    { kByteStr7, kByte2 },
    { kByteStr8, kByte3 },
    { kByteStr9, kByte4 },
    { kByteStr10, kByte5 },
    { kByteStr11, kByte6 },
    { }
  };

  for (int i = 0; kTestCases[i].str; ++i) {
    uint8_t result[2];
    EncodeByteString(kTestCases[i].byte, result);
    EXPECT_EQ(kTestCases[i].str[0], result[0]);
    EXPECT_EQ(kTestCases[i].str[1], result[1]);
  }
}

}  // namespace
}  // namespace util
}  // namespace debugserver
