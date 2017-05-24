// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gfx/gfx.h>
#include <mxtl/auto_lock.h>
#include <mxtl/unique_ptr.h>
#include <sys/param.h>
#include <unittest/unittest.h>

#include "textcon.h"
#include "vc.h"

namespace {

void invalidate_callback(void* cookie, int x, int y, int w, int h) {
}

void movecursor_callback(void* cookie, int x, int y) {
}

void push_scrollback_line_callback(void* cookie, int y) {
}

void copy_lines_callback(void* cookie, int y_dest, int y_src, int line_count) {
    auto* tc = reinterpret_cast<textcon_t*>(cookie);
    tc_copy_lines(tc, y_dest, y_src, line_count);
}

void setparam_callback(void* cookie, int param, uint8_t* arg, size_t arglen) {
}

// Helper for initializing and testing console instances.  This actually
// creates two console instances:
//
//  * A textcon_t (non-graphical), for testing character-level output.
//  * A vc_t (graphical), for testing incremental updates to the
//    gfx_surface.
//
// In principle, we could test the character-level output via the textcon_t
// that the vc_t creates internally.  However, using our own
// separate textcon_t instance helps check that textcon_t can be used on
// its own, outside of vc_t.
class TextconHelper {
public:
    TextconHelper(uint32_t size_x, uint32_t size_y) : size_x(size_x),
                                                      size_y(size_y) {
        // Create a textcon_t.
        textbuf = new vc_char_t[size_x * size_y];
        textcon.cookie = &textcon;
        textcon.invalidate = invalidate_callback;
        textcon.movecursor = movecursor_callback;
        textcon.push_scrollback_line = push_scrollback_line_callback;
        textcon.copy_lines = copy_lines_callback;
        textcon.setparam = setparam_callback;
        tc_init(&textcon, size_x, size_y, textbuf, 0, 0);
        // Initialize buffer contents, since this is currently done
        // outside of textcon.cpp in vc-device.cpp.
        for (size_t i = 0; i < size_x * size_y; ++i)
            textbuf[i] = ' ';

        // Create a vc_t with the same size in characters.
        const gfx_font* font = vc_get_font();
        int pixels_x = font->width * size_x;
        int pixels_y = font->height * (size_y + 1); // Add 1 for status line.
        // Add margins that aren't large enough to fit a whole column or
        // row at the right and bottom.  This tests incremental update of
        // anything that might be displayed in the margins.
        pixels_x += font->width - 1;
        pixels_y += font->height - 1;
        vc_surface = gfx_create_surface(
            nullptr, pixels_x, pixels_y, /* stride= */ pixels_x,
            MX_PIXEL_FORMAT_RGB_565, 0);
        EXPECT_TRUE(vc_surface, "");
        // This takes ownership of vc_surface.
        EXPECT_EQ(vc_alloc(vc_surface, -1, &vc_dev), NO_ERROR, "");
        EXPECT_EQ(vc_dev->columns, size_x, "");
        EXPECT_EQ(vc_rows(vc_dev), static_cast<int>(size_y), "");
        // Mark the console as active so that display updates get
        // propagated to vc_surface.
        vc_dev->active = true;
        // Propagate the initial display contents to vc_surface.
        vc_gfx_invalidate_all(vc_dev);
    }

    ~TextconHelper() {
        delete[] textbuf;
        vc_free(vc_dev);
    }

    // Takes a snapshot of the vc_t's display.
    class DisplaySnapshot {
    public:
        DisplaySnapshot(TextconHelper* helper)
            : helper_(helper),
              snapshot_(new uint8_t[helper->vc_surface->len]) {
            memcpy(snapshot_.get(), helper->vc_surface->ptr,
                   helper->vc_surface->len);
        }

        // Returns whether the vc_t's display changed since the
        // snapshot was taken.
        bool ChangedSinceSnapshot() {
            return memcmp(snapshot_.get(), helper_->vc_surface->ptr,
                          helper_->vc_surface->len) != 0;
        }

