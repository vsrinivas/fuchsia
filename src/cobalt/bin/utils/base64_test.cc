// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/utils/base64.h"

#include "gtest/gtest.h"

namespace cobalt {
TEST(Base64, EncodeBytes) {
  std::string data = "\x01\x01\x02\x03\x04\x05\x06\xff\xfe\xfd\xfc\xfb\xfa";
  data[0] = '\x00';
  std::string encoded = "AAECAwQFBv/+/fz7+g==";
  EXPECT_EQ(Base64Encode(data), encoded);
}

TEST(Base64, EncodeString) {
  std::string data = "hello world";
  std::string encoded = "aGVsbG8gd29ybGQ=";
  EXPECT_EQ(Base64Encode(data), encoded);
}

TEST(Base64, DecodeValid) {
  std::string data = "hello world";
  std::string encoded = "aGVsbG8gd29ybGQ=";
  EXPECT_EQ(Base64Decode(encoded), data);
}

TEST(Base64, DecodeInvalid) {
  std::string invalid_encoded = "aGVs;G8gd29ybGQ=";
  EXPECT_EQ(Base64Decode(invalid_encoded), "");
}

TEST(Base64, LongStringRoundTrip) {
  const size_t kDataLen = 1024;
  std::string data(kDataLen, '\0');
  for (size_t i = 0; i < kDataLen; i++) {
    data[i] = 'a' + (i % 25);
  }
  std::string encoded = Base64Encode(data);
  EXPECT_EQ(Base64Decode(encoded), data);
}
}  // namespace cobalt
