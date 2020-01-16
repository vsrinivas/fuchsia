// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/base64url/base64url.h"

#include "gtest/gtest.h"

namespace base64url {
namespace {

TEST(Base64UrlTest, Base64UrlEncode) {
  // These examples are from RFC 4648.
  EXPECT_EQ(Base64UrlEncode(""), "");
  EXPECT_EQ(Base64UrlEncode("f"), "Zg==");
  EXPECT_EQ(Base64UrlEncode("fo"), "Zm8=");
  EXPECT_EQ(Base64UrlEncode("foo"), "Zm9v");
  EXPECT_EQ(Base64UrlEncode("foob"), "Zm9vYg==");
  EXPECT_EQ(Base64UrlEncode("fooba"), "Zm9vYmE=");
  EXPECT_EQ(Base64UrlEncode("foobar"), "Zm9vYmFy");

  // Extra tests for URL safe version.
  EXPECT_EQ(Base64UrlEncode(".s>"), "LnM-");
  EXPECT_EQ(Base64UrlEncode(".s?"), "LnM_");
}

TEST(Base64UrlTest, Base64UrlDecode) {
  std::string decoded;

  // These examples are from RFC 4648.
  EXPECT_TRUE(Base64UrlDecode("", &decoded));
  EXPECT_EQ(decoded, "");
  EXPECT_TRUE(Base64UrlDecode("Zg==", &decoded));
  EXPECT_EQ(decoded, "f");
  EXPECT_TRUE(Base64UrlDecode("Zm8=", &decoded));
  EXPECT_EQ(decoded, "fo");
  EXPECT_TRUE(Base64UrlDecode("Zm9v", &decoded));
  EXPECT_EQ(decoded, "foo");
  EXPECT_TRUE(Base64UrlDecode("Zm9vYg==", &decoded));
  EXPECT_EQ(decoded, "foob");
  EXPECT_TRUE(Base64UrlDecode("Zm9vYmE=", &decoded));
  EXPECT_EQ(decoded, "fooba");
  EXPECT_TRUE(Base64UrlDecode("Zm9vYmFy", &decoded));
  EXPECT_EQ(decoded, "foobar");

  // Extra tests for URL safe version.
  EXPECT_TRUE(Base64UrlDecode("LnM-", &decoded));
  EXPECT_EQ(decoded, ".s>");
  EXPECT_TRUE(Base64UrlDecode("LnM_", &decoded));
  EXPECT_EQ(decoded, ".s?");
}

}  // namespace
}  // namespace base64url
