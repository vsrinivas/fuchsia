// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/utf-utils/internal/arm-neon.h>
#include <lib/utf-utils/internal/generic-simd.h>
#include <lib/utf-utils/internal/scalar.h>
#include <lib/utf-utils/internal/x86-avx2.h>
#include <lib/utf-utils/internal/x86-ssse3.h>
#include <lib/utf-utils/utf-utils.h>

#include <iostream>
#include <string>

#include <zxtest/zxtest.h>

[[maybe_unused]] constexpr char kTag[] = "[utf-utils-unit-tests] ";

template <bool (*ValidateFn)(const char*, size_t),
          bool (*ValidateAndCopyFn)(const char*, char*, size_t)>
bool TestUtf8(const char* data, size_t size) {
  auto tmp = std::make_unique<char[]>(size);
  bool validate_result = ValidateFn(data, size);
  bool copy_result = ValidateAndCopyFn(data, tmp.get(), size);
  if (copy_result && size > 0) {
    EXPECT_BYTES_EQ(data, tmp.get(), size);
  }

  EXPECT_EQ(validate_result, copy_result);
  return validate_result;
}

bool TestUtf8Scalar(const char* data, size_t size) {
  return TestUtf8<utfutils::internal::IsValidUtf8Scalar,
                  utfutils::internal::ValidateAndCopyUtf8Scalar>(data, size);
}

bool TestUtf8Neon(const char* data, size_t size, bool expectation) {
#ifdef __ARM_NEON
  static bool printed_message = false;
  if (!printed_message) {
    std::cout << kTag << "Testing NEON extensions" << std::endl;
    printed_message = true;
  }
  return TestUtf8<utfutils::internal::IsValidUtf8Simd<utfutils::internal::arm::Neon>,
                  utfutils::internal::ValidateAndCopyUtf8Simd<utfutils::internal::arm::Neon>>(data,
                                                                                              size);
#else
  return expectation;
#endif
}

bool TestUtf8Ssse3(const char* data, size_t size, bool expectation) {
#ifdef __x86_64__
  static bool printed_message = false;
  if (__builtin_cpu_supports("ssse3")) {
    if (!printed_message) {
      std::cout << kTag << "Testing SSSE3 extensions" << std::endl;
      printed_message = true;
    }
    return TestUtf8<utfutils::internal::IsValidUtf8Simd<utfutils::internal::x86::Ssse3>,
                    utfutils::internal::ValidateAndCopyUtf8Simd<utfutils::internal::x86::Ssse3>>(
        data, size);
  }

  return expectation;
#else
  return expectation;
#endif
}

bool TestUtf8Avx2(const char* data, size_t size, bool expectation) {
#ifdef __x86_64__
  static bool printed_message = false;

  if (__builtin_cpu_supports("avx2")) {
    if (!printed_message) {
      std::cout << kTag << "Testing AVX2 extensions" << std::endl;
      printed_message = true;
    }
    return TestUtf8<utfutils::internal::IsValidUtf8Simd<utfutils::internal::x86::Avx2>,
                    utfutils::internal::ValidateAndCopyUtf8Simd<utfutils::internal::x86::Avx2>>(
        data, size);
  }

  return expectation;
#else
  return expectation;
#endif
}

constexpr size_t kTestVectorBoundaries[] = {7, 13, 14, 15, 29, 30, 31, 61, 62, 63, 79, 95, 127};

#define EXPECT_VALID_STRING_HELPER(bytes, num_bytes)           \
  {                                                            \
    EXPECT_TRUE(utfutils_is_valid_utf8((bytes), (num_bytes))); \
    EXPECT_TRUE(TestUtf8Scalar((bytes), (num_bytes)));         \
    EXPECT_TRUE(TestUtf8Neon((bytes), (num_bytes), true));     \
    EXPECT_TRUE(TestUtf8Ssse3((bytes), (num_bytes), true));    \
    EXPECT_TRUE(TestUtf8Avx2((bytes), (num_bytes), true));     \
  }

#define EXPECT_INVALID_STRING_HELPER(bytes, num_bytes, explanation)          \
  {                                                                          \
    EXPECT_FALSE(utfutils_is_valid_utf8((bytes), (num_bytes)), explanation); \
    EXPECT_FALSE(TestUtf8Scalar((bytes), (num_bytes)), explanation);         \
    EXPECT_FALSE(TestUtf8Neon((bytes), (num_bytes), false), explanation);    \
    EXPECT_FALSE(TestUtf8Ssse3((bytes), (num_bytes), false), explanation);   \
    EXPECT_FALSE(TestUtf8Avx2((bytes), (num_bytes), false), explanation);    \
  }

#define EXPECT_VALID_STRING(input)                                                          \
  {                                                                                         \
    const char* bytes = input;                                                              \
    size_t num_bytes = sizeof(input) - 1;                                                   \
    EXPECT_VALID_STRING_HELPER(bytes, num_bytes);                                           \
    for (size_t prefix_size : kTestVectorBoundaries) {                                      \
      std::string tmp = std::string(prefix_size, '\r') + std::string((bytes), (num_bytes)); \
      EXPECT_VALID_STRING_HELPER(tmp.data(), tmp.size());                                   \
    }                                                                                       \
  }

