// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <string>

#include "garnet/lib/debugger_utils/util.h"

#include "gtest/gtest.h"

namespace debugserver {
namespace {

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
  constexpr char kValid2[] = "7b";
  constexpr char kValid3[] = "-1";
  constexpr char kValid4[] = "p0.0";
  constexpr char kValid5[] = "p7b.-1";
  constexpr char kValid6[] = "p-1.4d2";

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
  fxl::StringView result;

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
  TEST_INVALID("$foo#G0");         // Malformed checksum
  TEST_INVALID("$foo#44$foo#44");  // Wrong checksum

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
  fxl::StringView prefix, params;

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

TEST(UtilTest, FindUnescapedChar) {
  constexpr char kChar = '$';
  constexpr char kPacket1[] = "";
  constexpr char kPacket2[] = "$";
  constexpr char kPacket3[] = "}$";
  constexpr char kPacket4[] = "}$$";
  constexpr char kPacket5[] = "}}$";
  constexpr char kPacket6[] = "}}}$";
  constexpr char kPacket7[] = "}}}$$";

  size_t index;
  EXPECT_FALSE(FindUnescapedChar(kChar, kPacket1, &index));
  EXPECT_TRUE(FindUnescapedChar(kChar, kPacket2, &index));
  EXPECT_EQ(0u, index);
  EXPECT_FALSE(FindUnescapedChar(kChar, kPacket3, &index));
  EXPECT_TRUE(FindUnescapedChar(kChar, kPacket4, &index));
  EXPECT_EQ(2u, index);
  EXPECT_TRUE(FindUnescapedChar(kChar, kPacket5, &index));
  EXPECT_EQ(2u, index);
  EXPECT_FALSE(FindUnescapedChar(kChar, kPacket6, &index));
  EXPECT_TRUE(FindUnescapedChar(kChar, kPacket7, &index));
  EXPECT_EQ(4u, index);

  // The escape character itself can not be searched for as "unescaped".
  EXPECT_FALSE(FindUnescapedChar(kEscapeChar, kPacket3, &index));
  EXPECT_FALSE(FindUnescapedChar(kEscapeChar, kPacket4, &index));
  EXPECT_FALSE(FindUnescapedChar(kEscapeChar, kPacket5, &index));
  EXPECT_FALSE(FindUnescapedChar(kEscapeChar, kPacket6, &index));
  EXPECT_FALSE(FindUnescapedChar(kEscapeChar, kPacket7, &index));
}

}  // namespace
}  // namespace debugserver
