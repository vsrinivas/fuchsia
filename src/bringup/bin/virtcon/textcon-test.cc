// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "textcon.h"

#include <sys/param.h>

#include <memory>

#include <gfx/gfx.h>
#include <zxtest/zxtest.h>

#include "vc.h"

// The graphics used for testing.
vc_gfx_t main_test_graphics = {};
void vc_attach_to_main_display(vc_t* vc) {
  vc->graphics = &main_test_graphics;
  vc_attach_gfx(vc);
}

// This is needed to satisfy a reference from vc_handle_device_control_keys
// in vc-input.cpp but the code path to that reference is dead in this test.
void vc_toggle_framebuffer() { __builtin_trap(); }

bool g_vc_owns_display = true;

namespace {

void invalidate_callback(void* cookie, int x, int y, int w, int h) {}

void movecursor_callback(void* cookie, int x, int y) {}

void push_scrollback_line_callback(void* cookie, int y) {}

void copy_lines_callback(void* cookie, int y_dest, int y_src, int line_count) {
  auto* tc = reinterpret_cast<textcon_t*>(cookie);
  tc_copy_lines(tc, y_dest, y_src, line_count);
}

void setparam_callback(void* cookie, int param, uint8_t* arg, size_t arglen) {}

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
  TextconHelper(uint32_t size_x, uint32_t size_y,
                const color_scheme_t* color_scheme = &color_schemes[kDefaultColorScheme])
      : size_x(size_x), size_y(size_y) {
    // Create a textcon_t.
    textbuf = new vc_char_t[size_x * size_y];
    textcon.cookie = &textcon;
    textcon.invalidate = invalidate_callback;
    textcon.movecursor = movecursor_callback;
    textcon.push_scrollback_line = push_scrollback_line_callback;
    textcon.copy_lines = copy_lines_callback;
    textcon.setparam = setparam_callback;
    tc_init(&textcon, size_x, size_y, textbuf, color_scheme->front, color_scheme->back, 0, 0);
    // Initialize buffer contents, since this is currently done
    // outside of textcon.cpp in vc-device.cpp.
    for (size_t i = 0; i < size_x * size_y; ++i)
      textbuf[i] = ' ';

    // Create a vc_t with the same size in characters.
    const gfx_font* font = vc_get_font();
    int pixels_x = font->width * size_x;
    int pixels_y = font->height * (size_y + 1);  // Add 1 for status line.
    // Add margins that aren't large enough to fit a whole column or
    // row at the right and bottom.  This tests incremental update of
    // anything that might be displayed in the margins.
    pixels_x += font->width - 1;
    pixels_y += font->height - 1;
    vc_surface = gfx_create_surface(nullptr, pixels_x, pixels_y, /* stride= */ pixels_x,
                                    ZX_PIXEL_FORMAT_RGB_565, 0);
    EXPECT_TRUE(vc_surface);
    // This takes ownership of vc_surface.
    EXPECT_OK(vc_init_gfx(&main_test_graphics, vc_surface));
    EXPECT_OK(vc_alloc(&vc_dev, color_scheme));
    EXPECT_EQ(vc_dev->columns, size_x);
    EXPECT_EQ(vc_rows(vc_dev), static_cast<int>(size_y));
    // Mark the console as active so that display updates get
    // propagated to vc_surface.
    vc_dev->active = true;
    // Propagate the initial display contents to vc_surface.
    vc_full_repaint(vc_dev);
    vc_gfx_invalidate_all(&main_test_graphics, vc_dev);
  }

  ~TextconHelper() {
    delete[] textbuf;
    vc_free(vc_dev);
    vc_free_gfx(&main_test_graphics);
  }

  // Takes a snapshot of the vc_t's display.
  class DisplaySnapshot {
   public:
    DisplaySnapshot(TextconHelper* helper)
        : helper_(helper), snapshot_(new uint8_t[helper->vc_surface->len]) {
      memcpy(snapshot_.get(), helper->vc_surface->ptr, helper->vc_surface->len);
    }

    // Returns whether the vc_t's display changed since the
    // snapshot was taken.
    bool ChangedSinceSnapshot() {
      return memcmp(snapshot_.get(), helper_->vc_surface->ptr, helper_->vc_surface->len) != 0;
    }