#define EXPECT_INVALID_STRING(input, explanation)                                           \
  {                                                                                         \
    const char* bytes = input;                                                              \
    size_t num_bytes = sizeof(input) - 1;                                                   \
    EXPECT_INVALID_STRING_HELPER(bytes, num_bytes, explanation);                            \
    for (size_t prefix_size : kTestVectorBoundaries) {                                      \
      std::string tmp = std::string(prefix_size, '\r') + std::string((bytes), (num_bytes)); \
      EXPECT_INVALID_STRING_HELPER(tmp.data(), tmp.size(), explanation);                    \
    }                                                                                       \
  }

TEST(ValidateUtf8, SafeOnNullptr) { EXPECT_FALSE(utfutils_is_valid_utf8(nullptr, 10)); }

TEST(ValidateUtf8, MinMaxCodeUnitsAndMinusOneAndPlusOne) {
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
}

TEST(ValidateUtf8, InvalidContinuations) {
  // 1 test for the first following byte of an initial two byte value not having the high bit.
  EXPECT_VALID_STRING("\xc2\x80");
  EXPECT_INVALID_STRING("\xc2\x7f", "first byte following two byte value not starting with 0b10");

  // 2 tests for the first and second following byte of an initial three byte value not having the
  // high bit set.
  EXPECT_INVALID_STRING("\xe1\x7f\x80",
                        "first byte following three byte value not starting with 0b10");
  EXPECT_INVALID_STRING("\xe1\x80\x7f",
                        "second byte following three byte value not starting with 0b10");

  // 3 tests for the first, second, and third following byte of an initial four byte value not
  // having the high bit set.
  EXPECT_VALID_STRING("\xf0\x90\x80\x80");
  EXPECT_INVALID_STRING("\xf0\x7f\x80\x80",
                        "first byte following four byte value not starting with 0b10");
  EXPECT_INVALID_STRING("\xf0\x90\x7f\x80",
                        "second byte following four byte value not starting with 0b10");
  EXPECT_INVALID_STRING("\xf0\x90\x80\x7f",
                        "third byte following four byte value not starting with 0b10");
}

TEST(ValidateUtf8, OnlyShortestEncodingIsValid) {
  // All encodings of slash, only the shortest is valid.
  //
  // For further details, see "code unit" defined to be 'The minimal bit
  // combination that can represent a unit of encoded text for processing or
  // interchange.'
  EXPECT_VALID_STRING("\x2f");
  EXPECT_INVALID_STRING("\xc0\xaf", "slash (2)");
  EXPECT_INVALID_STRING("\xe0\x80\xaf", "slash (3)");
  EXPECT_INVALID_STRING("\xf0\x80\x80\xaf", "slash (4)");
}

TEST(ValidateUtf8, ValidNoncharacterCodepoints) {
  EXPECT_VALID_STRING("\xd8\x9d");          // U+061D
  EXPECT_VALID_STRING("\xd7\xb6");          // U+05F6
  EXPECT_VALID_STRING("\xe0\xab\xb4");      // U+0AF4
  EXPECT_VALID_STRING("\xe0\xb1\x92");      // U+0C52
  EXPECT_VALID_STRING("\xf0\x9e\x91\x94");  // U+1E454
  EXPECT_VALID_STRING("\xf0\x9f\xa5\xb8");  // U+1F978
}

TEST(ValidateUtf8, Various) {
  EXPECT_VALID_STRING("");
  EXPECT_VALID_STRING("a");
  EXPECT_VALID_STRING("€");  // \xe2\x82\xac

  // Mix and match from MinMaxCodeUnitsAndMinusOneAndPlusOne
  EXPECT_VALID_STRING("\x00\xf4\x8f\xbf\xbf\x7f\xf0\x90\x80\x80\xc2\x80");
  EXPECT_VALID_STRING("\xdf\xbf\xef\xbf\xbf\xe1\x80\x80");

  // UTF-8 BOM
  EXPECT_VALID_STRING("\xef\xbb\xbf");
  EXPECT_INVALID_STRING("\xef", "Partial UTF-8 BOM (1)");
  EXPECT_INVALID_STRING("\xef\xbb", "Partial UTF-8 BOM (2)");

  EXPECT_INVALID_STRING("\xdf\x80\x80", "invalid partial sequence");
  EXPECT_INVALID_STRING("\xe0\x80\x80", "long U+0000, non shortest form");
  EXPECT_VALID_STRING("\xe1\x80\x80");
}

