// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "tests.h"

#include <debug.h>
#include <lib/unittest/unittest.h>
#include <stdio.h>
#include <string.h>

// Checks that vsnprintf() gives the expected string as output.
static bool test_printf(const char* expected, const char* format, ...) {
    char buf[100];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (length < 0 || length >= (int)sizeof(buf)) {
        printf("vsnprintf() returned %d\n", length);
        return false;
    }
    if (buf[length] != '\0') {
        printf("missing string terminator\n");
        return false;
    }
    if (length != (int)strlen(expected) ||
        memcmp(buf, expected, length + 1) != 0) {
        printf("expected: \"%s\" (length %zu)\n",
               expected, strlen(expected));
        printf("but got:  \"%s\" (length %zu) with return value %d)\n",
               buf, strlen(buf), length);
        return false;
    }
    return true;
}

static bool printf_tests() {
    BEGIN_TEST;

    printf("numbers:\n");
    EXPECT_TRUE(test_printf("int8:  -12 0 -2",
                            "int8:  %hhd %hhd %hhd", -12, 0, 254), "");
    EXPECT_TRUE(test_printf("uint8: 244 0 254",
                            "uint8: %hhu %hhu %hhu", -12, 0, 254), "");
    EXPECT_TRUE(test_printf("int16: -1234 0 1234",
                            "int16: %hd %hd %hd", -1234, 0, 1234), "");
    EXPECT_TRUE(test_printf("uint16:64302 0 1234",
                            "uint16:%hu %hu %hu", -1234, 0, 1234), "");
    EXPECT_TRUE(test_printf("int:   -12345678 0 12345678",
                            "int:   %d %d %d", -12345678, 0, 12345678), "");
    static_assert(-12345678U == 4282621618, "");
    EXPECT_TRUE(test_printf("uint:  4282621618 0 12345678",
                            "uint:  %u %u %u", -12345678, 0, 12345678), "");
    EXPECT_TRUE(test_printf("long:  -12345678 0 12345678",
                            "long:  %ld %ld %ld", -12345678L, 0L, 12345678L), "");
    static_assert(-12345678UL == 18446744073697205938UL, "");
    EXPECT_TRUE(test_printf("ulong: 18446744073697205938 0 12345678",
                            "ulong: %lu %lu %lu", -12345678UL, 0UL, 12345678UL), "");

    EXPECT_TRUE(test_printf("longlong: -12345678 0 12345678",
                            "longlong: %lli %lli %lli", -12345678LL, 0LL, 12345678LL), "");
    EXPECT_TRUE(test_printf("ulonglong: 18446744073697205938 0 12345678",
                            "ulonglong: %llu %llu %llu", -12345678LL, 0LL, 12345678LL), "");
    EXPECT_TRUE(test_printf("ssize_t: -12345678 0 12345678",
                            "ssize_t: %zd %zd %zd", (ssize_t)-12345678, (ssize_t)0, (ssize_t)12345678), "");
    EXPECT_TRUE(test_printf("usize_t: 18446744073697205938 0 12345678",
                            "usize_t: %zu %zu %zu", (size_t)-12345678, (size_t)0, (size_t)12345678), "");
    EXPECT_TRUE(test_printf("intmax_t: -12345678 0 12345678",
                            "intmax_t: %jd %jd %jd", (intmax_t)-12345678, (intmax_t)0, (intmax_t)12345678), "");
    EXPECT_TRUE(test_printf("uintmax_t: 18446744073697205938 0 12345678",
                            "uintmax_t: %ju %ju %ju", (uintmax_t)-12345678, (uintmax_t)0, (uintmax_t)12345678), "");
    EXPECT_TRUE(test_printf("ptrdiff_t: -12345678 0 12345678",
                            "ptrdiff_t: %td %td %td", (ptrdiff_t)-12345678, (ptrdiff_t)0, (ptrdiff_t)12345678), "");
    EXPECT_TRUE(test_printf("ptrdiff_t (u): 18446744073697205938 0 12345678",
                            "ptrdiff_t (u): %tu %tu %tu", (ptrdiff_t)-12345678, (ptrdiff_t)0, (ptrdiff_t)12345678), "");

    printf("hex:\n");
    EXPECT_TRUE(test_printf("uint8: f4 0 fe",
                            "uint8: %hhx %hhx %hhx", -12, 0, 254), "");
    EXPECT_TRUE(test_printf("uint16:fb2e 0 4d2",
                            "uint16:%hx %hx %hx", -1234, 0, 1234), "");
    EXPECT_TRUE(test_printf("uint:  ff439eb2 0 bc614e",
                            "uint:  %x %x %x", -12345678, 0, 12345678), "");
    EXPECT_TRUE(test_printf("ulong: ffffffffff439eb2 0 bc614e",
                            "ulong: %lx %lx %lx", -12345678UL, 0UL, 12345678UL), "");
    EXPECT_TRUE(test_printf("ulong: FF439EB2 0 BC614E",
                            "ulong: %X %X %X", -12345678, 0, 12345678), "");
    EXPECT_TRUE(test_printf("ulonglong: ffffffffff439eb2 0 bc614e",
                            "ulonglong: %llx %llx %llx", -12345678LL, 0LL, 12345678LL), "");
    EXPECT_TRUE(test_printf("usize_t: ffffffffff439eb2 0 bc614e",
                            "usize_t: %zx %zx %zx", (size_t)-12345678, (size_t)0, (size_t)12345678), "");

    printf("alt/sign:\n");
    EXPECT_TRUE(test_printf("uint: 0xabcdef 0XABCDEF",
                            "uint: %#x %#X", 0xabcdef, 0xabcdef), "");
    EXPECT_TRUE(test_printf("int: +12345678 -12345678",
                            "int: %+d %+d", 12345678, -12345678), "");
    EXPECT_TRUE(test_printf("int:  12345678 +12345678",
                            "int: % d %+d", 12345678, 12345678), "");

    printf("formatting\n");
    EXPECT_TRUE(test_printf("int: a12345678a",
                            "int: a%8da", 12345678), "");
    EXPECT_TRUE(test_printf("int: a 12345678a",
                            "int: a%9da", 12345678), "");
    EXPECT_TRUE(test_printf("int: a12345678 a",
                            "int: a%-9da", 12345678), "");
    EXPECT_TRUE(test_printf("int: a  12345678a",
                            "int: a%10da", 12345678), "");
    EXPECT_TRUE(test_printf("int: a12345678  a",
                            "int: a%-10da", 12345678), "");
    EXPECT_TRUE(test_printf("int: a012345678a",
                            "int: a%09da", 12345678), "");
    EXPECT_TRUE(test_printf("int: a0012345678a",
                            "int: a%010da", 12345678), "");
    EXPECT_TRUE(test_printf("int: a12345678a",
                            "int: a%6da", 12345678), "");

    EXPECT_TRUE(test_printf("aba",
                            "a%1sa", "b"), "");
    EXPECT_TRUE(test_printf("a        ba",
                            "a%9sa", "b"), "");
    EXPECT_TRUE(test_printf("ab        a",
                            "a%-9sa", "b"), "");
    EXPECT_TRUE(test_printf("athisisatesta",
                            "a%5sa", "thisisatest"), "");

    EXPECT_TRUE(test_printf("-02",
                            "%03d", -2), "");
    EXPECT_TRUE(test_printf("-02",
                            "%0+3d", -2), "");
    EXPECT_TRUE(test_printf("+02",
                            "%0+3d", 2), "");
    EXPECT_TRUE(test_printf(" +2",
                            "%+3d", 2), "");
    EXPECT_TRUE(test_printf("-2000",
                            "% 3d", -2000), "");
    EXPECT_TRUE(test_printf(" 2000",
                            "% 3d", 2000), "");
    EXPECT_TRUE(test_printf("+2000",
                            "%+3d", 2000), "");
    EXPECT_TRUE(test_printf("      test",
                            "%10s", "test"), "");
    EXPECT_TRUE(test_printf("      test",
                            "%010s", "test"), "");
    EXPECT_TRUE(test_printf("test      ",
                            "%-10s", "test"), "");
    EXPECT_TRUE(test_printf("test      ",
                            "%-010s", "test"), "");

    END_TEST;
}

