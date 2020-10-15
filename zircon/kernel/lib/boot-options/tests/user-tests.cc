// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <lib/fit/defer.h>

#include <zxtest/zxtest.h>

namespace {

constexpr size_t kFileSizeMax = 64;

//
// Retrieves the BootOptions test-only member backed by a given type.
//
template <typename OptionType>
OptionType GetValue(const BootOptions& options);

template <>
bool GetValue<bool>(const BootOptions& options) {
  return options.test_bool;
}

template <>
uint32_t GetValue<uint32_t>(const BootOptions& options) {
  return options.test_uint32;
}

template <>
uint64_t GetValue<uint64_t>(const BootOptions& options) {
  return options.test_uint64;
}

template <>
SmallString GetValue<SmallString>(const BootOptions& options) {
  return options.test_smallstring;
}

template <>
TestEnum GetValue<TestEnum>(const BootOptions& options) {
  return options.test_enum;
}

template <>
TestStruct GetValue<TestStruct>(const BootOptions& options) {
  return options.test_struct;
}

template <>
RedactedHex GetValue<RedactedHex>(const BootOptions& options) {
  return options.test_redacted_hex;
}

//
// Updates the BootOptions test-only member backed by a given type.
//
template <typename OptionType>
void SetManyValue(BootOptions* options, OptionType value);

template <>
void SetManyValue<bool>(BootOptions* options, bool value) {
  options->test_bool = value;
}

template <>
void SetManyValue<uint32_t>(BootOptions* options, uint32_t value) {
  options->test_uint32 = value;
}

template <>
void SetManyValue<uint64_t>(BootOptions* options, uint64_t value) {
  options->test_uint64 = value;
}

template <>
void SetManyValue<SmallString>(BootOptions* options, SmallString value) {
  options->test_smallstring = value;
}

template <>
void SetManyValue<TestEnum>(BootOptions* options, TestEnum value) {
  options->test_enum = value;
}

template <>
void SetManyValue<TestStruct>(BootOptions* options, TestStruct value) {
  options->test_struct = value;
}

template <>
void SetManyValue<RedactedHex>(BootOptions* options, RedactedHex value) {
  options->test_redacted_hex = value;
}

//
// Compares values of a particular test option type.
//
template <typename OptionType>
void CompareValues(const OptionType& lhs, const OptionType& rhs) {
  EXPECT_EQ(lhs, rhs);
}

template <>
void CompareValues<SmallString>(const SmallString& lhs, const SmallString& rhs) {
  // SmallStrings are null-terminated.
  EXPECT_STR_EQ(lhs.data(), rhs.data());
}

//
// Tests, templated by test option type.
//
template <typename OptionType>
void TestParsing(std::string_view name, std::string_view to_set, OptionType expected_value) {
  // TODO: use fbl::unique_FILE when available.
  FILE* f = tmpfile();
  auto cleanup = fit::defer([f]() { fclose(f); });

  BootOptions options;
  options.SetMany(to_set, f);

  char buff[kFileSizeMax];
  size_t n = fread(buff, 1, kFileSizeMax, f);
  ASSERT_EQ(0, ferror(f), "failed to read file: %s", strerror(errno));
  ASSERT_EQ(0, n, "unexpected error while setting: %.*s", static_cast<int>(n), buff);

  auto actual_value = GetValue<OptionType>(options);
  CompareValues(expected_value, actual_value);
}

template <typename OptionType>
void TestUnparsing(std::string_view name, OptionType value, std::string_view expected_shown) {
  // TODO: use fbl::unique_FILE when available.
  FILE* f = tmpfile();
  auto cleanup = fit::defer([f]() { fclose(f); });

  BootOptions options;
  SetManyValue(&options, value);

  ASSERT_EQ(0, options.Show(name, false, f));

  char buff[kFileSizeMax];
  rewind(f);  // Without this line, test cases fail. tmpfile is being reused despite being closed?!
  size_t n = fread(buff, 1, kFileSizeMax, f);
  ASSERT_EQ(0, ferror(f), "failed to read file: %s", strerror(errno));
  ASSERT_EQ(expected_shown.size(), n, "did not show as much as expected: \"%.*s\"",
            static_cast<int>(n), buff);
  EXPECT_EQ(0, memcmp(expected_shown.data(), buff, n), "unexpected file contents: \"%.*s\"",
            static_cast<int>(n), buff);
}

TEST(ParsingTests, DefaultBoolValue) {
  ASSERT_NO_FATAL_FAILURES(TestParsing<bool>("test.option.bool", "", false));
}

TEST(ParsingTests, FalseyBoolValues) {
  ASSERT_NO_FATAL_FAILURES(
      TestUnparsing<bool>("test.option.bool", false, "test.option.bool=false\n"));

  ASSERT_NO_FATAL_FAILURES(TestParsing<bool>("test.option.bool", "test.option.bool=0", false));

  ASSERT_NO_FATAL_FAILURES(TestParsing<bool>("test.option.bool", "test.option.bool=off", false));
}

TEST(ParsingTests, TruthyBoolValues) {
  ASSERT_NO_FATAL_FAILURES(TestParsing<bool>("test.option.bool", "test.option.bool=true", true));

  // A truthy value is by definition anything that isn't falsey (see above).
  ASSERT_NO_FATAL_FAILURES(TestParsing<bool>("test.option.bool", "test.option.bool=", true));

  ASSERT_NO_FATAL_FAILURES(
      TestParsing<bool>("test.option.bool", "test.option.bool=anything", true));
}

TEST(UnparsingTests, FalseBoolValue) {
  ASSERT_NO_FATAL_FAILURES(
      TestUnparsing<bool>("test.option.bool", false, "test.option.bool=false\n"));
}

TEST(UnparsingTests, TrueBoolValue) {
  ASSERT_NO_FATAL_FAILURES(
      TestUnparsing<bool>("test.option.bool", true, "test.option.bool=true\n"));
}

TEST(ParsingTests, DefaultUint32Value) {
  ASSERT_NO_FATAL_FAILURES(TestParsing<uint32_t>("test.option.uint32", "", 123));
}

TEST(ParsingTests, BasicUint32Value) {
  ASSERT_NO_FATAL_FAILURES(
      TestParsing<uint32_t>("test.option.uint32", "test.option.uint32=123", 123));
}

TEST(ParsingTests, HexUint32Value) {
  ASSERT_NO_FATAL_FAILURES(
      TestParsing<uint32_t>("test.option.uint32", "test.option.uint32=0x123", 0x123));
}

TEST(ParsingTests, NegativeUint32Value) {
  ASSERT_NO_FATAL_FAILURES(
      TestParsing<uint32_t>("test.option.uint32", "test.option.uint32=-123", ~uint32_t{123} + 1));
}

TEST(UnparsingTests, BasicUint32Value) {
  ASSERT_NO_FATAL_FAILURES(
      TestUnparsing<uint32_t>("test.option.uint32", 123, "test.option.uint32=0x7b\n"));
}

TEST(ParsingTests, DefaultUint64Value) {
  ASSERT_NO_FATAL_FAILURES(TestParsing<uint64_t>("test.option.uint64", "", 456));
}

TEST(ParsingTests, BasicUint64Value) {
  ASSERT_NO_FATAL_FAILURES(
      TestParsing<uint64_t>("test.option.uint64", "test.option.uint64=456", 456));
}

TEST(ParsingTests, HexUint64Value) {
  ASSERT_NO_FATAL_FAILURES(
      TestParsing<uint64_t>("test.option.uint64", "test.option.uint64=0x456", 0x456));
}

TEST(ParsingTests, NegativeUint64Value) {
  ASSERT_NO_FATAL_FAILURES(
      TestParsing<uint64_t>("test.option.uint64", "test.option.uint64=-456", ~uint64_t{456} + 1));
}

TEST(ParsingTests, LargeUint64Value) {
  ASSERT_NO_FATAL_FAILURES(TestParsing<uint64_t>(
      "test.option.uint64", "test.option.uint64=0x87654321012345678", 0x7654321012345678));
}

TEST(UnparsingTests, BasicUint64Value) {
  ASSERT_NO_FATAL_FAILURES(
      TestUnparsing<uint64_t>("test.option.uint64", 456, "test.option.uint64=0x1c8\n"));
}

TEST(ParsingTests, DefaultSmallStringValue) {
  ASSERT_NO_FATAL_FAILURES(
      TestParsing<SmallString>("test.option.smallstring", "",
                               {'t', 'e', 's', 't', '-', 'd', 'e', 'f', 'a', 'u', 'l', 't', '-',
                                'v', 'a', 'l', 'u', 'e', '\0'}));
}

TEST(ParsingTests, BasicSmallStringValue) {
  ASSERT_NO_FATAL_FAILURES(
      TestParsing<SmallString>("test.option.smallstring", "test.option.smallstring=new-value",
                               {'n', 'e', 'w', '-', 'v', 'a', 'l', 'u', 'e', '\0'}));
}

TEST(ParsingTests, LargeSmallStringValue) {
  constexpr SmallString kSevenAlphabetsTruncated = {
      'a', 'b', 'c', 'd',  'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',  //
      'o', 'p', 'q', 'r',  's', 't', 'u', 'v', 'w', 'x', 'y', 'z',            //
      'a', 'b', 'c', 'd',  'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',  //
      'o', 'p', 'q', 'r',  's', 't', 'u', 'v', 'w', 'x', 'y', 'z',            //
      'a', 'b', 'c', 'd',  'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',  //
      'o', 'p', 'q', 'r',  's', 't', 'u', 'v', 'w', 'x', 'y', 'z',            //
      'a', 'b', 'c', 'd',  'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',  //
      'o', 'p', 'q', 'r',  's', 't', 'u', 'v', 'w', 'x', 'y', 'z',            //
      'a', 'b', 'c', 'd',  'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',  //
      'o', 'p', 'q', 'r',  's', 't', 'u', 'v', 'w', 'x', 'y', 'z',            //
      'a', 'b', 'c', 'd',  'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',  //
      'o', 'p', 'q', 'r',  's', 't', 'u', 'v', 'w', 'x', 'y', 'z',            //
      'a', 'b', 'c', '\0',                                                    //
  };

  ASSERT_NO_FATAL_FAILURES(TestParsing<SmallString>("test.option.smallstring",
                                                    "test.option.smallstring="  // Seven alphabets.
                                                    "abcdefghijklmnopqrstuvwxyz"
                                                    "abcdefghijklmnopqrstuvwxyz"
                                                    "abcdefghijklmnopqrstuvwxyz"
                                                    "abcdefghijklmnopqrstuvwxyz"
                                                    "abcdefghijklmnopqrstuvwxyz"
                                                    "abcdefghijklmnopqrstuvwxyz"
                                                    "abcdefghijklmnopqrstuvwxyz",
                                                    kSevenAlphabetsTruncated));
}

TEST(UnparsingTests, BasicSmallStringValue) {
  ASSERT_NO_FATAL_FAILURES(TestUnparsing<SmallString>(
      "test.option.smallstring", {'n', 'e', 'w', '-', 'v', 'a', 'l', 'u', 'e', '\0'},
      "test.option.smallstring=new-value\n"));
}

TEST(ParsingTests, DefaultEnumValue) {
  ASSERT_NO_FATAL_FAILURES(TestParsing<TestEnum>("test.option.enum", "", TestEnum::kDefault));
}

TEST(ParsingTests, BasicEnumValues) {
  ASSERT_NO_FATAL_FAILURES(
      TestParsing<TestEnum>("test.option.enum", "test.option.enum=default", TestEnum::kDefault));

  ASSERT_NO_FATAL_FAILURES(
      TestParsing<TestEnum>("test.option.enum", "test.option.enum=value1", TestEnum::kValue1));

  ASSERT_NO_FATAL_FAILURES(
      TestParsing<TestEnum>("test.option.enum", "test.option.enum=value2", TestEnum::kValue2));
}

TEST(ParsingTests, UnknownEnumValue) {
  ASSERT_NO_FATAL_FAILURES(
      TestParsing<TestEnum>("test.option.enum", "test.option.enum=unknown", TestEnum::kDefault));
}

TEST(UnparsingTests, BasicEnumValues) {
  ASSERT_NO_FATAL_FAILURES(TestUnparsing<TestEnum>("test.option.enum", TestEnum::kDefault,
                                                   "test.option.enum=default\n"));

  ASSERT_NO_FATAL_FAILURES(
      TestUnparsing<TestEnum>("test.option.enum", TestEnum::kValue1, "test.option.enum=value1\n"));

  ASSERT_NO_FATAL_FAILURES(
      TestUnparsing<TestEnum>("test.option.enum", TestEnum::kValue2, "test.option.enum=value2\n"));
}

TEST(ParsingTests, DefaultStructValue) {
  ASSERT_NO_FATAL_FAILURES(TestParsing<TestStruct>("test.option.struct", "", {}));
}

TEST(ParsingTests, BasicStructValue) {
  ASSERT_NO_FATAL_FAILURES(
      TestParsing<TestStruct>("test.option.struct", "test.option.struct=test", {.present = true}));
}

TEST(ParsingTests, UnparsableStructValue) {
  ASSERT_NO_FATAL_FAILURES(TestParsing<TestStruct>(
      // We expect no change from the default value.
      "test.option.struct", "test.option.struct=unparsable", {}));
}

TEST(UnparsingTests, BasicStructValue) {
  ASSERT_NO_FATAL_FAILURES(TestUnparsing<TestStruct>("test.option.struct", {.present = true},
                                                     "test.option.struct=test\n"));
}

TEST(UnparsingTests, EmptyStructValue) {
  ASSERT_NO_FATAL_FAILURES(
      TestUnparsing<TestStruct>("test.option.struct", {}, "test.option.struct=test\n"));
}

TEST(ParsingTests, DefaultRedatedHexValue) {
  ASSERT_NO_FATAL_FAILURES(TestParsing<RedactedHex>("test.option.redacted_hex", "", {}));
}

TEST(ParsingTests, BasicRedatedHexValue) {
  // If we inline, the string will point to rodata, which will result in a
  // segmentation fault on redaction.
  const char to_set[] = "test.option.redacted_hex=abc123";
  ASSERT_NO_FATAL_FAILURES(TestParsing<RedactedHex>("test.option.redacted_hex", to_set,
                                                    {
                                                        .hex = {'a', 'b', 'c', '1', '2', '3', '\0'},
                                                        .len = 6,
                                                    }));
  EXPECT_STR_EQ("test.option.redacted_hex=xxxxxx", to_set);
}

TEST(ParsingTests, NonHexRedatedHexValue) {
  // We expect neither the updating of the BootOptions member nor redaction
  // when non-hex characters are present (e.g., 'x', 'y', or 'z').
  const char to_set[] = "test.option.redacted_hex=xyz123";
  ASSERT_NO_FATAL_FAILURES(TestParsing<RedactedHex>("test.option.redacted_hex", to_set, {}));
  EXPECT_STR_EQ("test.option.redacted_hex=xyz123", to_set);
}

TEST(UnparsingTests, BasicRedatedHexValue) {
  ASSERT_NO_FATAL_FAILURES(
      TestUnparsing<RedactedHex>("test.option.redacted_hex",
                                 {
                                     .hex = {'a', 'b', 'c', '1', '2', '3', '\0'},
                                     .len = 6,
                                 },
                                 "test.option.redacted_hex=<redacted.6.hex.chars>\n"));
}

TEST(UnparsingTests, EmptyRedatedHexValue) {
  ASSERT_NO_FATAL_FAILURES(
      TestUnparsing<RedactedHex>("test.option.redacted_hex", {}, "test.option.redacted_hex=\n"));
}

TEST(ParsingTests, SetManyAdditivity) {
  // TODO: use fbl::unique_FILE when available.
  FILE* f = tmpfile();
  auto cleanup = fit::defer([f]() { fclose(f); });

  BootOptions options;
  options.test_bool = false;
  options.test_uint32 = 0;
  options.test_uint64 = 0;

  options.SetMany("test.option.bool=true test.option.uint32=123", f);
  EXPECT_TRUE(options.test_bool);
  EXPECT_EQ(123, options.test_uint32);
  EXPECT_EQ(0, options.test_uint64);

  options.SetMany("test.option.bool=false test.option.uint64=456", f);
  EXPECT_FALSE(options.test_bool);
  EXPECT_EQ(123, options.test_uint32);
  EXPECT_EQ(456, options.test_uint64);

  char buff[kFileSizeMax];
  size_t n = fread(buff, 1, kFileSizeMax, f);
  ASSERT_EQ(0, ferror(f), "failed to read file: %s", strerror(errno));
  ASSERT_EQ(0, n, "unexpected error while setting: %.*s", static_cast<int>(n), buff);
}

TEST(BootOptionTests, StringSanitization) {
  // Printable ASCII characters are left alone.
  {
    constexpr std::string_view kIn =
        " !\"#$%&\'()*+,-./"
        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    std::string_view expected = kIn;

    char out[kIn.size()];
    std::string_view sanitized{out, BootOptions::SanitizeString(out, sizeof(out), kIn)};
    ASSERT_EQ(expected.size(), sanitized.size());
    EXPECT_BYTES_EQ(expected.data(), sanitized.data(), expected.size());
  }

  // All whitespace becomes plain space.
  {
    constexpr std::string_view kIn = "abc\t\n\r123";
    std::string_view expected = "abc   123";

    char out[kIn.size()];
    std::string_view sanitized{out, BootOptions::SanitizeString(out, sizeof(out), kIn)};
    ASSERT_EQ(expected.size(), sanitized.size());
    EXPECT_BYTES_EQ(expected.data(), sanitized.data(), expected.size());
  }

  // Other characters become periods.
  {
    // \t, \n, \r are 9, 10, 13, respectively.
    constexpr char kIn[] = {'a', 'b', 'c', 0,  1,  2,  3,  4,  5,   6,   7,   8,
                            11,  12,  14,  15, 16, 17, 18, 19, 20,  21,  22,  23,
                            24,  25,  26,  27, 28, 29, 30, 31, 127, '1', '2', '3'};
    std::string_view expected = "abc..............................123";

    char out[std::size(kIn)];
    std::string_view sanitized{out,
                               BootOptions::SanitizeString(out, sizeof(out), {kIn, sizeof(kIn)})};
    ASSERT_EQ(expected.size(), sanitized.size());
    EXPECT_BYTES_EQ(expected.data(), sanitized.data(), expected.size());
  }
}

}  // namespace