        mxtl::unique_ptr<char[]> ComparisonString() {
            vc_t* vc_dev = helper_->vc_dev;
            gfx_surface* vc_surface = helper_->vc_surface;
            // Add 1 to these sizes to account for the margins.
            uint32_t cmp_size_x = vc_dev->columns + 1;
            uint32_t cmp_size_y = vc_dev->rows + 1;
            uint32_t size_in_chars = cmp_size_x * cmp_size_y;

            mxtl::unique_ptr<bool[]> diffs(new bool[size_in_chars]);
            for (uint32_t i = 0; i < size_in_chars; ++i)
                diffs[i] = false;

            for (uint32_t i = 0; i < vc_surface->len; ++i) {
                if (static_cast<uint8_t*>(vc_surface->ptr)[i] != snapshot_[i]) {
                    uint32_t pixel_index = i / vc_surface->pixelsize;
                    uint32_t x_pixels = pixel_index % vc_surface->stride;
                    uint32_t y_pixels = pixel_index / vc_surface->stride;
                    uint32_t x_chars = x_pixels / vc_dev->charw;
                    uint32_t y_chars = y_pixels / vc_dev->charh;
                    EXPECT_LT(x_chars, cmp_size_x, "");
                    EXPECT_LT(y_chars, cmp_size_y, "");
                    diffs[x_chars + y_chars * cmp_size_x] = true;
                }
            }

            // Build a string showing the differences.  If we had
            // std::string or equivalent, we'd use that here.
            size_t string_size = (cmp_size_x + 3) * cmp_size_y + 1;
            mxtl::unique_ptr<char[]> string(new char[string_size]);
            char* ptr = string.get();
            for (uint32_t y = 0; y < cmp_size_y; ++y) {
                *ptr++ = '|';
                for (uint32_t x = 0; x < cmp_size_x; ++x) {
                    bool diff = diffs[x + y * cmp_size_x];
                    *ptr++ = diff ? 'D' : '-';
                }
                *ptr++ = '|';
                *ptr++ = '\n';
            }
            *ptr++ = 0;
            EXPECT_EQ(ptr, string.get() + string_size, "");
            return string;
        }

        // Prints a representation of which characters in the vc_t's
        // display changed since the snapshot was taken.
        void PrintComparison() {
            printf("%s", ComparisonString().get());
        }

    private:
        TextconHelper* helper_;
        mxtl::unique_ptr<uint8_t[]> snapshot_;
    };

    void InvalidateAllGraphics() {
        vc_invalidate_all_for_testing(vc_dev);
        vc_gfx_invalidate_all(vc_dev);
    }

    void PutString(const char* str) {
        for (const char* ptr = str; *ptr; ++ptr)
            textcon.putc(&textcon, *ptr);

        vc_write(vc_dev, str, strlen(str), 0);
        // Test that the incremental update of the display was correct.  We
        // do that by refreshing the entire display, and checking that
        // there was no change.
        DisplaySnapshot copy(this);
        InvalidateAllGraphics();
        if (copy.ChangedSinceSnapshot()) {
            copy.PrintComparison();
            EXPECT_TRUE(false, "Display contents changed");
        }
    }

    void AssertTextbufLineContains(vc_char_t* buf, int line_num,
                                   const char* str) {
        size_t len = strlen(str);
        EXPECT_LE(len, size_x, "");
        for (size_t i = 0; i < len; ++i)
            EXPECT_EQ(str[i], vc_char_get_char(buf[size_x * line_num + i]), "");
        // The rest of the line should contain spaces.
        for (size_t i = len; i < size_x; ++i)
            EXPECT_EQ(' ', vc_char_get_char(buf[size_x * line_num + i]), "");
    }

    void AssertLineContains(int line_num, const char* str) {
        AssertTextbufLineContains(textbuf, line_num, str);
        AssertTextbufLineContains(vc_dev->text_buf, line_num, str);
    }

    uint32_t size_x;
    uint32_t size_y;

    vc_char_t* textbuf;
    textcon_t textcon = {};

