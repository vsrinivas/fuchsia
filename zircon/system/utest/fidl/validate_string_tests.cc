// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>

#include <unittest/unittest.h>

#define EXPECT_VALID_STRING(input)                                    \
  {                                                                   \
    const char* bytes = input;                                        \
    uint32_t num_bytes = sizeof(input) - 1;                           \
    EXPECT_EQ(ZX_OK, fidl_validate_string(bytes, num_bytes));         \
  }

#define EXPECT_INVALID_STRING(input, explanation)                     \
  {                                                                   \
    const char* bytes = input;                                        \
    uint32_t num_bytes = sizeof(input) - 1;                           \
    EXPECT_EQ(ZX_ERR_INVALID_ARGS,                                    \
              fidl_validate_string(bytes, num_bytes), explanation);   \
  }

bool safe_on_nullptr() {
  BEGIN_TEST;

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fidl_validate_string(nullptr, 10));

  END_TEST;
}

bool string_with_size_too_big() {
  BEGIN_TEST;

  uint64_t size_too_big = static_cast<uint64_t>(FIDL_MAX_SIZE) + 1;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fidl_validate_string("", size_too_big));

  END_TEST;
}

bool min_max_code_units_and_minus_one_and_plus_one() {
  BEGIN_TEST;

  EXPECT_VALID_STRING("\x00");              // single byte, min: 0
  EXPECT_VALID_STRING("\x7f");              // single byte, max: 127
  EXPECT_VALID_STRING("\xc2\x80");          // two bytes,   min: 128
  EXPECT_VALID_STRING("\xdf\xbf");          // two bytes,   max: 2047
  EXPECT_VALID_STRING("\xe1\x80\x80");      // three bytes, min: 2048
  EXPECT_VALID_STRING("\xef\xbf\xbf");      // three bytes, max: 65535
  EXPECT_VALID_STRING("\xf0\x90\x80\x80");  // four bytes,  min: 65536
  EXPECT_VALID_STRING("\xf4\x8f\xbf\xbf");  // four bytes,  max: 1114111

  EXPECT_INVALID_STRING("\x80", "1 above max single byte");
  EXPECT_INVALID_STRING("\xc2\x7f", "1 below min two bytes");
  EXPECT_INVALID_STRING("\xdf\xc0", "1 above max two bytes");
  EXPECT_INVALID_STRING("\xe1\x80\x7f", "1 below min three bytes");
  EXPECT_INVALID_STRING("\xef\xbf\xc0", "1 above max three bytes");
  EXPECT_INVALID_STRING("\xf0\x80\x80\x80", "1 below min four bytes");
  EXPECT_INVALID_STRING("\xf7\xbf\xbf\xc0", "1 above max four bytes");

  END_TEST;
}

bool invalid_continuations() {
  BEGIN_TEST;

  // 1 test for the first following byte of an initial two byte value not having the high bit.
  EXPECT_VALID_STRING("\xc2\x80");
  EXPECT_INVALID_STRING("\xc2\x7f", "first byte following two byte value not starting with 0b10");

  // 2 tests for the first and second following byte of an initial three byte value not having the high bit set.
  EXPECT_INVALID_STRING("\xe1\x7f\x80", "first byte following three byte value not starting with 0b10");
  EXPECT_INVALID_STRING("\xe1\x80\x7f", "second byte following three byte value not starting with 0b10");

  // 3 tests for the first, second, and third following byte of an initial four byte value not having the high bit set.
  EXPECT_VALID_STRING("\xf0\x90\x80\x80");
  EXPECT_INVALID_STRING("\xf0\x7f\x80\x80", "first byte following four byte value not starting with 0b10");
  EXPECT_INVALID_STRING("\xf0\x90\x7f\x80", "second byte following four byte value not starting with 0b10");
  EXPECT_INVALID_STRING("\xf0\x90\x80\x7f", "third byte following four byte value not starting with 0b10");

  END_TEST;
}

bool only_shortest_encoding_is_valid() {
  BEGIN_TEST;

  // All encodings of slash, only the shortest is valid.
  //
  // For further details, see "code unit" defined to be 'The minimal bit
  // combination that can represent a unit of encoded text for processing or
  // interchange.'
  EXPECT_VALID_STRING("\x2f");
  EXPECT_INVALID_STRING("\xc0\xaf", "slash (2)");
  EXPECT_INVALID_STRING("\xe0\x80\xaf", "slash (3)");
  EXPECT_INVALID_STRING("\xf0\x80\x80\xaf", "slash (4)");

  END_TEST;
}

bool valid_noncharacter_codepoints() {
  BEGIN_TEST;

  EXPECT_VALID_STRING("\xd8\x9d");          // U+061D
  EXPECT_VALID_STRING("\xd7\xb6");          // U+05F6
  EXPECT_VALID_STRING("\xe0\xab\xb4");      // U+0AF4
  EXPECT_VALID_STRING("\xe0\xb1\x92");      // U+0C52
  EXPECT_VALID_STRING("\xf0\x9e\x91\x94");  // U+1E454
  EXPECT_VALID_STRING("\xf0\x9f\xa5\xb8");  // U+1F978

  END_TEST;
}

