// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>

#include <fbl/type_support.h>
#include <unittest/unittest.h>

// stdint.h defines the following types:
// Fixed width types:
// int8_t
// int16_t
// int32_t
// int64_t
// uint8_t
// uint16_t
// uint32_t
// uint64_t

// Minimum width types:
// int_least8_t
// int_least16_t
// int_least32_t
// int_least64_t
// uint_least8_t
// uint_least16_t
// uint_least32_t
// uint_least64_t

// Fast minimum width types:
// int_fast8_t
// int_fast16_t
// int_fast32_t
// int_fast64_t
// uint_fast8_t
// uint_fast16_t
// uint_fast32_t
// uint_fast64_t

// Pointer-sized types:
// intptr_t
// uintptr_t

// Maximum-width types:
// intmax_t
// uintmax_t

// stddef.h defines the following types:
// ptrdiff_t
// size_t
// wchar_t

// signal.h defines the following type:
// sig_atomic_t

// Test sizes.
static_assert(CHAR_BIT == 8, "");

static_assert(sizeof(int8_t) == 1, "");
static_assert(sizeof(int16_t) == 2, "");
static_assert(sizeof(int32_t) == 4, "");
static_assert(sizeof(int64_t) == 8, "");
static_assert(sizeof(uint8_t) == 1, "");
static_assert(sizeof(uint16_t) == 2, "");
static_assert(sizeof(uint32_t) == 4, "");
static_assert(sizeof(uint64_t) == 8, "");

static_assert(sizeof(int_least8_t) >= 1, "");
static_assert(sizeof(int_least16_t) >= 2, "");
static_assert(sizeof(int_least32_t) >= 4, "");
static_assert(sizeof(int_least64_t) >= 8, "");
static_assert(sizeof(uint_least8_t) >= 1, "");
static_assert(sizeof(uint_least16_t) >= 2, "");
static_assert(sizeof(uint_least32_t) >= 4, "");
static_assert(sizeof(uint_least64_t) >= 8, "");

static_assert(sizeof(int_fast8_t) >= 1, "");
static_assert(sizeof(int_fast16_t) >= 2, "");
static_assert(sizeof(int_fast32_t) >= 4, "");
static_assert(sizeof(int_fast64_t) >= 8, "");
static_assert(sizeof(uint_fast8_t) >= 1, "");
static_assert(sizeof(uint_fast16_t) >= 2, "");
static_assert(sizeof(uint_fast32_t) >= 4, "");
static_assert(sizeof(uint_fast64_t) >= 8, "");

static_assert(sizeof(intptr_t) == sizeof(void*), "");
static_assert(sizeof(uintptr_t) == sizeof(void*), "");

static_assert(sizeof(intmax_t) == 8, "");
static_assert(sizeof(uintmax_t) == 8, "");

static_assert(sizeof(ptrdiff_t) == sizeof(void*), "");
static_assert(sizeof(size_t) == sizeof(void*), "");

// No interesting guarantees can be made about the sizes of these:
// wchar_t
// sig_atomic_t