    std::unique_ptr<char[]> ComparisonString() {
      vc_t* vc_dev = helper_->vc_dev;
      gfx_surface* vc_surface = helper_->vc_surface;
      // Add 1 to these sizes to account for the margins.
      uint32_t cmp_size_x = vc_dev->columns + 1;
      uint32_t cmp_size_y = vc_dev->rows + 1;
      uint32_t size_in_chars = cmp_size_x * cmp_size_y;

      std::unique_ptr<bool[]> diffs(new bool[size_in_chars]);
      for (uint32_t i = 0; i < size_in_chars; ++i)
        diffs[i] = false;

      for (uint32_t i = 0; i < vc_surface->len; ++i) {
        if (static_cast<uint8_t*>(vc_surface->ptr)[i] != snapshot_[i]) {
          uint32_t pixel_index = i / vc_surface->pixelsize;
          uint32_t x_pixels = pixel_index % vc_surface->stride;
          uint32_t y_pixels = pixel_index / vc_surface->stride;
          uint32_t x_chars = x_pixels / vc_dev->charw;
          uint32_t y_chars = y_pixels / vc_dev->charh;
          EXPECT_LT(x_chars, cmp_size_x);
          EXPECT_LT(y_chars, cmp_size_y);
          diffs[x_chars + y_chars * cmp_size_x] = true;
        }
      }

      // Build a string showing the differences.  If we had
      // std::string or equivalent, we'd use that here.
      size_t string_size = (cmp_size_x + 3) * cmp_size_y + 1;
      std::unique_ptr<char[]> string(new char[string_size]);
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
      EXPECT_EQ(ptr, string.get() + string_size);
      return string;
    }

    // Prints a representation of which characters in the vc_t's
    // display changed since the snapshot was taken.
    void PrintComparison() { printf("%s", ComparisonString().get()); }

   private:
    TextconHelper* helper_;
    std::unique_ptr<uint8_t[]> snapshot_;
  };