static bool printf_field_width_test() {
    BEGIN_TEST;

    char input[] = "0123456789";
    EXPECT_TRUE(test_printf("", "%.", input), "");
    EXPECT_TRUE(test_printf("", "%.s", input), "");
    EXPECT_TRUE(test_printf("'0'", "'%.*s'", 1, input), "");
    EXPECT_TRUE(test_printf("'01'", "'%.*s'", 2, input), "");
    EXPECT_TRUE(test_printf("'012'", "'%.*s'", 3, input), "");
    EXPECT_TRUE(test_printf("'0123'", "'%.*s'", 4, input), "");
    EXPECT_TRUE(test_printf("'01234'", "'%.*s'", 5, input), "");
    EXPECT_TRUE(test_printf("'012345'", "'%.*s'", 6, input), "");
    EXPECT_TRUE(test_printf("'0123456'", "'%.*s'", 7, input), "");
    EXPECT_TRUE(test_printf("'01234567'", "'%.*s'", 8, input), "");
    EXPECT_TRUE(test_printf("'012345678'", "'%.*s'", 9, input), "");
    EXPECT_TRUE(test_printf("'0123456789'", "'%.*s'", 10, input), "");

    END_TEST;
}

// Test snprintf() when the output is larger than the given buffer.
static bool snprintf_truncation_test() {
    BEGIN_TEST;

    char buf[32];

    memset(buf, 'x', sizeof(buf));
    static const char str[26] = "0123456789abcdef012345678";
    int shorter_length = 15;
    int result = snprintf(buf, shorter_length, "%s", str);

    // Check that snprintf() returns the length of the string that it would
    // have written if the buffer was big enough.
    EXPECT_EQ(result, (int)strlen(str), "");

    // Check that snprintf() wrote a truncated, terminated string.
    EXPECT_EQ(memcmp(buf, str, shorter_length - 1), 0, "");
    EXPECT_EQ(buf[shorter_length - 1], '\0', "");

    // Check that snprintf() did not overrun the buffer it was given.
    for (uint32_t i = shorter_length; i < sizeof(buf); ++i)
        EXPECT_EQ(buf[i], 'x', "");

    END_TEST;
}

UNITTEST_START_TESTCASE(printf_tests)
UNITTEST("printf_tests", printf_tests)
UNITTEST("printf_field_width_tests", printf_field_width_test)
UNITTEST("snprintf_truncation_test", snprintf_truncation_test)
UNITTEST_END_TESTCASE(printf_tests, "printf_tests", "printf_tests");
