// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/feature.h>
#include <assert.h>
#include <lib/unittest/unittest.h>
#include <stddef.h>

extern "C" {

extern void* memcpy(void*, const void*, size_t);
extern void* memcpy_erms(void*, const void*, size_t);
extern void* memcpy_quad(void*, const void*, size_t);

extern void* memset(void*, int, size_t);
extern void* memset_erms(void*, int, size_t);
extern void* memset_quad(void*, int, size_t);

}

typedef void* (*memcpy_func_t)(void*, const void*, size_t);
typedef void* (*memset_func_t)(void*, int, size_t);

// Initializes buf with |fill_len| bytes of |fill|, and pads the remaining
// |len - fill_len| bytes with 0xff.
static void initialize_buffer(char* buf, size_t len, char fill, size_t fill_len) {
    for (size_t i = 0; i < fill_len; ++i) {
        buf[i] = fill;
    }
    for (size_t i = fill_len; i < len; ++i) {
        buf[i] = static_cast<char>(0xff);
    }
}

static bool memcpy_func_test(memcpy_func_t cpy) {
    BEGIN_TEST;

    // Test buffers for sizes from 0 to 64
    constexpr size_t kBufLen = 64;
    for (size_t len = 0; len < kBufLen; ++len) {
        // Give the buffers an extra byte so we can check we're not copying
        // excess.
        char src[kBufLen + 1];
        char dst[kBufLen + 1] = { 0 };

        initialize_buffer(src, sizeof(src), static_cast<char>(len + 1), len);
        cpy(dst, src, len);
        ASSERT_TRUE(!memcmp(src, dst, len), "buffer mismatch");
        for (size_t i = len; i < sizeof(dst); ++i) {
            ASSERT_EQ(0, dst[i], "coppied padding");
        }
    }

    // Test alignment offsets relative to 8 bytes.
    for (size_t dst_offset = 0; dst_offset < 8; ++dst_offset) {
        for (size_t src_offset = 0; src_offset < 8; ++src_offset) {
            // Give the buffers an extra byte so we can check we're not copying
            // excess.
            char src[kBufLen + 1];
            // Give the destination an extra 8 bytes so we don't need to worry
            // about the case where src_offset = 0 and dst_offset = 7.
            char dst[kBufLen + 1 + 8] = { 0 };

            for (size_t i = 0; i < src_offset; ++i) {
                src[i] = static_cast<char>(0xff);
            }
            for (size_t i = src_offset; i < kBufLen; ++i) {
                src[i] = static_cast<char>(i - src_offset + 1);
            }
            src[kBufLen] = static_cast<char>(0xff);

            const size_t cpy_len = kBufLen - src_offset;
            cpy(dst + dst_offset, src + src_offset, cpy_len);
            for (size_t i = 0; i < dst_offset; ++i) {
                ASSERT_EQ(0, dst[i], "overwrote before buffer");
            }
            for (size_t i = dst_offset; i < dst_offset + cpy_len; ++i) {
                ASSERT_EQ(static_cast<char>(i - dst_offset + 1), dst[i], "buffer mismatch");
            }
            for (size_t i = dst_offset + cpy_len; i < sizeof(dst); ++i) {
                ASSERT_EQ(0, dst[i], "overwrote after buffer");
            }
        }
    }

    END_TEST;
}

static bool memset_func_test(memset_func_t set) {
    BEGIN_TEST;

    // Test buffers for sizes from 0 to 64
    constexpr size_t kBufLen = 64;
    for (size_t len = 0; len < kBufLen; ++len) {
        // Give the buffer an extra byte so we can check we're not copying
        // excess.
        char dst[kBufLen + 1] = { 0 };

        set(dst, static_cast<int>(len + 1), len);
        for (size_t i = 0; i < len; ++i) {
            ASSERT_EQ(static_cast<char>(len + 1), dst[i], "buffer mismatch");
        }
        for (size_t i = len; i < sizeof(dst); ++i) {
            ASSERT_EQ(0, dst[i], "overwrote padding");
        }
    }

    // Test all fill values
    for (int fill = 0; fill < 0x100; ++fill) {
        char dst[kBufLen] = { static_cast<char>(fill + 1) };
        set(dst, fill, sizeof(dst));
        for (size_t i = 0; i < kBufLen; ++i) {
            ASSERT_EQ(static_cast<char>(fill), dst[i], "buffer mismatch");
        }
    }

    // Test all alignment offsets relative to 8 bytes.
    for (size_t offset = 0; offset < 8; ++offset) {
        // Give the buffer an extra byte so we can check we're not copying
        // excess.
        char dst[kBufLen + 1] = { 0 };

        set(dst + offset, static_cast<int>(kBufLen - offset), kBufLen - offset);
        for (size_t i = 0; i < offset; ++i) {
            ASSERT_EQ(0, dst[i], "overwrote before buffer");
        }
        for (size_t i = offset; i < kBufLen; ++i) {
            ASSERT_EQ(static_cast<char>(kBufLen - offset), dst[i], "buffer mismatch");
        }
        for (size_t i = kBufLen; i < sizeof(dst); ++i) {
            ASSERT_EQ(0, dst[i], "overwrote after buffer");
        }
    }

    END_TEST;
}

static bool memcpy_test() {
    return memcpy_func_test(memcpy);
}

static bool memcpy_quad_test() {
    return memcpy_func_test(memcpy_quad);
}

static bool memcpy_erms_test() {
    if (!x86_feature_test(X86_FEATURE_ERMS)) {
        return true;
    }

    return memcpy_func_test(memcpy_erms);
}

static bool memset_test() {
    return memset_func_test(memset);
}

static bool memset_quad_test() {
    return memset_func_test(memset_quad);
}

static bool memset_erms_test() {
    if (!x86_feature_test(X86_FEATURE_ERMS)) {
        return true;
    }

    return memset_func_test(memset_erms);
}

UNITTEST_START_TESTCASE(memops_tests)
UNITTEST("memcpy tests", memcpy_test)
UNITTEST("memcpy_quad tests", memcpy_quad_test)
UNITTEST("memcpy_erms tests", memcpy_erms_test)
UNITTEST("memset tests", memset_test)
UNITTEST("memset_quad tests", memset_quad_test)
UNITTEST("memset_erms tests", memset_erms_test)
UNITTEST_END_TESTCASE(memops_tests, "memops_tests", "memcpy/memset tests");