bool various() {
  BEGIN_TEST;

  EXPECT_VALID_STRING("");
  EXPECT_VALID_STRING("a");
  EXPECT_VALID_STRING("€"); // \xe2\x82\xac

  // Mix and match from min_max_code_units_and_minus_one_and_plus_one
  EXPECT_VALID_STRING("\x00\xf4\x8f\xbf\xbf\x7f\xf0\x90\x80\x80\xc2\x80");
  EXPECT_VALID_STRING("\xdf\xbf\xef\xbf\xbf\xe1\x80\x80");

  // UTF-8 BOM
  EXPECT_VALID_STRING("\xef\xbb\xbf");
  EXPECT_INVALID_STRING("\xef", "Partial UTF-8 BOM (1)");
  EXPECT_INVALID_STRING("\xef\xbb", "Partial UTF-8 BOM (2)");

  EXPECT_INVALID_STRING("\xdf\x80\x80", "invalid partial sequence");
  EXPECT_INVALID_STRING("\xe0\x80\x80", "long U+0000, non shortest form");
  EXPECT_VALID_STRING("\xe1\x80\x80");

  // All the following test cases are taken from Chromium's
  // streaming_utf8_validator_unittest.cc
  //
  // Some are duplicative to other tests, and have been kept to ease
  // comparison and translation of the tests.

  EXPECT_VALID_STRING("\r");
  EXPECT_VALID_STRING("\n");
  EXPECT_VALID_STRING("a");
  EXPECT_VALID_STRING("\xc2\x81");
  EXPECT_VALID_STRING("\xe1\x80\xbf");
  EXPECT_VALID_STRING("\xf1\x80\xa0\xbf");
  EXPECT_VALID_STRING("\xef\xbb\xbf");  // UTF-8 BOM

  // always invalid bytes
  EXPECT_INVALID_STRING("\xc0", "");
  EXPECT_INVALID_STRING("\xc1", "");
  EXPECT_INVALID_STRING("\xf5", "");
  EXPECT_INVALID_STRING("\xf6", "");
  EXPECT_INVALID_STRING("\xf7", "");
  EXPECT_INVALID_STRING("\xf8", "");
  EXPECT_INVALID_STRING("\xf9", "");
  EXPECT_INVALID_STRING("\xfa", "");
  EXPECT_INVALID_STRING("\xfb", "");
  EXPECT_INVALID_STRING("\xfc", "");
  EXPECT_INVALID_STRING("\xfd", "");
  EXPECT_INVALID_STRING("\xfe", "");
  EXPECT_INVALID_STRING("\xff", "");

  // surrogate code points
  EXPECT_INVALID_STRING("\xed\xa0\x80", "U+D800, high surrogate, first");
  EXPECT_INVALID_STRING("\xed\xb0\x80", "low surrogate, first");
  EXPECT_INVALID_STRING("\xed\xbf\xbf", "low surrogate, last");

  // overlong sequences
  EXPECT_INVALID_STRING("\xc0\x80", "U+0000");
  EXPECT_INVALID_STRING("\xc1\x80", "\"A\"");
  EXPECT_INVALID_STRING("\xc1\x81", "\"B\"");
  EXPECT_INVALID_STRING("\xe0\x80\x80", "U+0000");
  EXPECT_INVALID_STRING("\xe0\x82\x80", "U+0080");
  EXPECT_INVALID_STRING("\xe0\x9f\xbf", "U+07ff");
  EXPECT_INVALID_STRING("\xf0\x80\x80\x8D", "U+000D");
  EXPECT_INVALID_STRING("\xf0\x80\x82\x91", "U+0091");
  EXPECT_INVALID_STRING("\xf0\x80\xa0\x80", "U+0800");
  EXPECT_INVALID_STRING("\xf0\x8f\xbb\xbf", "U+FEFF (BOM)");
  EXPECT_INVALID_STRING("\xf8\x80\x80\x80\xbf", "U+003F");
  EXPECT_INVALID_STRING("\xfc\x80\x80\x80\xa0\xa5", "");

  // Beyond U+10FFFF
  EXPECT_INVALID_STRING("\xf4\x90\x80\x80", "U+110000");
  EXPECT_INVALID_STRING("\xf8\xa0\xbf\x80\xbf", "5 bytes");
  EXPECT_INVALID_STRING("\xfc\x9c\xbf\x80\xbf\x80", "6 bytes");

  // BOMs in UTF-16(BE|LE)
  EXPECT_INVALID_STRING("\xfe\xff", "BOMs in UTF-16 BE");
  EXPECT_INVALID_STRING("\xff\xfe", "BOMs in UTF-16 LE");

  END_TEST;
}

BEGIN_TEST_CASE(validate_string)
RUN_TEST(safe_on_nullptr)
RUN_TEST(string_with_size_too_big)
RUN_TEST(min_max_code_units_and_minus_one_and_plus_one)
RUN_TEST(invalid_continuations)
RUN_TEST(only_shortest_encoding_is_valid)
RUN_TEST(valid_noncharacter_codepoints)
RUN_TEST(various)
END_TEST_CASE(validate_string)
