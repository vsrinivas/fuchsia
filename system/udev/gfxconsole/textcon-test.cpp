// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "textcon.h"

namespace {

void invalidate_callback(void* cookie, int x, int y, int w, int h) {
}

void movecursor_callback(void* cookie, int x, int y) {
}

void pushline_callback(void* cookie, int y) {
}

void scroll_callback(void* cookie, int x, int y0, int y1) {
}

void setparam_callback(void* cookie, int param, uint8_t* arg, size_t arglen) {
}

// Helper for initializing a textcon instance for testing.
class TextconHelper {
public:
    TextconHelper() {
        textcon.invalidate = invalidate_callback;
        textcon.movecursor = movecursor_callback;
        textcon.pushline = pushline_callback;
        textcon.scroll = scroll_callback;
        textcon.setparam = setparam_callback;
        tc_init(&textcon, kSizeX, kSizeY, textbuf, 0, 0);

        // Initialize buffer contents, since this is currently done
        // outside of textcon.cpp in vc-device.cpp.
        for (size_t i = 0; i < kSizeX * kSizeY; ++i)
            textbuf[i] = ' ';
    }

    ~TextconHelper() {
        delete[] textbuf;
    }

    void PutString(const char* str) {
        while (*str)
            textcon.putc(&textcon, *str++);
    }

    void AssertLineContains(int line_num, const char* str) {
        size_t len = strlen(str);
        EXPECT_LE(len, kSizeX, "");
        for (size_t i = 0; i < len; ++i)
            EXPECT_EQ(str[i], textbuf[kSizeX * line_num + i], "");
        // The rest of the line should contain spaces.
        for (size_t i = len; i < kSizeX; ++i)
            EXPECT_EQ(' ', textbuf[kSizeX * line_num + i], "");
    }

    static constexpr int kSizeX = 10;
    static constexpr int kSizeY = 5;

    vc_char_t* textbuf = new vc_char_t[kSizeX * kSizeY];
    textcon_t textcon = {};
};

bool test_simple() {
    BEGIN_TEST;

    TextconHelper tc;
    tc.PutString("Hello");
    tc.AssertLineContains(0, "Hello");
    tc.AssertLineContains(1, "");

    END_TEST;
}

bool test_wrapping() {
    BEGIN_TEST;

    TextconHelper tc;
    tc.PutString("Hello world! More text here.");
    tc.AssertLineContains(0, "Hello worl");
    tc.AssertLineContains(1, "d! More te");
    tc.AssertLineContains(2, "xt here.");

    END_TEST;
}

BEGIN_TEST_CASE(gfxconsole_textbuf_tests)
RUN_TEST(test_simple)
RUN_TEST(test_wrapping)
END_TEST_CASE(gfxconsole_textbuf_tests)

}

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