TEST(ValidateUtf8, IncompleteCodepointEndOfString) {
  EXPECT_INVALID_STRING("\xc2", "incomplete 2-byte codepoint");
  EXPECT_INVALID_STRING("\xd0", "incomplete 2-byte codepoint");

  EXPECT_INVALID_STRING("\xe0", "incomplete 3-byte codepoint");
  EXPECT_INVALID_STRING("\xe0\xa9", "incomplete 3-byte codepoint");
  EXPECT_INVALID_STRING("\xed", "incomplete 3-byte codepoint");
  EXPECT_INVALID_STRING("\xed\x9f", "incomplete 3-byte codepoint");
  EXPECT_INVALID_STRING("\xea", "incomplete 3-byte codepoint");

  EXPECT_INVALID_STRING("\xf0", "incomplete 4-byte codepoint");
  EXPECT_INVALID_STRING("\xf0\xaa", "incomplete 4-byte codepoint");
  EXPECT_INVALID_STRING("\xf0\xaa\x80", "incomplete 4-byte codepoint");
  EXPECT_INVALID_STRING("\xf4", "incomplete 4-byte codepoint");
  EXPECT_INVALID_STRING("\xf4\x8f", "incomplete 4-byte codepoint");
  EXPECT_INVALID_STRING("\xf4\xbf\xbf", "incomplete 4-byte codepoint");
}

TEST(ValidateUtf8, InvalidSpecialCharacterRanges) {
  // [0x80, 0xC1]
  EXPECT_INVALID_STRING("\x80", "invalid 1st byte character in range [0x80, 0xC1]");
  EXPECT_INVALID_STRING("\x9d\x9d\x0a", "invalid 1st byte character in range [0x80, 0xC1]");
  EXPECT_INVALID_STRING("\xa0", "invalid 1st byte character in range [0x80, 0xC1]");
  EXPECT_INVALID_STRING("\xb6", "invalid 1st byte character in range [0x80, 0xC1]");
  EXPECT_INVALID_STRING("\xc1", "invalid 1st byte character in range [0x80, 0xC1]");

  // 0xE0 followed by something not in [0xA0, 0xBF]
  EXPECT_INVALID_STRING("\xe0\x16\xc1", "0xE0 followed by something not in [0xA0, 0xBF]");
  EXPECT_INVALID_STRING("\xe0\xc0\xbf", "0xE0 followed by something not in [0xA0, 0xBF]");

  // 0xED followed by something not in [0x80, 0x9F]
  EXPECT_INVALID_STRING("\xed\x7f\xbf", "0xED followed by something not in [0x80, 0x9F]");
  EXPECT_INVALID_STRING("\xed\x7f\xbf", "0xED followed by something not in [0x80, 0x9F]");

  // 0xF0 followed by something not in [0x90, 0xBF]
  EXPECT_INVALID_STRING("\xf0\x8e\xbf", "0xF0 followed by something not in [0x90, 0xBF]");
  EXPECT_INVALID_STRING("\xf0\xc1\xbf", "0xF0 followed by something not in [0x90, 0xBF]");

  // 0xF4 followed by something not in [0x80-0x8F]
  EXPECT_INVALID_STRING("\xf4\x7d\xbc", "0xF4 followed by something not in [0x80, 0x8F]");
  EXPECT_INVALID_STRING("\xf4\x92\xa8", "0xF4 followed by something not in [0x80, 0x8F]");
}

// All the following test cases are taken from Chromium's
// streaming_utf8_validator_unittest.cc
//
// Some are duplicative to other tests, and have been kept to ease
// comparison and translation of the tests.

TEST(ValidateUtf8, ChromiumSimple) {
  EXPECT_VALID_STRING("\r");
  EXPECT_VALID_STRING("\n");
  EXPECT_VALID_STRING("a");
  EXPECT_VALID_STRING("\xc2\x81");
  EXPECT_VALID_STRING("\xe1\x80\xbf");
  EXPECT_VALID_STRING("\xf1\x80\xa0\xbf");
  EXPECT_VALID_STRING("\xef\xbb\xbf");  // UTF-8 BOM
}

TEST(ValidateUtf8, ChromiumAlwaysInvalidBytes) {
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
}

TEST(ValidateUtf8, ChromiumSurrogateCodepoints) {
  EXPECT_INVALID_STRING("\xed\xa0\x80", "U+D800, high surrogate, first");
  EXPECT_INVALID_STRING("\xed\xb0\x80", "low surrogate, first");
  EXPECT_INVALID_STRING("\xed\xbf\xbf", "low surrogate, last");
}

TEST(ValidateUtf8, ChromiumOverlongSequences) {
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
}

TEST(ValidateUtf8, ChromiumBeyondU10FFFF) {
  // Beyond U+10FFFF
  EXPECT_INVALID_STRING("\xf4\x90\x80\x80", "U+110000");
  EXPECT_INVALID_STRING("\xf5\xaf\xb6\x96", "First byte beyond 0xF4");
  EXPECT_INVALID_STRING("\xf8\xa0\xbf\x80\xbf", "5 bytes");
  EXPECT_INVALID_STRING("\xfc\x9c\xbf\x80\xbf\x80", "6 bytes");
}

TEST(ValidateUtf8, ChromiumUtf16Boms) {
  // BOMs in UTF-16(BE|LE)
  EXPECT_INVALID_STRING("\xfe\xff", "BOMs in UTF-16 BE");
  EXPECT_INVALID_STRING("\xff\xfe", "BOMs in UTF-16 LE");
}