    gfx_surface* vc_surface;
    vc_t* vc_dev;
};

bool test_simple() {
    BEGIN_TEST;

    TextconHelper tc(10, 5);
    tc.PutString("Hello");
    tc.AssertLineContains(0, "Hello");
    tc.AssertLineContains(1, "");

    END_TEST;
}

// This tests the DisplaySnapshot test helper above.  If we write directly
// to vc_dev's text buffer without invalidating the display, the test
// machinery should detect which characters in the display were not updated
// properly.
bool test_display_update_comparison() {
    BEGIN_TEST;

    TextconHelper tc(10, 3);
    // Write some characters directly into the text buffer.
    auto SetChar = [&](int x, int y, char ch) {
        tc.vc_dev->text_buf[x + y * tc.size_x] =
            vc_char_make(ch, tc.textcon.fg, tc.textcon.bg);
    };
    SetChar(2, 1, 'x');
    SetChar(3, 1, 'y');
    SetChar(6, 1, 'z');

    // Check that these characters in the display are detected as not
    // properly updated.
    TextconHelper::DisplaySnapshot snapshot(&tc);
    tc.InvalidateAllGraphics();
    EXPECT_TRUE(snapshot.ChangedSinceSnapshot(), "");
    const char *expected =
        "|-----------|\n"  // Console status line
        "|D----------|\n"  // Cursor
        "|--DD--D----|\n"  // Chars set by SetChar() above
        "|-----------|\n"
        "|-----------|\n"; // Bottom margin
    EXPECT_EQ(strcmp(snapshot.ComparisonString().get(), expected), 0, "");

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

bool test_scroll_up() {
    BEGIN_TEST;

    TextconHelper tc(10, 4);
    tc.PutString("AAA\nBBB\nCCC\nDDD\n");
    tc.AssertLineContains(0, "BBB");
    tc.AssertLineContains(1, "CCC");
    tc.AssertLineContains(2, "DDD");
    tc.AssertLineContains(3, "");
    EXPECT_EQ(vc_get_scrollback_lines(tc.vc_dev), 1, "");

    END_TEST;
}

// Same as scroll_up(), but using ESC E (NEL) instead of "\n".
bool test_scroll_up_nel() {
    BEGIN_TEST;

    TextconHelper tc(10, 4);
    tc.PutString("AAA" "\x1b" "E"
                 "BBB" "\x1b" "E"
                 "CCC" "\x1b" "E"
                 "DDD" "\x1b" "E");
    tc.AssertLineContains(0, "BBB");
    tc.AssertLineContains(1, "CCC");
    tc.AssertLineContains(2, "DDD");
    tc.AssertLineContains(3, "");
    EXPECT_EQ(vc_get_scrollback_lines(tc.vc_dev), 1, "");

    END_TEST;
}

bool test_insert_lines() {
    BEGIN_TEST;

    TextconHelper tc(10, 5);
    tc.PutString("AAA\nBBB\nCCC\nDDD\nEEE");
    tc.PutString("\x1b[2A"); // Move the cursor up 2 lines
    tc.PutString("\x1b[2L"); // Insert 2 lines
    tc.PutString("Z"); // Output char to show where the cursor ends up
    tc.AssertLineContains(0, "AAA");
    tc.AssertLineContains(1, "BBB");
    tc.AssertLineContains(2, "   Z");
    tc.AssertLineContains(3, "");
    tc.AssertLineContains(4, "CCC");
    EXPECT_EQ(vc_get_scrollback_lines(tc.vc_dev), 0, "");

    END_TEST;
}

bool test_delete_lines() {
    BEGIN_TEST;

    TextconHelper tc(10, 5);
    tc.PutString("AAA\nBBB\nCCC\nDDD\nEEE");
    tc.PutString("\x1b[2A"); // Move the cursor up 2 lines
    tc.PutString("\x1b[2M"); // Delete 2 lines
    tc.PutString("Z"); // Output char to show where the cursor ends up
    tc.AssertLineContains(0, "AAA");
    tc.AssertLineContains(1, "BBB");
    tc.AssertLineContains(2, "EEEZ");
    tc.AssertLineContains(3, "");
    tc.AssertLineContains(4, "");
    // TODO(mseaborn): We probably don't want to be adding the deleted
    // lines to the scrollback in this case, because they are not from the
    // top of the console.
    EXPECT_EQ(vc_get_scrollback_lines(tc.vc_dev), 2, "");

    END_TEST;
}

// Test for a bug where this would cause an out-of-bounds array access.
bool test_insert_lines_many() {
    BEGIN_TEST;

    TextconHelper tc(10, 5);
    tc.PutString("AAA\nBBB");
    tc.PutString("\x1b[999L"); // Insert 999 lines
    tc.PutString("Z"); // Output char to show where the cursor ends up
    tc.AssertLineContains(0, "AAA");
    tc.AssertLineContains(1, "   Z");

    END_TEST;
}

// Test for a bug where this would cause an out-of-bounds array access.
bool test_delete_lines_many() {
    BEGIN_TEST;

    TextconHelper tc(10, 5);
    tc.PutString("AAA\nBBB");
    tc.PutString("\x1b[999M"); // Delete 999 lines
    tc.PutString("Z"); // Output char to show where the cursor ends up
    tc.AssertLineContains(0, "AAA");
    tc.AssertLineContains(1, "   Z");

    END_TEST;
}


// Check that passing a huge parameter via "insert lines" completes in a
// reasonable amount of time.  (We don't check the time here but we assume
// that someone will notice if this takes a long time.)
bool test_insert_lines_huge() {
    BEGIN_TEST;

    TextconHelper tc(10, 5);
    tc.PutString("AAA\nBBB");
    tc.PutString("\x1b[2000000000L"); // Insert lines
    tc.PutString("Z"); // Output char to show where the cursor ends up
    tc.AssertLineContains(0, "AAA");
    tc.AssertLineContains(1, "   Z");

    END_TEST;
}

// Check that passing a huge parameter via "delete lines" completes in a
// reasonable amount of time.  (We don't check the time here but we assume
// that someone will notice if this takes a long time.)
bool test_delete_lines_huge() {
    BEGIN_TEST;

    TextconHelper tc(10, 5);
    tc.PutString("AAA\nBBB");
    tc.PutString("\x1b[200000000M"); // Delete lines
    tc.PutString("Z"); // Output char to show where the cursor ends up
    tc.AssertLineContains(0, "AAA");
    tc.AssertLineContains(1, "   Z");

    END_TEST;
}

bool test_move_cursor_up_and_scroll() {
    BEGIN_TEST;

    TextconHelper tc(10, 4);
    tc.PutString("AAA\nBBB\nCCC\nDDD");
    tc.PutString("\x1bM" "1"); // Move cursor up; print char
    tc.PutString("\x1bM" "2"); // Move cursor up; print char
    tc.PutString("\x1bM" "3"); // Move cursor up; print char
    tc.PutString("\x1bM" "4"); // Move cursor up; print char
    tc.AssertLineContains(0, "      4");
    tc.AssertLineContains(1, "AAA  3");
    tc.AssertLineContains(2, "BBB 2");
    tc.AssertLineContains(3, "CCC1");

    END_TEST;
}

bool test_move_cursor_down_and_scroll() {
    BEGIN_TEST;

    TextconHelper tc(10, 4);
    tc.PutString("1" "\x1b" "D"); // Print char; move cursor down
    tc.PutString("2" "\x1b" "D"); // Print char; move cursor down
    tc.PutString("3" "\x1b" "D"); // Print char; move cursor down
    tc.PutString("4" "\x1b" "D"); // Print char; move cursor down
    tc.PutString("5");
    tc.AssertLineContains(0, " 2");
    tc.AssertLineContains(1, "  3");
    tc.AssertLineContains(2, "   4");
    tc.AssertLineContains(3, "    5");

    END_TEST;
}

bool test_cursor_hide_and_show() {
    BEGIN_TEST;

    TextconHelper tc(10, 4);
    ASSERT_FALSE(tc.vc_dev->hide_cursor, "");
    tc.PutString("\x1b[?25l"); // Hide cursor
    ASSERT_TRUE(tc.vc_dev->hide_cursor, "");
    tc.PutString("\x1b[?25h"); // Show cursor
    ASSERT_FALSE(tc.vc_dev->hide_cursor, "");

    END_TEST;
}

// This tests for a bug: If the cursor was positioned over a character when
// we scroll up, that character would get erased.
bool test_cursor_scroll_bug() {
    BEGIN_TEST;

    TextconHelper tc(10, 3);
    // Move the cursor to the bottom line.
    tc.PutString("\n\n\n");
    // Scroll down when the cursor is over "C".
    tc.PutString("ABCDE\b\b\b\n");

    END_TEST;
}

// Test for a bug where scrolling the console viewport by a large delta
// (e.g. going from the top to the bottom) can crash due to out-of-bounds
// memory accesses.
bool test_scroll_viewport_by_large_delta() {
    BEGIN_TEST;

    TextconHelper tc(2, 2);
    tc.PutString("\n");
    for (int lines = 1; lines < 100; ++lines) {
        tc.PutString("\n");

        // Keep the thread checker happy.
        mxtl::AutoLock lock(&g_vc_lock);

        // Scroll up, to show older lines.
        vc_scroll_viewport_top(tc.vc_dev);
        EXPECT_EQ(tc.vc_dev->viewport_y, -lines, "");

        // Scroll down, to show newer lines.
        vc_scroll_viewport_bottom(tc.vc_dev);
        EXPECT_EQ(tc.vc_dev->viewport_y, 0, "");
    }

    END_TEST;
}

// When the console is displaying only the main console region (and no
// scrollback), the console should keep displaying that as new lines are
// outputted.
bool test_viewport_scrolling_follows_bottom() {
    BEGIN_TEST;

    TextconHelper tc(1, 1);
    for (unsigned i = 0; i < tc.vc_dev->scrollback_rows_max * 2; ++i) {
        EXPECT_EQ(tc.vc_dev->viewport_y, 0, "");
        tc.PutString("\n");
    }

    END_TEST;
}

// When the console is displaying some of the scrollback buffer, then as
// new lines are outputted, the console should scroll the viewpoint to keep
// displaying the same point, unless we're at the top of the scrollback
// buffer.
bool test_viewport_scrolling_follows_scrollback() {
    BEGIN_TEST;

    TextconHelper tc(1, 1);
    // Add 3 lines to the scrollback buffer.
    tc.PutString("\n\n\n");
    {
        mxtl::AutoLock lock(&g_vc_lock); // Keep the thread checker happy.
        vc_scroll_viewport(tc.vc_dev, -2);
    }
    EXPECT_EQ(tc.vc_dev->viewport_y, -2, "");
    int limit = tc.vc_dev->scrollback_rows_max;
    for (int line = 3; line < limit * 2; ++line) {
        // Output different strings on each line in order to test that the
        // display is updated consistently when the console starts dropping
        // lines from the scrollback region.
        char str[3] = { static_cast<char>('0' + (line % 10)), '\n', '\0' };
        tc.PutString(str);
        EXPECT_EQ(tc.vc_dev->viewport_y, -MIN(line, limit), "");
    }

    END_TEST;
}

bool test_output_when_viewport_scrolled() {
    BEGIN_TEST;

    TextconHelper tc(10, 3);
    // Line 1 will move into the scrollback region.
    tc.PutString("1\n 2\n  3\n   4");
    EXPECT_EQ(tc.vc_dev->viewport_y, 0, "");
    {
        mxtl::AutoLock lock(&g_vc_lock); // Keep the thread checker happy.
        vc_scroll_viewport_top(tc.vc_dev);
    }
    EXPECT_EQ(tc.vc_dev->viewport_y, -1, "");
    // Check redrawing consistency.
    tc.PutString("");

    // Test that output updates the display correctly when the viewport is
    // scrolled.  Using two separate PutString() calls here was necessary
    // for reproducing an incremental update bug.
    tc.PutString("\x1b[1;1f"); // Move to top left
    tc.PutString("Epilobium");
    tc.AssertLineContains(0, "Epilobium");
    tc.AssertLineContains(1, "  3");
    tc.AssertLineContains(2, "   4");

    // Test that erasing also updates the display correctly.  This
    // changes the console contents without moving the cursor.
    tc.PutString("\b\b\b\b"); // Move cursor left 3 chars
    tc.PutString("\x1b[1K"); // Erase to beginning of line
    tc.AssertLineContains(0, "      ium");
    tc.AssertLineContains(1, "  3");
    tc.AssertLineContains(2, "   4");

    END_TEST;
}

bool test_scrolling_when_viewport_scrolled() {
    BEGIN_TEST;

    TextconHelper tc(10, 3);
    // Line 1 will move into the scrollback region.
    tc.PutString("1\n 2\n  3\n   4");
    EXPECT_EQ(tc.vc_dev->viewport_y, 0, "");
    {
        mxtl::AutoLock lock(&g_vc_lock); // Keep the thread checker happy.
        vc_scroll_viewport_top(tc.vc_dev);
    }
    EXPECT_EQ(tc.vc_dev->viewport_y, -1, "");
    // Check redrawing consistency.
    tc.PutString("");

    // Test that the display is updated correctly when we scroll.
    tc.PutString("\n5");
    tc.AssertLineContains(0, "  3");
    tc.AssertLineContains(1, "   4");
    tc.AssertLineContains(2, "5");

    END_TEST;
}

// Test that vc_get_scrollback_lines() gives the correct results.
bool test_scrollback_lines_count() {
    BEGIN_TEST;

    TextconHelper tc(10, 3);
    tc.PutString("\n\n");

    // Reduce the scrollback limit to make the test faster.
    const int kLimit = 20;
    EXPECT_LE(kLimit, tc.vc_dev->scrollback_rows_max, "");
    tc.vc_dev->scrollback_rows_max = kLimit;

    for (int lines = 1; lines < kLimit * 4; ++lines) {
        tc.PutString("\n");
        EXPECT_EQ(MIN(lines, kLimit),
                  vc_get_scrollback_lines(tc.vc_dev), "");
    }

    END_TEST;
}

// Test that the scrollback lines have the correct contents.
bool test_scrollback_lines_contents() {
    BEGIN_TEST;

    // Use a 1-row-high console, which simplifies this test.
    TextconHelper tc(3, 1);

    // Reduce the scrollback limit to make the test faster.
    const int kLimit = 20;
    EXPECT_LE(kLimit, tc.vc_dev->scrollback_rows_max, "");
    tc.vc_dev->scrollback_rows_max = kLimit;

    vc_char_t test_val = 0;
    for (int lines = 1; lines <= kLimit; ++lines) {
        tc.vc_dev->text_buf[0] = test_val++;
        tc.PutString("\n");

        EXPECT_EQ(lines, vc_get_scrollback_lines(tc.vc_dev), "");
        for (int i = 0; i < lines; ++i)
            EXPECT_EQ(i, vc_get_scrollback_line_ptr(tc.vc_dev, i)[0], "");
    }
    for (int lines = 0; lines < kLimit * 3; ++lines) {
        tc.vc_dev->text_buf[0] = test_val++;
        tc.PutString("\n");

        EXPECT_EQ(kLimit, vc_get_scrollback_lines(tc.vc_dev), "");
        for (int i = 0; i < kLimit; ++i) {
            EXPECT_EQ(test_val + i - kLimit,
                      vc_get_scrollback_line_ptr(tc.vc_dev, i)[0], "");
        }
    }

    END_TEST;
}

BEGIN_TEST_CASE(gfxconsole_textbuf_tests)
RUN_TEST(test_simple)
RUN_TEST(test_display_update_comparison)
RUN_TEST(test_wrapping)
RUN_TEST(test_tabs)
RUN_TEST(test_backspace_moves_cursor)
RUN_TEST(test_backspace_at_start_of_line)
RUN_TEST(test_scroll_up)
RUN_TEST(test_scroll_up_nel)
RUN_TEST(test_insert_lines)
RUN_TEST(test_delete_lines)
RUN_TEST(test_insert_lines_many)
RUN_TEST(test_delete_lines_many)
RUN_TEST(test_insert_lines_huge)
RUN_TEST(test_delete_lines_huge)
RUN_TEST(test_move_cursor_up_and_scroll)
RUN_TEST(test_move_cursor_down_and_scroll)
RUN_TEST(test_cursor_hide_and_show)
RUN_TEST(test_cursor_scroll_bug)
RUN_TEST(test_scroll_viewport_by_large_delta)
RUN_TEST(test_viewport_scrolling_follows_bottom)
RUN_TEST(test_viewport_scrolling_follows_scrollback)
RUN_TEST(test_output_when_viewport_scrolled)
RUN_TEST(test_scrolling_when_viewport_scrolled)
RUN_TEST(test_scrollback_lines_count)
RUN_TEST(test_scrollback_lines_contents)
END_TEST_CASE(gfxconsole_textbuf_tests)

}

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
