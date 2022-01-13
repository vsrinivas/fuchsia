// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <pretty/hexdump.h>
#include <pretty/sizes.h>
#include <zxtest/zxtest.h>

typedef struct {
  const size_t input;
  const char unit;
  const char* expected_output;
} format_size_test_case_t;

#define KILO (1024ULL)
#define MEGA (KILO * 1024)
#define GIGA (MEGA * 1024)
#define TERA (GIGA * 1024)
#define PETA (TERA * 1024)
#define EXA (PETA * 1024)

static const format_size_test_case_t format_size_test_cases[] = {
// Declare a test case that uses a unit of 0,
// picking a natural unit for the size.
#define TC0(i, o) \
  { .input = i, .unit = 0, .expected_output = o }

    // Whole multiples don't print decimals,
    // and always round up to their largest unit.
    TC0(0, "0B"),
    TC0(1, "1B"),

    // Favor the largest unit when it loses no precision
    // (e.g., "1k" not "1024B").
    // Larger values may still use a smaller unit
    // (e.g., "1k" + 1 == "1025B") to preserve precision.
    TC0(KILO - 1, "1023B"),
    TC0(KILO, "1K"),
    TC0(KILO + 1, "1025B"),
    TC0(KILO * 9, "9K"),
    TC0(KILO * 9 + 1, "9217B"),
    TC0(KILO * 10, "10K"),

    // Same demonstration for the next unit.
    TC0(MEGA - KILO, "1023K"),
    TC0(MEGA, "1M"),
    TC0(MEGA + KILO, "1025K"),
    TC0(MEGA * 9, "9M"),
    TC0(MEGA * 9 + KILO, "9217K"),
    TC0(MEGA * 10, "10M"),

    // Sanity checks for remaining units.
    TC0(MEGA, "1M"),
    TC0(GIGA, "1G"),
    TC0(TERA, "1T"),
    TC0(PETA, "1P"),
    TC0(EXA, "1E"),

    // Non-whole multiples print decimals, and favor more whole digits
    // (e.g., "1024.0k" not "1.0M") to retain precision.
    TC0(MEGA - 1, "1024.0K"),
    TC0(MEGA + MEGA / 3, "1365.3K"),  // Only one decimal place is ever shown.
    TC0(GIGA - 1, "1024.0M"),
    TC0(TERA - 1, "1024.0G"),
    TC0(PETA - 1, "1024.0T"),
    TC0(EXA - 1, "1024.0P"),
    TC0(UINT64_MAX, "16.0E"),

    // Never show more than four whole digits,
    // to make the values easier to eyeball.
    TC0(9999, "9999B"),
    TC0(10000, "9.8K"),
    TC0(KILO * 9999, "9999K"),
    TC0(KILO * 9999 + 1, "9999.0K"),
    TC0(KILO * 10000, "9.8M"),

    // Ensure values are correctly rounded.
    TC0(10700, "10.4K"),                  // 10700 / 1024 == 10.4492187500
    TC0(10701, "10.5K"),                  // 10701 / 1024 == 10.4501953125
    TC0(69887590, "66.6M"),               // 69887590 / 1M == ~66.64999961
    TC0(69887591, "66.7M"),               // 69887591 / 1M == ~66.65000057
    TC0(18389097998479209267u, "15.9E"),  // 18389097998479209267 / 1E == ~15.9499999999999999998
    TC0(18389097998479209268u, "16.0E"),  // 18389097998479209268 / 1E == ~15.9500000000000000016

// Declare a test case fixed to the specified unit.
#define TCF(i, u, o) \
  { .input = i, .unit = u, .expected_output = o }

    // When fixed, we can see a lot more digits.
    TCF(UINT64_MAX, 'B', "18446744073709551615B"),
    TCF(UINT64_MAX, 'K', "18014398509481984.0K"),
    TCF(UINT64_MAX, 'M', "17592186044416.0M"),
    TCF(UINT64_MAX, 'G', "17179869184.0G"),
    TCF(UINT64_MAX, 'T', "16777216.0T"),
    TCF(UINT64_MAX, 'P', "16384.0P"),
    TCF(UINT64_MAX, 'E', "16.0E"),

    // Smaller than natural fixed unit.
    TCF(GIGA, 'K', "1048576K"),

    // Larger than natural fixed unit.
    TCF(MEGA / 10, 'M', "0.1M"),

    // Unknown units fall back to natural, but add a '?' prefix.
    TCF(GIGA, 'q', "?1G"),
    TCF(KILO, 'q', "?1K"),
    TCF(GIGA + 1, '#', "?1.0G"),
    TCF(KILO + 1, '#', "?1025B"),
};

TEST(PrettyTests, format_size_fixed_test) {
  char str[MAX_FORMAT_SIZE_LEN];
  char msg[128];
  for (unsigned int i = 0; i < countof(format_size_test_cases); i++) {
    const format_size_test_case_t* tc = format_size_test_cases + i;
    memset(str, 0, sizeof(str));
    char* ret = format_size_fixed(str, sizeof(str), tc->input, tc->unit);
    snprintf(msg, sizeof(msg), "format_size_fixed(bytes=%zd, unit=%c)", tc->input,
             tc->unit == 0 ? '0' : tc->unit);
    EXPECT_STREQ(tc->expected_output, str, "%s", msg);
    // Should always return the input pointer.
    EXPECT_EQ(&(str[0]), ret, "%s", msg);
  }
}

