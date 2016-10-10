// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <string>

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

  EXPECT_FALSE(DecodeByteString(kInvalidInput1, &result));
  EXPECT_FALSE(DecodeByteString(kInvalidInput2, &result));
  EXPECT_FALSE(DecodeByteString(kInvalidInput3, &result));
  EXPECT_FALSE(DecodeByteString(kInvalidInput4, &result));

  struct {
    const char* str;
    const uint8_t byte;
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
    EXPECT_TRUE(DecodeByteString(kTestCases[i].str, &result));
    EXPECT_EQ(kTestCases[i].byte, result);
  }
}

TEST(UtilTest, EncodeByteString) {
  struct {
    const char* str;
    const uint8_t byte;
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
    char result[2];
    EncodeByteString(kTestCases[i].byte, result);
    EXPECT_EQ(kTestCases[i].str[0], result[0]);
    EXPECT_EQ(kTestCases[i].str[1], result[1]);
  }
}

TEST(UtilTest, BuildErrorPacket) {
  EXPECT_EQ("E01", BuildErrorPacket(ErrorCode::PERM));
  EXPECT_EQ("E02", BuildErrorPacket(ErrorCode::NOENT));
  EXPECT_EQ("E13", BuildErrorPacket(ErrorCode::ACCES));
  EXPECT_EQ("E91", BuildErrorPacket(ErrorCode::NAMETOOLONG));
  EXPECT_EQ("E9999", BuildErrorPacket(ErrorCode::UNKNOWN));
}

TEST(UtilTest, ParseThreadId) {
  bool has_pid;
  int64_t pid, tid;

  constexpr char kInvalid1[] = "";
  constexpr char kInvalid2[] = "hello";
  constexpr char kInvalid3[] = "phello.world";
  constexpr char kInvalid4[] = "p123.world";
  constexpr char kInvalid5[] = "phello.123";

  EXPECT_FALSE(ParseThreadId(kInvalid1, &has_pid, &pid, &tid));
  EXPECT_FALSE(ParseThreadId(kInvalid2, &has_pid, &pid, &tid));
  EXPECT_FALSE(ParseThreadId(kInvalid3, &has_pid, &pid, &tid));
  EXPECT_FALSE(ParseThreadId(kInvalid4, &has_pid, &pid, &tid));
  EXPECT_FALSE(ParseThreadId(kInvalid5, &has_pid, &pid, &tid));

  constexpr char kValid1[] = "0";
  constexpr char kValid2[] = "123";
  constexpr char kValid3[] = "-1";
  constexpr char kValid4[] = "p0.0";
  constexpr char kValid5[] = "p123.-1";
  constexpr char kValid6[] = "p-1.1234";

  EXPECT_TRUE(ParseThreadId(kValid1, &has_pid, &pid, &tid));
  EXPECT_FALSE(has_pid);
  EXPECT_EQ(0, tid);

  EXPECT_TRUE(ParseThreadId(kValid2, &has_pid, &pid, &tid));
  EXPECT_FALSE(has_pid);
  EXPECT_EQ(123, tid);

  EXPECT_TRUE(ParseThreadId(kValid3, &has_pid, &pid, &tid));
  EXPECT_FALSE(has_pid);
  EXPECT_EQ(-1, tid);

  EXPECT_TRUE(ParseThreadId(kValid4, &has_pid, &pid, &tid));
  EXPECT_TRUE(has_pid);
  EXPECT_EQ(0, tid);
  EXPECT_EQ(0, pid);

  EXPECT_TRUE(ParseThreadId(kValid5, &has_pid, &pid, &tid));
  EXPECT_TRUE(has_pid);
  EXPECT_EQ(-1, tid);
  EXPECT_EQ(123, pid);

  EXPECT_TRUE(ParseThreadId(kValid6, &has_pid, &pid, &tid));
  EXPECT_TRUE(has_pid);
  EXPECT_EQ(1234, tid);
  EXPECT_EQ(-1, pid);
}

TEST(UtilTest, VerifyPacket) {
  ftl::StringView result;

#define TEST_PACKET(var, type) EXPECT_##type(VerifyPacket(var, &result))
#define TEST_VALID(var) TEST_PACKET(var, TRUE)
#define TEST_INVALID(var) TEST_PACKET(var, FALSE)
#define TEST_RESULT(expected) EXPECT_EQ(expected, result)

  // Invalid packets
  TEST_INVALID("");                // Empty
  TEST_INVALID("foo");             // No '$'
  TEST_INVALID("$foo");            // No '#'
  TEST_INVALID("$foo#");           // No checksum
  TEST_INVALID("$foo#4");          // No checksum
  TEST_INVALID("$foo#43");         // Wrong checksum
  TEST_INVALID("$foo#4Z");         // Malformed checksum
  TEST_INVALID("$foo#G0");         // Malvormed checksum
  TEST_INVALID("$foo#44$foo#44");  // Malvormed checksum

  // Valid packets
  TEST_VALID("$foo#44");
  TEST_RESULT("foo");
  TEST_VALID("$#00");
  TEST_RESULT("");

#undef TEST_INVALID
#undef TEST_VALID
#undef TEST_PACKET
}

TEST(UtilTest, ExtractParameters) {
  ftl::StringView prefix, params;

#define TEST_PARAMS(packet, expected_prefix, expected_params) \
  ExtractParameters(packet, &prefix, &params);                \
  EXPECT_EQ(expected_prefix, prefix);                         \
  EXPECT_EQ(expected_params, params);

  TEST_PARAMS("foo", "foo", "");
  TEST_PARAMS("foo:", "foo", "");
  TEST_PARAMS("foo:b", "foo", "b");
  TEST_PARAMS("foo:bar", "foo", "bar");

#undef TEST_PARAMS
}

}  // namespace
}  // namespace util
}  // namespace debugserver
