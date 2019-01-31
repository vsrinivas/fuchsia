// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase/encoding.h"

#include <string>

#include <lib/fxl/random/rand.h>
#include <lib/fxl/strings/utf_codecs.h>
#include <zircon/syscalls.h>

#include "gtest/gtest.h"

namespace firebase {
namespace {

// Allows to create correct std::strings with \0 bytes inside from C-style
// string constants.
std::string operator"" _s(const char* str, size_t size) {
  return std::string(str, size);
}

// See
// https://www.firebase.com/docs/rest/guide/understanding-data.html#section-limitations
bool IsValidKey(const std::string& s) {
  if (!fxl::IsStringUTF8(s)) {
    return false;
  }

  for (const char& c : s) {
    if (static_cast<unsigned char>(c) <= 31) {
      return false;
    }

    if (c == 127) {
      return false;
    }

    if (c == '+' || c == '$' || c == '[' || c == ']' || c == '/' || c == '\"' ||
        c == '\\') {
      return false;
    }
  }
  return true;
}

bool IsValidValue(const std::string s) {
  if (!fxl::IsStringUTF8(s)) {
    return false;
  }

  for (const char& c : s) {
    if (static_cast<unsigned char>(c) <= 31) {
      return false;
    }

    if (c == 127) {
      return false;
    }

    if (c == '\"' || c == '\\') {
      return false;
    }
  }
  return true;
}

TEST(EncodingTest, BackAndForth) {
  std::string s;
  std::string ret_key;
  std::string ret_value;

  s = "";
  EXPECT_TRUE(Decode(EncodeKey(s), &ret_key));
  EXPECT_EQ(s, ret_key);
  EXPECT_TRUE(Decode(EncodeValue(s), &ret_value));
  EXPECT_EQ(s, ret_value);

  s = "abcdef";
  EXPECT_TRUE(Decode(EncodeKey(s), &ret_key));
  EXPECT_EQ(s, ret_key);
  EXPECT_TRUE(Decode(EncodeValue(s), &ret_value));
  EXPECT_EQ(s, ret_value);

  s = "leśna łączka";
  EXPECT_TRUE(Decode(EncodeKey(s), &ret_key));
  EXPECT_EQ(s, ret_key);
  EXPECT_TRUE(Decode(EncodeValue(s), &ret_value));
  EXPECT_EQ(s, ret_value);

  s = "\x02\x7F";
  EXPECT_TRUE(Decode(EncodeKey(s), &ret_key));
  EXPECT_EQ(s, ret_key);
  EXPECT_TRUE(Decode(EncodeValue(s), &ret_value));
  EXPECT_EQ(s, ret_value);

  s = "\xFF";
  EXPECT_TRUE(Decode(EncodeKey(s), &ret_key));
  EXPECT_EQ(s, ret_key);
  EXPECT_TRUE(Decode(EncodeValue(s), &ret_value));
  EXPECT_EQ(s, ret_value);

  s = "\x01";
  EXPECT_TRUE(Decode(EncodeKey(s), &ret_key));
  EXPECT_EQ(s, ret_key);
  EXPECT_TRUE(Decode(EncodeValue(s), &ret_value));
  EXPECT_EQ(s, ret_value);

  s = "abc\"def\"ghi'jkl'";
  EXPECT_TRUE(Decode(EncodeKey(s), &ret_key));
  EXPECT_EQ(s, ret_key);
  EXPECT_TRUE(Decode(EncodeValue(s), &ret_value));
  EXPECT_EQ(s, ret_value);

  s = "\0\0\0"_s;
  EXPECT_TRUE(Decode(EncodeKey(s), &ret_key));
  EXPECT_EQ(s, ret_key);
  EXPECT_TRUE(Decode(EncodeValue(s), &ret_value));
  EXPECT_EQ(s, ret_value);

  s = "bazinga\0\0\0"_s;
  EXPECT_TRUE(Decode(EncodeKey(s), &ret_key));
  EXPECT_EQ(s, ret_key);
  EXPECT_TRUE(Decode(EncodeValue(s), &ret_value));
  EXPECT_EQ(s, ret_value);

  s = "alice\0bob"_s;
  EXPECT_TRUE(Decode(EncodeKey(s), &ret_key));
  EXPECT_EQ(s, ret_key);
  EXPECT_TRUE(Decode(EncodeValue(s), &ret_value));
  EXPECT_EQ(s, ret_value);

  // Check random sequence of size less or equals 3.
  for (size_t i = 0; i < 10000; ++i) {
    size_t size = fxl::RandUint64() % 4;
    char buffer[size];
    fxl::StringView view(buffer, size);
    zx_cprng_draw(reinterpret_cast<unsigned char*>(buffer), size);
    EXPECT_TRUE(Decode(EncodeKey(view), &ret_key));
    EXPECT_EQ(view, ret_key);
    EXPECT_TRUE(Decode(EncodeValue(view), &ret_value));
    EXPECT_EQ(view, ret_value);
  }
}

TEST(EncodingTest, Keys) {
  EXPECT_EQ("V", EncodeKey(""));
  EXPECT_EQ("abcV", EncodeKey("abc"));
  EXPECT_EQ("qwerty123V", EncodeKey("qwerty123"));
  EXPECT_EQ("YWJjLw==B", EncodeKey("abc/"));
  EXPECT_EQ("I1tdIQ==B", EncodeKey("#[]!"));
  EXPECT_EQ("fw==B", EncodeKey("\x7F"));
  EXPECT_EQ("_w==B", EncodeKey("\xFF"));
  EXPECT_EQ("Ig==B", EncodeKey("\""));
  EXPECT_EQ("Kw==B", EncodeKey("+"));
}

TEST(EncodingTest, Values) {
  EXPECT_EQ("V", EncodeValue(""));
  EXPECT_EQ("abcV", EncodeValue("abc"));
  EXPECT_EQ("qwerty123V", EncodeValue("qwerty123"));
  EXPECT_EQ("abc/V", EncodeValue("abc/"));
  EXPECT_EQ("#[]!V", EncodeValue("#[]!"));
  EXPECT_EQ("fw==B", EncodeValue("\x7F"));
  EXPECT_EQ("_w==B", EncodeValue("\xFF"));
  EXPECT_EQ("Ig==B", EncodeValue("\""));
  EXPECT_EQ("Iy9cIT9bXQ==B", EncodeValue("#/\\!?[]"));
  EXPECT_EQ("+V", EncodeValue("+"));
}

TEST(EncodingTest, ValidKeys) {
  std::string original;
  std::string encoded;
  std::string decoded;

  original = "\x02, \x7F, \x18, \x1D are forbidden, [], $ and / too!";
  encoded = EncodeKey(original);
  EXPECT_TRUE(IsValidKey(encoded));
  EXPECT_TRUE(Decode(encoded, &decoded));
  EXPECT_EQ(original, decoded);

  original = "\xFF";
  encoded = EncodeKey(original);
  EXPECT_TRUE(IsValidKey(encoded));
  EXPECT_TRUE(Decode(encoded, &decoded));
  EXPECT_EQ(original, decoded);

  original = "\xFF\x7F\x05\x09\xFF\xFF\x0B";
  encoded = EncodeKey(original);
  EXPECT_TRUE(IsValidKey(encoded));
  EXPECT_TRUE(Decode(encoded, &decoded));
  EXPECT_EQ(original, decoded);

  original = "zażółć gęślą jaźń\xFF\xFF";
  encoded = EncodeKey(original);
  EXPECT_TRUE(IsValidKey(encoded));
  EXPECT_TRUE(Decode(encoded, &decoded));
  EXPECT_EQ(original, decoded);
}

TEST(EncodingTest, ValidValues) {
  std::string original;
  std::string encoded;
  std::string decoded;

  original = "\x02, \x7F, \x18, \x1D are ok, [], $ and / too!";
  encoded = EncodeValue(original);
  EXPECT_TRUE(IsValidValue(encoded));
  EXPECT_TRUE(Decode(encoded, &decoded));
  EXPECT_EQ(original, decoded);

  original = "\x01\x00\x02"_s;
  encoded = EncodeValue(original);
  EXPECT_TRUE(IsValidValue(encoded));
  EXPECT_TRUE(Decode(encoded, &decoded));
  EXPECT_EQ(original, decoded);

  original = "\xFF";
  encoded = EncodeValue(original);
  EXPECT_TRUE(IsValidValue(encoded));
  EXPECT_TRUE(Decode(encoded, &decoded));
  EXPECT_EQ(original, decoded);

  original = "\xFF\x7F\x05\x09\xFF\xFF\x0B";
  encoded = EncodeValue(original);
  EXPECT_TRUE(IsValidValue(encoded));
  EXPECT_TRUE(Decode(encoded, &decoded));
  EXPECT_EQ(original, decoded);

  original = "zażółć gęślą jaźń\xFF\xFF";
  encoded = EncodeValue(original);
  EXPECT_TRUE(IsValidValue(encoded));
  EXPECT_TRUE(Decode(encoded, &decoded));
  EXPECT_EQ(original, decoded);
}

}  // namespace
}  // namespace firebase