TEST(PrettyTests, format_size_short_buf_truncates) {
  // Widest possible output: four whole digits + decimal.
  static const size_t input = 1023 * 1024 + 1;
  static const char expected_output[] = "1023.0K";

  char buf[sizeof(expected_output) * 2];
  char msg[128];
  for (size_t str_size = 0; str_size <= sizeof(expected_output); str_size++) {
    memset(buf, 0x55, sizeof(buf));
    char* ret = format_size(buf, str_size, input);

    snprintf(msg, sizeof(msg), "format_size(str_size=%zd, bytes=%zd)", str_size, input);
    EXPECT_EQ(&(buf[0]), ret, "%s", msg);
    if (str_size > 2) {
      EXPECT_BYTES_EQ((uint8_t*)expected_output, (uint8_t*)buf, str_size - 1, "%s", msg);
    }
    if (str_size > 1) {
      EXPECT_EQ(buf[str_size - 1], '\0', "%s", msg);
    }
    EXPECT_EQ(buf[str_size], 0x55, "%s", msg);
  }
}

// Tests the path where we add a prefix '?' to make sure we don't
// overrun the buffer or return a non-null-terminated result.
TEST(PrettyTests, format_size_bad_unit_short_buf_truncates) {
  char buf[MAX_FORMAT_SIZE_LEN];

  // Size zero should not touch the buffer.
  memset(buf, 0x55, sizeof(buf));
  format_size_fixed(buf, 0, GIGA, 'q');
  EXPECT_EQ(buf[0], 0x55, "");

  // Size 1 should only null out the first byte.
  memset(buf, 0x55, sizeof(buf));
  format_size_fixed(buf, 1, GIGA, 'q');
  EXPECT_EQ(buf[0], '\0', "");
  EXPECT_EQ(buf[1], 0x55, "");

  // Size 2 should just be the warning '?'.
  memset(buf, 0x55, sizeof(buf));
  format_size_fixed(buf, 2, GIGA, 'q');
  EXPECT_EQ(buf[0], '?', "");
  EXPECT_EQ(buf[1], '\0', "");
  EXPECT_EQ(buf[2], 0x55, "");

  // Then just the number without units.
  memset(buf, 0x55, sizeof(buf));
  format_size_fixed(buf, 3, GIGA, 'q');
  EXPECT_EQ(buf[0], '?', "");
  EXPECT_EQ(buf[1], '1', "");
  EXPECT_EQ(buf[2], '\0', "");
  EXPECT_EQ(buf[3], 0x55, "");

  // Then the whole thing.
  memset(buf, 0x55, sizeof(buf));
  format_size_fixed(buf, 4, GIGA, 'q');
  EXPECT_EQ(buf[0], '?', "");
  EXPECT_EQ(buf[1], '1', "");
  EXPECT_EQ(buf[2], 'G', "");
  EXPECT_EQ(buf[3], '\0', "");
  EXPECT_EQ(buf[4], 0x55, "");
}

TEST(PrettyTests, format_size_empty_str_succeeds) {
  static const size_t input = 1023 * 1024 + 1;

  char c = 0x55;
  char* ret = format_size(&c, 0, input);
  EXPECT_EQ(&c, ret, "");
  EXPECT_EQ(0x55, c, "");
}

TEST(PrettyTests, format_size_empty_null_str_succeeds) {
  static const size_t input = 1023 * 1024 + 1;

  char* ret = format_size(NULL, 0, input);
  EXPECT_EQ(NULL, ret, "");
}

TEST(PrettyTests, hexdump_very_ex_test) {
  static const uint8_t input[] = {0, 1, 2, 3, 'a', 'b', 'c', 'd'};
  const uint64_t kTestDisplayAddr = 0x1000;
  static const char expected[] =
      "0x00001000: 03020100 64636261                   |....abcd........|\n";

  char output_buffer[sizeof(expected)];
  FILE* f = fmemopen(output_buffer, sizeof(output_buffer), "w");
  ASSERT_NOT_NULL(f, "");
  hexdump_very_ex(input, sizeof(input), kTestDisplayAddr, hexdump_stdio_printf, f);
  fclose(f);
  EXPECT_STREQ((const char*)output_buffer, expected, "");
}

TEST(PrettyTests, hexdump8_very_ex_test) {
  static const uint8_t input[] = {0, 1, 2, 3, 'a', 'b', 'c', 'd'};
  const uint64_t kTestDisplayAddr = 0x1000;
  static const char expected[] =
      "0x00001000: 00 01 02 03 61 62 63 64                         |....abcd\n";

  char output_buffer[sizeof(expected)];
  FILE* f = fmemopen(output_buffer, sizeof(output_buffer), "w");
  ASSERT_NOT_NULL(f, "");
  hexdump8_very_ex(input, sizeof(input), kTestDisplayAddr, hexdump_stdio_printf, f);
  fclose(f);
  EXPECT_STREQ((const char*)output_buffer, expected, "");
}
