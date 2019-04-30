// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <byteswap.h>

#include <zxtest/zxtest.h>

void string_fix(uint16_t* buf, size_t size);

namespace {

TEST(SataTest, StringFixTest) {
    // Nothing to do.
    string_fix(nullptr, 0);

    // Zero length, no swapping happens.
    uint16_t a = 0x1234;
    string_fix(&a, 0);
    ASSERT_EQ(a, 0x1234, "unexpected string result");

    // One character, only swap to even lengths.
    a = 0x1234;
    string_fix(&a, 1);
    ASSERT_EQ(a, 0x1234, "unexpected string result");

    // Swap A.
    a = 0x1234;
    string_fix(&a, sizeof(a));
    ASSERT_EQ(a, 0x3412, "unexpected string result");

    // Swap a group of values.
    uint16_t b[] = { 0x0102, 0x0304, 0x0506 };
    string_fix(b, sizeof(b));
    const uint16_t b_rev[] = { 0x0201, 0x0403, 0x0605 };
    ASSERT_EQ(memcmp(b, b_rev, sizeof(b)), 0, "unexpected string result");

    // Swap a string.
    const char* qemu_model_id = "EQUMH RADDSI K";
    const char* qemu_rev = "QEMU HARDDISK ";
    const size_t qsize = strlen(qemu_model_id);

    union {
        uint16_t word[10];
        char byte[20];
    } str;

    memcpy(str.byte, qemu_model_id, qsize);
    string_fix(str.word, qsize);
    ASSERT_EQ(memcmp(str.byte, qemu_rev, qsize), 0, "unexpected string result");

    const char* sin = "abcdefghijklmnoprstu"; // 20 chars
    const size_t slen = strlen(sin);
    ASSERT_EQ(slen, 20, "bad string length");
    ASSERT_EQ(slen & 1, 0, "string length must be even");
    char sout[22];
    memset(sout, 0, sizeof(sout));
    memcpy(sout, sin, slen);

    // Verify swapping the length of every pair from 0 to 20 chars, inclusive.
    for (size_t i = 0; i <= slen; i += 2) {
        memcpy(str.byte, sin, slen);
        string_fix(str.word, i);
        ASSERT_EQ(memcmp(str.byte, sout, slen), 0, "unexpected string result");
        ASSERT_EQ(sout[slen], 0, "buffer overrun");
        char c = sout[i];
        sout[i] = sout[i+1];
        sout[i+1] = c;
    }
}

} // namespace
