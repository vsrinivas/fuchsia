// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pretty/sizes.h>
#include <unittest/unittest.h>

typedef struct {
    size_t input;
    const char* expected_output;
} format_size_test_case_t;

#define KILO (1024ULL)
#define MEGA (KILO * 1024)
#define GIGA (MEGA * 1024)
#define TERA (GIGA * 1024)
#define PETA (TERA * 1024)
#define EXA (PETA * 1024)

static const format_size_test_case_t format_size_test_cases[] = {
    // Whole multiples don't print decimals,
    // and always round up to their largest unit.
    {0, "0B"},
    {1, "1B"},

    // Favor the largest unit when it loses no precision
    // (e.g., "1k" not "1024B").
    // Larger values may still use a smaller unit
    // (e.g., "1k" + 1 == "1025B") to preserve precision.
    {KILO - 1, "1023B"},
    {KILO, "1k"},
    {KILO + 1, "1025B"},
    {KILO * 9, "9k"},
    {KILO * 9 + 1, "9217B"},
    {KILO * 10, "10k"},

    // Same demonstration for the next unit.
    {MEGA - KILO, "1023k"},
    {MEGA, "1M"},
    {MEGA + KILO, "1025k"},
    {MEGA * 9, "9M"},
    {MEGA * 9 + KILO, "9217k"},
    {MEGA * 10, "10M"},

    // Sanity checks for remaining units.
    {MEGA, "1M"},
    {GIGA, "1G"},
    {TERA, "1T"},
    {PETA, "1P"},
    {EXA, "1E"},

    // Non-whole multiples print decimals, and favor more whole digits
    // (e.g., "1024.0k" not "1.0M") to retain precision.
    {MEGA - 1, "1024.0k"},
    {MEGA + MEGA / 3, "1365.3k"}, // Only one decimal place is ever shown.
    {GIGA - 1, "1024.0M"},
    {TERA - 1, "1024.0G"},
    {PETA - 1, "1024.0T"},
    {EXA - 1, "1024.0P"},
    {UINT64_MAX, "16.0E"},

    // Never show more than four whole digits,
    // to make the values easier to eyeball.
    {9999, "9999B"},
    {10000, "9.8k"},
    {KILO * 9999, "9999k"},
    {KILO * 9999 + 1, "9999.0k"},
    {KILO * 10000, "9.8M"},
};

bool format_size_test(void) {
    BEGIN_TEST;
    char str[MAX_FORMAT_SIZE_LEN];
    char msg[128];
    for (unsigned int i = 0; i < countof(format_size_test_cases); i++) {
        const format_size_test_case_t* tc = format_size_test_cases + i;
        memset(str, 0, sizeof(str));
        char* ret = format_size(str, sizeof(str), tc->input);
        snprintf(msg, sizeof(msg), "format_size(bytes=%zd)", tc->input);
        EXPECT_STR_EQ(tc->expected_output, str, /* len */ 128, msg);
        // Should always return the input pointer.
        EXPECT_EQ(&(str[0]), ret, msg);
    }
    END_TEST;
}

bool format_size_short_buf_truncates(void) {
    BEGIN_TEST;
    // Widest possible output: four whole digits + decimal.
    static const size_t input = 1023 * 1024 + 1;
    static const char expected_output[] = "1023.0k";

    char buf[sizeof(expected_output) * 2];
    char msg[128];
    for (size_t str_size = 0; str_size <= sizeof(expected_output);
         str_size++) {
        memset(buf, 0x55, sizeof(buf));
        char* ret = format_size(buf, str_size, input);

        snprintf(msg, sizeof(msg),
                 "format_size(str_size=%zd, bytes=%zd)", str_size, input);
        EXPECT_EQ(&(buf[0]), ret, msg);
        if (str_size > 2) {
            EXPECT_BYTES_EQ(
                (uint8_t*)expected_output, (uint8_t*)buf, str_size - 1, msg);
        }
        if (str_size > 1) {
            EXPECT_EQ(buf[str_size - 1], '\0', msg);
        }
        EXPECT_EQ(buf[str_size], 0x55, msg);
    }
    END_TEST;
}

bool format_size_empty_str_succeeds(void) {
    BEGIN_TEST;
    static const size_t input = 1023 * 1024 + 1;

    char c = 0x55;
    char* ret = format_size(&c, 0, input);
    EXPECT_EQ(&c, ret, "");
    EXPECT_EQ(0x55, c, "");
    END_TEST;
}

bool format_size_empty_null_str_succeeds(void) {
    BEGIN_TEST;
    static const size_t input = 1023 * 1024 + 1;

    char* ret = format_size(NULL, 0, input);
    EXPECT_EQ(NULL, ret, "");
    END_TEST;
}

BEGIN_TEST_CASE(pretty_tests)
RUN_TEST(format_size_test)
RUN_TEST(format_size_short_buf_truncates)
RUN_TEST(format_size_empty_str_succeeds)
RUN_TEST(format_size_empty_null_str_succeeds)
END_TEST_CASE(pretty_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