// Check maximums.
#define CHECK_MAX_TYPE(type, TYPE, max_t, MAX) \
    ((max_t)TYPE##_MAX == (MAX >> (CHAR_BIT * (sizeof(max_t) - sizeof(type)))))

#define CHECK_MAX_UNSIGNED(type, TYPE) CHECK_MAX_TYPE(type, TYPE, uintmax_t, UINTMAX_MAX)
#define CHECK_MAX_SIGNED(type, TYPE) CHECK_MAX_TYPE(type, TYPE, intmax_t, INTMAX_MAX)

#define CHECK_MAX(type, TYPE) static_assert(fbl::is_signed<type>::value ? CHECK_MAX_SIGNED(type, TYPE) : CHECK_MAX_UNSIGNED(type, TYPE), "");

CHECK_MAX(int8_t, INT8);
CHECK_MAX(int16_t, INT16);
CHECK_MAX(int32_t, INT32);
CHECK_MAX(int64_t, INT64);
CHECK_MAX(uint8_t, UINT8);
CHECK_MAX(uint16_t, UINT16);
CHECK_MAX(uint32_t, UINT32);
CHECK_MAX(uint64_t, UINT64);

CHECK_MAX(int_least8_t, INT_LEAST8);
CHECK_MAX(int_least16_t, INT_LEAST16);
CHECK_MAX(int_least32_t, INT_LEAST32);
CHECK_MAX(int_least64_t, INT_LEAST64);
CHECK_MAX(uint_least8_t, UINT_LEAST8);
CHECK_MAX(uint_least16_t, UINT_LEAST16);
CHECK_MAX(uint_least32_t, UINT_LEAST32);
CHECK_MAX(uint_least64_t, UINT_LEAST64);

CHECK_MAX(int_fast8_t, INT_FAST8);
CHECK_MAX(int_fast16_t, INT_FAST16);
CHECK_MAX(int_fast32_t, INT_FAST32);
CHECK_MAX(int_fast64_t, INT_FAST64);
CHECK_MAX(uint_fast8_t, UINT_FAST8);
CHECK_MAX(uint_fast16_t, UINT_FAST16);
CHECK_MAX(uint_fast32_t, UINT_FAST32);
CHECK_MAX(uint_fast64_t, UINT_FAST64);

CHECK_MAX(intptr_t, INTPTR);
CHECK_MAX(uintptr_t, UINTPTR);

CHECK_MAX(intmax_t, INTMAX);
CHECK_MAX(uintmax_t, UINTMAX);

CHECK_MAX(ptrdiff_t, PTRDIFF);
CHECK_MAX(size_t, SIZE);
CHECK_MAX(wchar_t, WCHAR);

CHECK_MAX(sig_atomic_t, SIG_ATOMIC);

// Check minimums.
#define CHECK_MIN_TYPE(type, TYPE)                                   \
    ((intmax_t)TYPE##_MIN == (INTMAX_MIN >> (CHAR_BIT * (sizeof(intmax_t) - sizeof(type)))))

#define CHECK_MIN(type, TYPE) static_assert(CHECK_MIN_TYPE(type, TYPE), "")

CHECK_MIN(int8_t, INT8);
CHECK_MIN(int16_t, INT16);
CHECK_MIN(int32_t, INT32);
CHECK_MIN(int64_t, INT64);

CHECK_MIN(int_least8_t, INT_LEAST8);
CHECK_MIN(int_least16_t, INT_LEAST16);
CHECK_MIN(int_least32_t, INT_LEAST32);
CHECK_MIN(int_least64_t, INT_LEAST64);

CHECK_MIN(int_fast8_t, INT_FAST8);
CHECK_MIN(int_fast16_t, INT_FAST16);
CHECK_MIN(int_fast32_t, INT_FAST32);
CHECK_MIN(int_fast64_t, INT_FAST64);

CHECK_MIN(intptr_t, INTPTR);

CHECK_MIN(intmax_t, INTMAX);

CHECK_MIN(ptrdiff_t, PTRDIFF);
static_assert(fbl::is_signed<wchar_t>::value ? CHECK_MIN_TYPE(wchar_t, WCHAR) : (WCHAR_MIN == 0), "");

static_assert(fbl::is_signed<sig_atomic_t>::value ? CHECK_MIN_TYPE(sig_atomic_t, SIG_ATOMIC) : (SIG_ATOMIC_MIN == 0), "");

// The INTN_C and UINTN_C macros expand into integer constants
// "corresponding to the type int_leastN_t" and "uint_leastN_t"
// respectively.

static_assert(INT8_C(0) == fbl::integral_constant<int_least8_t, 0>::value, "");
static_assert(INT8_C(-0x7f - 1) == fbl::integral_constant<int_least8_t, -0x7f - 1>::value, "");
static_assert(INT8_C(0x7f) == fbl::integral_constant<int_least8_t, 0x7f>::value, "");

static_assert(INT16_C(0) == fbl::integral_constant<int_least16_t, 0>::value, "");
static_assert(INT16_C(-0x7fff - 1) == fbl::integral_constant<int_least16_t, -0x7fff - 1>::value, "");
static_assert(INT16_C(0x7fff) == fbl::integral_constant<int_least16_t, 0x7fff>::value, "");

static_assert(INT32_C(0) == fbl::integral_constant<int_least32_t, 0>::value, "");
static_assert(INT32_C(-0x7fffffff - 1) == fbl::integral_constant<int_least32_t, -0x7fffffff - 1>::value, "");
static_assert(INT32_C(0x7fffffff) == fbl::integral_constant<int_least32_t, 0x7fffffff>::value, "");

static_assert(INT64_C(0) == fbl::integral_constant<int_least64_t, 0>::value, "");
static_assert(INT64_C(-0x7fffffffffffffff - 1) == fbl::integral_constant<int_least64_t, -0x7fffffffffffffff - 1>::value, "");
static_assert(INT64_C(0x7fffffffffffffff) == fbl::integral_constant<int_least64_t, 0x7fffffffffffffff>::value, "");


static_assert(UINT8_C(0) == fbl::integral_constant<uint_least8_t, 0>::value, "");
static_assert(UINT8_C(0xff) == fbl::integral_constant<uint_least8_t, 0xff>::value, "");

static_assert(UINT16_C(0) == fbl::integral_constant<uint_least16_t, 0>::value, "");
static_assert(UINT16_C(0xffff) == fbl::integral_constant<uint_least16_t, 0xffff>::value, "");

static_assert(UINT32_C(0) == fbl::integral_constant<uint_least32_t, 0>::value, "");
static_assert(UINT32_C(0xffffffff) == fbl::integral_constant<uint_least32_t, 0xffffffff>::value, "");

static_assert(UINT64_C(0) == fbl::integral_constant<uint_least64_t, 0>::value, "");
static_assert(UINT64_C(0xffffffffffffffff) == fbl::integral_constant<uint_least64_t, 0xffffffffffffffff>::value, "");


// Unlike the above, the INTMAX_C and UINTMAX_C macros explicitly
// produce values of type intmax_t and uintmax_t respectively, not
// just compatible values.

static_assert(fbl::is_same<intmax_t, decltype(INTMAX_C(0))>::value, "");
static_assert(fbl::is_same<intmax_t, decltype(INTMAX_C(-0x7fffffffffffffff - 1))>::value, "");
static_assert(fbl::is_same<intmax_t, decltype(INTMAX_C(0x7fffffffffffffff))>::value, "");
static_assert(INTMAX_C(0) == fbl::integral_constant<intmax_t, 0>::value, "");
static_assert(INTMAX_C(-0xffffffffffffffff - 1) == fbl::integral_constant<intmax_t, -0xffffffffffffffff - 1>::value, "");
static_assert(INTMAX_C(0x7fffffffffffffff) == fbl::integral_constant<intmax_t, 0x7fffffffffffffff>::value, "");

static_assert(fbl::is_same<uintmax_t, decltype(UINTMAX_C(0))>::value, "");
static_assert(fbl::is_same<uintmax_t, decltype(UINTMAX_C(0xffffffffffffffff))>::value, "");
static_assert(UINTMAX_C(0) == fbl::integral_constant<uintmax_t, 0>::value, "");
static_assert(UINTMAX_C(0xffffffffffffffff) == fbl::integral_constant<uintmax_t, 0xffffffffffffffff>::value, "");


// Check PRI* and SCN* format strings.
#define LAST(fmt) (fmt[strlen(fmt) - 1])

// Signed format specifiers.
#define CHECK_d(fmt) EXPECT_EQ(LAST(fmt), 'd', "incorrect format specifier")
#define CHECK_i(fmt) EXPECT_EQ(LAST(fmt), 'i', "incorrect format specifier")

// Unsigned format specifiers. Note that X is only used by printf, and
// not also by scanf.
#define CHECK_o(fmt) EXPECT_EQ(LAST(fmt), 'o', "incorrect format specifier")
#define CHECK_u(fmt) EXPECT_EQ(LAST(fmt), 'u', "incorrect format specifier")
#define CHECK_x(fmt) EXPECT_EQ(LAST(fmt), 'x', "incorrect format specifier")
#define CHECK_X(fmt) EXPECT_EQ(LAST(fmt), 'X', "incorrect format specifier")

#define CHECK_FORMAT_STRINGS(pri, scn, pcheck, scheck, type, max) do { \
        pcheck(pri);                                                    \
        scheck(scn);                                                    \
        char buf[256] = {0};                                            \
        ASSERT_GT(snprintf(buf, sizeof(buf), "%" pri, (type)max), 1); \
        type n = (type)0;                                               \
        ASSERT_EQ(sscanf(buf, "%" scn, &n), 1);                     \
        ASSERT_EQ(n, max);                                          \
    } while (0)

#define CHECK_SIGNED_FORMATS(size, type, max) do {                      \
        CHECK_FORMAT_STRINGS(PRId ## size, SCNd ## size, CHECK_d, CHECK_d, type, max); \
        CHECK_FORMAT_STRINGS(PRIi ## size, SCNi ## size, CHECK_i, CHECK_i, type, max); \
    } while (0)

// Since X is not used by scanf (only x), the last line has PRIX but
// SCNx (upper and lower case).
#define CHECK_UNSIGNED_FORMATS(size, type, max) do {                    \
        CHECK_FORMAT_STRINGS(PRIo ## size, SCNo ## size, CHECK_o, CHECK_o, type, max); \
        CHECK_FORMAT_STRINGS(PRIu ## size, SCNu ## size, CHECK_u, CHECK_u, type, max); \
        CHECK_FORMAT_STRINGS(PRIx ## size, SCNx ## size, CHECK_x, CHECK_x, type, max); \
        CHECK_FORMAT_STRINGS(PRIX ## size, SCNx ## size, CHECK_X, CHECK_x, type, max); \
    } while (0)

#define CHECK_FORMATS(size, type, max) do {             \
        if (fbl::is_signed<type>::value) {             \
            CHECK_SIGNED_FORMATS(size, type, max);      \
        } else {                                        \
            CHECK_UNSIGNED_FORMATS(size, type, max);    \
        }                                               \
    } while (0)

static bool check_format_specifiers(void) {
    BEGIN_TEST;

    CHECK_FORMATS(8, int8_t, INT8_MAX);
    CHECK_FORMATS(16, int16_t, INT16_MAX);
    CHECK_FORMATS(32, int32_t, INT32_MAX);
    CHECK_FORMATS(64, int64_t, INT64_MAX);
    CHECK_FORMATS(8, uint8_t, UINT8_MAX);
    CHECK_FORMATS(16, uint16_t, UINT16_MAX);
    CHECK_FORMATS(32, uint32_t, UINT32_MAX);
    CHECK_FORMATS(64, uint64_t, UINT64_MAX);

    CHECK_FORMATS(FAST8, int_fast8_t, INT_FAST8_MAX);
    CHECK_FORMATS(FAST16, int_fast16_t, INT_FAST16_MAX);
    CHECK_FORMATS(FAST32, int_fast32_t, INT_FAST32_MAX);
    CHECK_FORMATS(FAST64, int_fast64_t, INT_FAST64_MAX);
    CHECK_FORMATS(FAST8, uint_fast8_t, UINT_FAST8_MAX);
    CHECK_FORMATS(FAST16, uint_fast16_t, UINT_FAST16_MAX);
    CHECK_FORMATS(FAST32, uint_fast32_t, UINT_FAST32_MAX);
    CHECK_FORMATS(FAST64, uint_fast64_t, UINT_FAST64_MAX);

    CHECK_FORMATS(LEAST8, int_least8_t, INT_LEAST8_MAX);
    CHECK_FORMATS(LEAST16, int_least16_t, INT_LEAST16_MAX);
    CHECK_FORMATS(LEAST32, int_least32_t, INT_LEAST32_MAX);
    CHECK_FORMATS(LEAST64, int_least64_t, INT_LEAST64_MAX);
    CHECK_FORMATS(LEAST8, uint_least8_t, UINT_LEAST8_MAX);
    CHECK_FORMATS(LEAST16, uint_least16_t, UINT_LEAST16_MAX);
    CHECK_FORMATS(LEAST32, uint_least32_t, UINT_LEAST32_MAX);
    CHECK_FORMATS(LEAST64, uint_least64_t, UINT_LEAST64_MAX);

    CHECK_FORMATS(PTR, intptr_t, INTPTR_MAX);
    CHECK_FORMATS(PTR, uintptr_t, UINTPTR_MAX);

    // Note that these are definined in addition to %j
    CHECK_FORMATS(MAX, intmax_t, INTMAX_MAX);
    CHECK_FORMATS(MAX, uintmax_t, UINTMAX_MAX);

    // ptrdiff_t is simply %t.
    // size_t is simply %z.
    // wchar_t is simply %sl.

    // No particular format specifier or macro is defined for sig_atomic_t.

    END_TEST;
}

BEGIN_TEST_CASE(format_specifiers)
RUN_TEST(check_format_specifiers)
END_TEST_CASE(format_specifiers)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
