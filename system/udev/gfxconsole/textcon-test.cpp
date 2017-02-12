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
    TextconHelper(uint32_t size_x, uint32_t size_y) : size_x(size_x),
                                                      size_y(size_y) {
        textbuf = new vc_char_t[size_x * size_y];
        textcon.invalidate = invalidate_callback;
        textcon.movecursor = movecursor_callback;
        textcon.pushline = pushline_callback;
        textcon.scroll = scroll_callback;
        textcon.setparam = setparam_callback;
        tc_init(&textcon, size_x, size_y, textbuf, 0, 0);

        // Initialize buffer contents, since this is currently done
        // outside of textcon.cpp in vc-device.cpp.
        for (size_t i = 0; i < size_x * size_y; ++i)
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
        EXPECT_LE(len, size_x, "");
        for (size_t i = 0; i < len; ++i)
            EXPECT_EQ(str[i], textbuf[size_x * line_num + i], "");
        // The rest of the line should contain spaces.
        for (size_t i = len; i < size_x; ++i)
            EXPECT_EQ(' ', textbuf[size_x * line_num + i], "");
    }

    uint32_t size_x;
    uint32_t size_y;
    vc_char_t* textbuf;
    textcon_t textcon = {};
};

bool test_simple() {
    BEGIN_TEST;

    TextconHelper tc(10, 5);
    tc.PutString("Hello");
    tc.AssertLineContains(0, "Hello");
    tc.AssertLineContains(1, "");

    END_TEST;
}

bool test_wrapping() {
    BEGIN_TEST;

    TextconHelper tc(10, 5);
    tc.PutString("Hello world! More text here.");
    tc.AssertLineContains(0, "Hello worl");
    tc.AssertLineContains(1, "d! More te");
    tc.AssertLineContains(2, "xt here.");

    END_TEST;
}

bool test_tabs() {
    BEGIN_TEST;

    TextconHelper tc(80, 40);
    tc.PutString("\tA\n");
    tc.PutString(" \tB\n");
    tc.PutString("       \tC\n"); // 7 spaces
    tc.PutString("        \tD\n"); // 8 spaces
    tc.AssertLineContains(0, "        A");
    tc.AssertLineContains(1, "        B");
    tc.AssertLineContains(2, "        C");
    tc.AssertLineContains(3, "                D");

    END_TEST;
}

bool test_backspace_moves_cursor() {
    BEGIN_TEST;

    TextconHelper tc(10, 5);
    tc.PutString("ABCDEF\b\b\b\bxy");
    // Backspace only moves the cursor and does not erase, so "EF" is left
    // in place.
    tc.AssertLineContains(0, "ABxyEF");

    END_TEST;
}

bool test_backspace_at_start_of_line() {
    BEGIN_TEST;

    TextconHelper tc(10, 5);
    tc.PutString("Foo\n\bBar");
    // When the cursor is at the start of a line, backspace has no effect.
    tc.AssertLineContains(0, "Foo");
    tc.AssertLineContains(1, "Bar");

    END_TEST;
}

BEGIN_TEST_CASE(gfxconsole_textbuf_tests)
RUN_TEST(test_simple)
RUN_TEST(test_wrapping)
RUN_TEST(test_tabs)
RUN_TEST(test_backspace_moves_cursor)
RUN_TEST(test_backspace_at_start_of_line)
END_TEST_CASE(gfxconsole_textbuf_tests)

}

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