  void InvalidateAllGraphics() {
    vc_full_repaint(vc_dev);
    vc_gfx_invalidate_all(&main_test_graphics, vc_dev);
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

  void AssertTextbufLineContains(vc_char_t* buf, int line_num, const char* str) {
    size_t len = strlen(str);
    EXPECT_LE(len, size_x);
    for (size_t i = 0; i < len; ++i)
      EXPECT_EQ(str[i], vc_char_get_char(buf[size_x * line_num + i]));
    // The rest of the line should contain spaces.
    for (size_t i = len; i < size_x; ++i)
      EXPECT_EQ(' ', vc_char_get_char(buf[size_x * line_num + i]));
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

TEST(GfxConsoleTextbufTests, Simple) {
  TextconHelper tc(10, 5);
  tc.PutString("Hello");
  tc.AssertLineContains(0, "Hello");
  tc.AssertLineContains(1, "");
}

// This tests the DisplaySnapshot test helper above.  If we write directly
// to vc_dev's text buffer without invalidating the display, the test
// machinery should detect which characters in the display were not updated
// properly.
TEST(GfxConsoleTextbufTests, DisplayUpdateComparison) {
  TextconHelper tc(10, 3);
  // Write some characters directly into the text buffer.
  auto SetChar = [&](int x, int y, char ch) {
    tc.vc_dev->text_buf[x + y * tc.size_x] = vc_char_make(ch, tc.textcon.fg, tc.textcon.bg);
  };
  SetChar(2, 1, 'x');
  SetChar(3, 1, 'y');
  SetChar(6, 1, 'z');

  // Check that these characters in the display are detected as not
  // properly updated.
  TextconHelper::DisplaySnapshot snapshot(&tc);
  tc.InvalidateAllGraphics();
  EXPECT_TRUE(snapshot.ChangedSinceSnapshot());
  const char* expected =
      "|-----------|\n"  // Console status line
      "|-----------|\n"  // Cursor at left was painted during tc init
      "|--DD--D----|\n"  // Chars set by SetChar() above
      "|-----------|\n"
      "|-----------|\n";  // Bottom margin
  EXPECT_EQ(strcmp(snapshot.ComparisonString().get(), expected), 0);
}

// This tests updating the display with all of the different color schemes. This
// catches that the tc and the vcs set their color schemes correctly. If
// something goes wrong, either all of the chars will appear to be changed or
// none will be changed.
TEST(GfxConsoleTextbufTests, DisplayColorSchemes) {
  int colors[] = {kDarkColorScheme, kLightColorScheme, kSpecialColorScheme};
  for (size_t c = 0; c < countof(colors); c++) {
    TextconHelper tc(10, 3, &color_schemes[colors[c]]);
    // Write some characters directly into the text buffer.
    auto SetChar = [&](int x, int y, char ch) {
      tc.vc_dev->text_buf[x + y * tc.size_x] = vc_char_make(ch, tc.textcon.fg, tc.textcon.bg);
    };
    SetChar(2, 1, 'x');
    SetChar(3, 1, 'y');
    SetChar(6, 1, 'z');

    // Check that these characters in the display are detected as not
    // properly updated.
    TextconHelper::DisplaySnapshot snapshot(&tc);
    tc.InvalidateAllGraphics();
    EXPECT_TRUE(snapshot.ChangedSinceSnapshot());
    const char* expected =
        "|-----------|\n"  // Console status line
        "|-----------|\n"  // Cursor at left was painted during tc init
        "|--DD--D----|\n"  // Chars set by SetChar() above
        "|-----------|\n"
        "|-----------|\n";  // Bottom margin
    EXPECT_EQ(strcmp(snapshot.ComparisonString().get(), expected), 0);
  }
}

TEST(GfxConsoleTextbufTests, Wrapping) {
  TextconHelper tc(10, 5);
  tc.PutString("Hello world! More text here.");
  tc.AssertLineContains(0, "Hello worl");
  tc.AssertLineContains(1, "d! More te");
  tc.AssertLineContains(2, "xt here.");
}

TEST(GfxConsoleTextbufTests, Tabs) {
  TextconHelper tc(80, 40);
  tc.PutString("\tA\n");
  tc.PutString(" \tB\n");
  tc.PutString("       \tC\n");   // 7 spaces
  tc.PutString("        \tD\n");  // 8 spaces
  tc.AssertLineContains(0, "        A");
  tc.AssertLineContains(1, "        B");
  tc.AssertLineContains(2, "        C");
  tc.AssertLineContains(3, "                D");
}

TEST(GfxConsoleTextbufTests, BackspaceMovesCursor) {
  TextconHelper tc(10, 5);
  tc.PutString("ABCDEF\b\b\b\bxy");
  // Backspace only moves the cursor and does not erase, so "EF" is left
  // in place.
  tc.AssertLineContains(0, "ABxyEF");
}

TEST(GfxConsoleTextbufTests, BackspaceAtStartOfLine) {
  TextconHelper tc(10, 5);
  tc.PutString("Foo\n\bBar");
  // When the cursor is at the start of a line, backspace has no effect.
  tc.AssertLineContains(0, "Foo");
  tc.AssertLineContains(1, "Bar");
}

TEST(GfxConsoleTextbufTests, DeleteChars) {
  TextconHelper tc(20, 10);

  tc.PutString("123456");
  // Move the cursor to be over the "3".
  tc.PutString("\b\b\b\b");
  // Delete 2 characters to the right of the cursor, using the "DCH"
  // escape sequence.  Any characters beyond that, on the right, are
  // moved to the left by 2 characters.  This escape sequence is a
  // reduced test from a fuzzer-discovered example (see fxbug.dev/32773).  This
  // used to trigger an assertion failure.
  tc.PutString("\x1b[2P");
  tc.AssertLineContains(0, "1256");
}

TEST(GfxConsoleTextbufTests, DeleteCharsOverflow) {
  TextconHelper tc(6, 10);

  // Fill the width of the console, leaving the cursor on the next line.
  tc.PutString("123456");
  // Move the cursor to be over the "3", to position (y+1,x+1) = (1,3).
  tc.PutString("\x1b[1;3H");
  // Request deleting 5 characters to the right of the cursor, using
  // the "DCH" escape sequence.  This tests an overflow case,
  // because there are only 4 characters to the right of the cursor.
  tc.PutString("\x1b[5P");
  tc.AssertLineContains(0, "12");
}

TEST(GfxConsoleTextbufTests, ScrollUp) {
  TextconHelper tc(10, 4);
  tc.PutString("AAA\nBBB\nCCC\nDDD\n");
  tc.AssertLineContains(0, "BBB");
  tc.AssertLineContains(1, "CCC");
  tc.AssertLineContains(2, "DDD");
  tc.AssertLineContains(3, "");
  EXPECT_EQ(vc_get_scrollback_lines(tc.vc_dev), 1);
}

// Same as scroll_up(), but using ESC E (NEL) instead of "\n".
TEST(GfxConsoleTextbufTests, ScrollUpNel) {
  TextconHelper tc(10, 4);
  tc.PutString(
      "AAA"
      "\x1b"
      "E"
      "BBB"
      "\x1b"
      "E"
      "CCC"
      "\x1b"
      "E"
      "DDD"
      "\x1b"
      "E");
  tc.AssertLineContains(0, "BBB");
  tc.AssertLineContains(1, "CCC");
  tc.AssertLineContains(2, "DDD");
  tc.AssertLineContains(3, "");
  EXPECT_EQ(vc_get_scrollback_lines(tc.vc_dev), 1);
}

TEST(GfxConsoleTextbufTests, InsertLines) {
  TextconHelper tc(10, 5);
  tc.PutString("AAA\nBBB\nCCC\nDDD\nEEE");
  tc.PutString("\x1b[2A");  // Move the cursor up 2 lines
  tc.PutString("\x1b[2L");  // Insert 2 lines
  tc.PutString("Z");        // Output char to show where the cursor ends up
  tc.AssertLineContains(0, "AAA");
  tc.AssertLineContains(1, "BBB");
  tc.AssertLineContains(2, "   Z");
  tc.AssertLineContains(3, "");
  tc.AssertLineContains(4, "CCC");
  EXPECT_EQ(vc_get_scrollback_lines(tc.vc_dev), 0);
}

TEST(GfxConsoleTextbufTests, DeleteLines) {
  TextconHelper tc(10, 5);
  tc.PutString("AAA\nBBB\nCCC\nDDD\nEEE");
  tc.PutString("\x1b[2A");  // Move the cursor up 2 lines
  tc.PutString("\x1b[2M");  // Delete 2 lines
  tc.PutString("Z");        // Output char to show where the cursor ends up
  tc.AssertLineContains(0, "AAA");
  tc.AssertLineContains(1, "BBB");
  tc.AssertLineContains(2, "EEEZ");
  tc.AssertLineContains(3, "");
  tc.AssertLineContains(4, "");
  // TODO(mseaborn): We probably don't want to be adding the deleted
  // lines to the scrollback in this case, because they are not from the
  // top of the console.
  EXPECT_EQ(vc_get_scrollback_lines(tc.vc_dev), 2);
}

// Test for a bug where this would cause an out-of-bounds array access.
TEST(GfxConsoleTextbufTests, InsertLinesMany) {
  TextconHelper tc(10, 5);
  tc.PutString("AAA\nBBB");
  tc.PutString("\x1b[999L");  // Insert 999 lines
  tc.PutString("Z");          // Output char to show where the cursor ends up
  tc.AssertLineContains(0, "AAA");
  tc.AssertLineContains(1, "   Z");
}

// Test for a bug where this would cause an out-of-bounds array access.
TEST(GfxConsoleTextbufTests, DeleteLinesMany) {
  TextconHelper tc(10, 5);
  tc.PutString("AAA\nBBB");
  tc.PutString("\x1b[999M");  // Delete 999 lines
  tc.PutString("Z");          // Output char to show where the cursor ends up
  tc.AssertLineContains(0, "AAA");
  tc.AssertLineContains(1, "   Z");
}

// Check that passing a huge parameter via "insert lines" completes in a
// reasonable amount of time.  (We don't check the time here but we assume
// that someone will notice if this takes a long time.)
TEST(GfxConsoleTextbufTests, InsertLinesHuge) {
  TextconHelper tc(10, 5);
  tc.PutString("AAA\nBBB");
  tc.PutString("\x1b[2000000000L");  // Insert lines
  tc.PutString("Z");                 // Output char to show where the cursor ends up
  tc.AssertLineContains(0, "AAA");
  tc.AssertLineContains(1, "   Z");
}

// Check that passing a huge parameter via "delete lines" completes in a
// reasonable amount of time.  (We don't check the time here but we assume
// that someone will notice if this takes a long time.)
TEST(GfxConsoleTextbufTests, DeleteLinesHuge) {
  TextconHelper tc(10, 5);
  tc.PutString("AAA\nBBB");
  tc.PutString("\x1b[200000000M");  // Delete lines
  tc.PutString("Z");                // Output char to show where the cursor ends up
  tc.AssertLineContains(0, "AAA");
  tc.AssertLineContains(1, "   Z");
}

TEST(GfxConsoleTextbufTests, MoveCursorUpAndScroll) {
  TextconHelper tc(10, 4);
  tc.PutString("AAA\nBBB\nCCC\nDDD");
  tc.PutString(
      "\x1bM"
      "1");  // Move cursor up; print char
  tc.PutString(
      "\x1bM"
      "2");  // Move cursor up; print char
  tc.PutString(
      "\x1bM"
      "3");  // Move cursor up; print char
  tc.PutString(
      "\x1bM"
      "4");  // Move cursor up; print char
  tc.AssertLineContains(0, "      4");
  tc.AssertLineContains(1, "AAA  3");
  tc.AssertLineContains(2, "BBB 2");
  tc.AssertLineContains(3, "CCC1");
}

TEST(GfxConsoleTextbufTests, MoveCursorDownAndScroll) {
  TextconHelper tc(10, 4);
  tc.PutString(
      "1"
      "\x1b"
      "D");  // Print char; move cursor down
  tc.PutString(
      "2"
      "\x1b"
      "D");  // Print char; move cursor down
  tc.PutString(
      "3"
      "\x1b"
      "D");  // Print char; move cursor down
  tc.PutString(
      "4"
      "\x1b"
      "D");  // Print char; move cursor down
  tc.PutString("5");
  tc.AssertLineContains(0, " 2");
  tc.AssertLineContains(1, "  3");
  tc.AssertLineContains(2, "   4");
  tc.AssertLineContains(3, "    5");
}

TEST(GfxConsoleTextbufTests, CursorHideAndShow) {
  TextconHelper tc(10, 4);
  ASSERT_FALSE(tc.vc_dev->hide_cursor);
  tc.PutString("\x1b[?25l");  // Hide cursor
  ASSERT_TRUE(tc.vc_dev->hide_cursor);
  tc.PutString("\x1b[?25h");  // Show cursor
  ASSERT_FALSE(tc.vc_dev->hide_cursor);
}

// This tests for a bug: If the cursor was positioned over a character when
// we scroll up, that character would get erased.
TEST(GfxConsoleTextbufTests, CursorScrollBug) {
  TextconHelper tc(10, 3);
  // Move the cursor to the bottom line.
  tc.PutString("\n\n\n");
  // Scroll down when the cursor is over "C".
  tc.PutString("ABCDE\b\b\b\n");
}

// Test for a bug where scrolling the console viewport by a large delta
// (e.g. going from the top to the bottom) can crash due to out-of-bounds
// memory accesses.
TEST(GfxConsoleTextbufTests, ScrollViewportByLargeDelta) {
  TextconHelper tc(2, 2);
  tc.PutString("\n");
  for (int lines = 1; lines < 100; ++lines) {
    tc.PutString("\n");

    // Scroll up, to show older lines.
    vc_scroll_viewport_top(tc.vc_dev);
    EXPECT_EQ(tc.vc_dev->viewport_y, -lines);

    // Scroll down, to show newer lines.
    vc_scroll_viewport_bottom(tc.vc_dev);
    EXPECT_EQ(tc.vc_dev->viewport_y, 0);
  }
}

// When the console is displaying only the main console region (and no
// scrollback), the console should keep displaying that as new lines are
// outputted.
TEST(GfxConsoleTextbufTests, ViewportScrollingFollowsBottom) {
  TextconHelper tc(1, 1);
  for (unsigned i = 0; i < tc.vc_dev->scrollback_rows_max * 2; ++i) {
    EXPECT_EQ(tc.vc_dev->viewport_y, 0);
    tc.PutString("\n");
  }
}

// When the console is displaying some of the scrollback buffer, then as
// new lines are outputted, the console should scroll the viewpoint to keep
// displaying the same point, unless we're at the top of the scrollback
// buffer.
TEST(GfxConsoleTextbufTests, ViewportScrollingFollowsScrollback) {
  TextconHelper tc(1, 1);
  // Add 3 lines to the scrollback buffer.
  tc.PutString("\n\n\n");
  vc_scroll_viewport(tc.vc_dev, -2);

  EXPECT_EQ(tc.vc_dev->viewport_y, -2);
  int limit = tc.vc_dev->scrollback_rows_max;
  for (int line = 3; line < limit * 2; ++line) {
    // Output different strings on each line in order to test that the
    // display is updated consistently when the console starts dropping
    // lines from the scrollback region.
    char str[3] = {static_cast<char>('0' + (line % 10)), '\n', '\0'};
    tc.PutString(str);
    EXPECT_EQ(tc.vc_dev->viewport_y, -MIN(line, limit));
  }
}

TEST(GfxConsoleTextbufTests, OutputWhenViewportScrolled) {
  TextconHelper tc(10, 3);
  // Line 1 will move into the scrollback region.
  tc.PutString("1\n 2\n  3\n   4");
  EXPECT_EQ(tc.vc_dev->viewport_y, 0);
  vc_scroll_viewport_top(tc.vc_dev);

  EXPECT_EQ(tc.vc_dev->viewport_y, -1);
  // Check redrawing consistency.
  tc.PutString("");

  // Test that output updates the display correctly when the viewport is
  // scrolled.  Using two separate PutString() calls here was necessary
  // for reproducing an incremental update bug.
  tc.PutString("\x1b[1;1f");  // Move to top left
  tc.PutString("Epilobium");
  tc.AssertLineContains(0, "Epilobium");
  tc.AssertLineContains(1, "  3");
  tc.AssertLineContains(2, "   4");

  // Test that erasing also updates the display correctly.  This
  // changes the console contents without moving the cursor.
  tc.PutString("\b\b\b\b");  // Move cursor left 3 chars
  tc.PutString("\x1b[1K");   // Erase to beginning of line
  tc.AssertLineContains(0, "      ium");
  tc.AssertLineContains(1, "  3");
  tc.AssertLineContains(2, "   4");
}

TEST(GfxConsoleTextbufTests, ScrollingWhenViewportScrolled) {
  TextconHelper tc(10, 3);
  // Line 1 will move into the scrollback region.
  tc.PutString("1\n 2\n  3\n   4");
  EXPECT_EQ(tc.vc_dev->viewport_y, 0);
  vc_scroll_viewport_top(tc.vc_dev);
  EXPECT_EQ(tc.vc_dev->viewport_y, -1);
  // Check redrawing consistency.
  tc.PutString("");

  // Test that the display is updated correctly when we scroll.
  tc.PutString("\n5");
  tc.AssertLineContains(0, "  3");
  tc.AssertLineContains(1, "   4");
  tc.AssertLineContains(2, "5");
}

// Test that vc_get_scrollback_lines() gives the correct results.
TEST(GfxConsoleTextbufTests, ScrollbackLinesCount) {
  TextconHelper tc(10, 3);
  tc.PutString("\n\n");

  // Reduce the scrollback limit to make the test faster.
  const int kLimit = 20;
  EXPECT_LE(kLimit, tc.vc_dev->scrollback_rows_max);
  tc.vc_dev->scrollback_rows_max = kLimit;

  for (int lines = 1; lines < kLimit * 4; ++lines) {
    tc.PutString("\n");
    EXPECT_EQ(MIN(lines, kLimit), vc_get_scrollback_lines(tc.vc_dev));
  }
}

// Test that the scrollback lines have the correct contents.
TEST(GfxConsoleTextbufTests, ScrollbackLinesContents) {
  // Use a 1-row-high console, which simplifies this test.
  TextconHelper tc(3, 1);

  // Reduce the scrollback limit to make the test faster.
  const int kLimit = 20;
  EXPECT_LE(kLimit, tc.vc_dev->scrollback_rows_max);
  tc.vc_dev->scrollback_rows_max = kLimit;

  vc_char_t test_val = 0;
  for (int lines = 1; lines <= kLimit; ++lines) {
    tc.vc_dev->text_buf[0] = test_val++;
    tc.PutString("\n");

    EXPECT_EQ(lines, vc_get_scrollback_lines(tc.vc_dev));
    for (int i = 0; i < lines; ++i)
      EXPECT_EQ(i, vc_get_scrollback_line_ptr(tc.vc_dev, i)[0]);
  }
  for (int lines = 0; lines < kLimit * 3; ++lines) {
    tc.vc_dev->text_buf[0] = test_val++;
    tc.PutString("\n");

    EXPECT_EQ(kLimit, vc_get_scrollback_lines(tc.vc_dev));
    for (int i = 0; i < kLimit; ++i) {
      EXPECT_EQ(test_val + i - kLimit, vc_get_scrollback_line_ptr(tc.vc_dev, i)[0]);
    }
  }
}

}  // namespace
