// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utf_conversion.h"

#include <gtest/gtest.h>

namespace gigaboot {

namespace {

TEST(UtfConversion, Utf16To8) {
  uint8_t out[64];
  memset(out, 0xAA, sizeof(out));
  size_t out_size = sizeof(out);

  ASSERT_EQ(utf16_to_utf8(reinterpret_cast<const uint16_t*>(u"foobar 123"), 10, out, &out_size),
            ZX_OK);

  EXPECT_EQ(out_size, 10u);
  // Output should not be null-terminated when the input isn't.
  EXPECT_EQ(memcmp(out, "foobar 123\xAA", 11), 0);
}

TEST(UtfConversion, Utf16To8WithNullTerminator) {
  uint8_t out[64];
  memset(out, 0xAA, sizeof(out));
  size_t out_size = sizeof(out);

  ASSERT_EQ(utf16_to_utf8(reinterpret_cast<const uint16_t*>(u"foobar 123"), 11, out, &out_size),
            ZX_OK);

  EXPECT_EQ(out_size, 11u);
  // Output should be null-terminated when the input is.
  EXPECT_EQ(memcmp(out, "foobar 123\0", 11), 0);
}

TEST(UtfConversion, Utf16To8QuerySize) {
  size_t out_size = 0;
  ASSERT_EQ(utf16_to_utf8(reinterpret_cast<const uint16_t*>(u"foobar 123"), 10, nullptr, &out_size),
            ZX_OK);

  EXPECT_EQ(out_size, 10u);
}

TEST(UtfConversion, Utf16To8ShortBuffer) {
  uint8_t out[64];
  memset(out, 0xAA, sizeof(out));
  // Pretend our out buffer is only 4 bytes.
  size_t out_size = 4;

  ASSERT_EQ(utf16_to_utf8(reinterpret_cast<const uint16_t*>(u"foobar 123"), 10, out, &out_size),
            ZX_OK);

  // Resulting size should be how many bytes we would have needed, but only
  // the given buffer space should have been written.
  EXPECT_EQ(out_size, 10u);
  EXPECT_EQ(memcmp(out, "foob\xAA", 5), 0);
}

TEST(UtfConversion, Utf16To8InvalidLowSurrogate) {
  uint8_t out[64];
  memset(out, 0xAA, sizeof(out));
  size_t out_size = sizeof(out);

  ASSERT_EQ(utf16_to_utf8(reinterpret_cast<const uint16_t*>(u"foo \xDC00 bar"), 9, out, &out_size),
            ZX_OK);

  EXPECT_EQ(out_size, 11u);
  // EF-BF-BD is the UTF-8 encoding for the Unicode replacement character.
  EXPECT_EQ(memcmp(out, "foo \xEF\xBF\xBD bar", 11), 0);
}

}  // namespace

}  // namespace gigaboot
