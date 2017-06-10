// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "textcon.h"

#include <assert.h>
#include <string.h>

static inline void invalidate(textcon_t* tc, int x, int y, int w, int h) {
    tc->invalidate(tc->cookie, x, y, w, h);
}
static inline void movecursor(textcon_t* tc, int x, int y) {
    tc->movecursor(tc->cookie, x, y);
}
static inline void push_scrollback_line(textcon_t* tc, int y) {
    tc->push_scrollback_line(tc->cookie, y);
}
static inline void setparam(textcon_t* tc, int param, uint8_t* arg,
                            size_t arglen) {
    tc->setparam(tc->cookie, param, arg, arglen);
}

// Construct a vc_char_t from the given character using the current colors.
static inline vc_char_t make_vc_char(textcon_t* tc, uint8_t ch) {
    return (vc_char_t)(ch |
                       (((vc_char_t)tc->fg & 15) << 8) |
                       (((vc_char_t)tc->bg & 15) << 12));
}

static vc_char_t* dataxy(textcon_t* tc, int x, int y) {
    assert(x >= 0);
    assert(x < tc->w);
    assert(y >= 0);
    assert(y < tc->h);
    return tc->data + y * tc->w + x;
}

static vc_char_t* get_start_of_line(textcon_t* tc, int y) {
    assert(y >= 0);
    assert(y <= tc->h);
    return tc->data + y * tc->w;
}

static int clampx(textcon_t* tc, int x) {
    return x < 0 ? 0 : x >= tc->w ? tc->w - 1 : x;
}

static int clampxatedge(textcon_t* tc, int x) {
    return x < 0 ? 0 : x > tc->w ? tc->w : x;
}

static int clampy(textcon_t* tc, int y) {
    return y < 0 ? 0 : y >= tc->h ? tc->h - 1 : y;
}

static void moveto(textcon_t* tc, int x, int y) {
    tc->x = clampx(tc, x);
    tc->y = clampy(tc, y);
}

static inline void moverel(textcon_t* tc, int dx, int dy) {
    moveto(tc, tc->x + dx, tc->y + dy);
}

static void fill(vc_char_t* ptr, vc_char_t val, size_t count) {
    while (count-- > 0) {
        *ptr++ = val;
    }
}

static void erase_region(textcon_t* tc, int x0, int y0, int x1, int y1) {
    if (x0 >= tc->w) {
        return;
    }
    x1 = clampx(tc, x1);
    vc_char_t* ptr = dataxy(tc, x0, y0);
    vc_char_t* end = dataxy(tc, x1, y1) + 1;
    fill(ptr, make_vc_char(tc, ' '), end - ptr);
    invalidate(tc, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

static void erase_screen(textcon_t* tc, int arg) {
    switch (arg) {
    case 0: // erase downward
        erase_region(tc, tc->x, tc->y, tc->w - 1, tc->h - 1);
        break;
    case 1: // erase upward
        erase_region(tc, 0, 0, tc->x, tc->y);
        break;
    case 2: // erase all
        erase_region(tc, 0, 0, tc->w - 1, tc->h - 1);
        break;
    }
}

static void erase_line(textcon_t* tc, int arg) {
    switch (arg) {
    case 0: // erase to eol
        erase_region(tc, tc->x, tc->y, tc->w - 1, tc->y);
        break;
    case 1: // erase from bol
        erase_region(tc, 0, tc->y, tc->x, tc->y);
        break;
    case 2: // erase line
        erase_region(tc, 0, tc->y, tc->w - 1, tc->y);
        break;
    }
}

static void erase_chars(textcon_t* tc, int arg) {
    if (tc->x >= tc->w) {
        return;
    }
    if (arg < 0) {
        arg = 0;
    }
    if (arg > tc->w) {
        arg = tc->w;
    }

    vc_char_t* dst = dataxy(tc, tc->x, tc->y);
    vc_char_t* src = dataxy(tc, tc->x + arg, tc->y);
    vc_char_t* end = dataxy(tc, tc->x + tc->w, tc->y);

    while (src < end) {
        *dst++ = *src++;
    }
    while (dst < end) {
        *dst++ = make_vc_char(tc, ' ');
    }

    invalidate(tc, tc->x, tc->y, tc->w - tc->x, 1);
}

void tc_copy_lines(textcon_t* tc, int y_dest, int y_src, int line_count) {
    vc_char_t* dest = get_start_of_line(tc, y_dest);
    vc_char_t* src = get_start_of_line(tc, y_src);
    memmove(dest, src, line_count * tc->w * sizeof(vc_char_t));
}

static void clear_lines(textcon_t* tc, int y, int line_count) {
    fill(get_start_of_line(tc, y), make_vc_char(tc, ' '), line_count * tc->w);
    invalidate(tc, 0, y, tc->w, line_count);
}

// Scroll the region between line |y0| (inclusive) and |y1| (exclusive).
// Scroll by |diff| lines, which may be positive (for moving lines up) or
// negative (for moving lines down).
static void scroll_lines(textcon_t* tc, int y0, int y1, int diff) {
    int delta = diff > 0 ? diff : -diff;
    if (delta > y1 - y0)
        delta = y1 - y0;
    int copy_count = y1 - y0 - delta;
    if (diff > 0) {
        // Scroll up.
        for (int i = 0; i < delta; ++i) {
            push_scrollback_line(tc, y0 + i);
        }
        tc->copy_lines(tc->cookie, y0, y0 + delta, copy_count);
        clear_lines(tc, y0 + copy_count, delta);
    } else {
        // Scroll down.
        tc->copy_lines(tc->cookie, y0 + delta, y0, copy_count);
        clear_lines(tc, y0, delta);
    }
}

static void scroll_up(textcon_t* tc) {
    scroll_lines(tc, tc->scroll_y0, tc->scroll_y1, 1);
}

// positive = up, negative = down
static void scroll_at_pos(textcon_t* tc, int dir) {
    if (tc->y < tc->scroll_y0)
        return;
    if (tc->y >= tc->scroll_y1)
        return;

    scroll_lines(tc, tc->y, tc->scroll_y1, dir);
}

void set_scroll(textcon_t* tc, int y0, int y1) {
    if (y0 > y1) {
        return;
    }
    tc->scroll_y0 = (y0 < 0) ? 0 : y0;
    tc->scroll_y1 = (y1 > tc->h) ? tc->h : y1;
}

static void savecursorpos(textcon_t* tc) {
    tc->save_x = tc->x;
    tc->save_y = tc->y;
}

static void restorecursorpos(textcon_t* tc) {
    tc->x = clampxatedge(tc, tc->save_x);
    tc->y = clampy(tc, tc->save_y);
}

static void putc_plain(textcon_t* tc, uint8_t c);
static void putc_escape2(textcon_t* tc, uint8_t c);

static void putc_ignore(textcon_t* tc, uint8_t c) {
    tc->putc = putc_plain;
}

static void putc_param(textcon_t* tc, uint8_t c) {
    switch (c) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        tc->num = tc->num * 10 + (c - '0');
        return;
    case ';':
        if (tc->argn_count < TC_MAX_ARG) {
            tc->argn[tc->argn_count++] = tc->num;
        }
        tc->putc = putc_escape2;
        break;
    default:
        if (tc->argn_count < TC_MAX_ARG) {
            tc->argn[tc->argn_count++] = tc->num;
        }
        tc->putc = putc_escape2;
        putc_escape2(tc, c);
        break;
    }
}

#define ARG0(def) ((tc->argn_count > 0) ? tc->argn[0] : (def))
#define ARG1(def) ((tc->argn_count > 1) ? tc->argn[1] : (def))

static void putc_dec(textcon_t* tc, uint8_t c) {
    switch (c) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        tc->num = tc->num * 10 + (c - '0');
        return;
    case 'h':
        if (tc->num == 25)
            setparam(tc, TC_SHOW_CURSOR, NULL, 0);
        break;
    case 'l':
        if (tc->num == 25)
            setparam(tc, TC_HIDE_CURSOR, NULL, 0);
        break;
    default:
        putc_plain(tc, c);
        break;
    }
    tc->putc = putc_plain;
}

static textcon_param_t osc_to_param(int osc) {
    switch (osc) {
    case 2:
        return TC_SET_TITLE;
    default:
        return TC_INVALID;
    }
}

static void putc_osc2(textcon_t* tc, uint8_t c) {
    switch (c) {
    case 7: { // end command
        textcon_param_t param = osc_to_param(ARG0(-1));
        if (param != TC_INVALID && tc->argstr_size)
            setparam(tc, param, tc->argstr, tc->argstr_size);
        tc->putc = putc_plain;
        break;
    }
    default:
        if (tc->argstr_size < TC_MAX_ARG_LENGTH)
            tc->argstr[tc->argstr_size++] = c;
        break;
    }
}

static void putc_osc(textcon_t* tc, uint8_t c) {
    switch (c) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        tc->num = tc->num * 10 + (c - '0');
        return;
    case ';':
        if (tc->argn_count < TC_MAX_ARG) {
            tc->argn[tc->argn_count++] = tc->num;
        }
        memset(tc->argstr, 0, TC_MAX_ARG_LENGTH);
        tc->argstr_size = 0;
        tc->putc = putc_osc2;
        break;
    default:
        if (tc->argn_count < TC_MAX_ARG) {
            tc->argn[tc->argn_count++] = tc->num;
        }
        tc->putc = putc_osc2;
        putc_osc2(tc, c);
        break;
    }
}

static void putc_escape2(textcon_t* tc, uint8_t c) {
    int x, y;
    switch (c) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        tc->num = c - '0';
        tc->putc = putc_param;
        return;
    case ';': // end parameter
        if (tc->argn_count < TC_MAX_ARG) {
            tc->argn[tc->argn_count++] = 0;
        }
        return;
    case '?':
        tc->num = 0;
        tc->argn_count = 0;
        tc->putc = putc_dec;
        return;
    case 'A': // (CUU) Cursor Up
        moverel(tc, 0, -ARG0(1));
        break;
    case 'B': // (CUD) Cursor Down
        moverel(tc, 0, ARG0(1));
        break;
    case 'C': // (CUF) Cursor Forward
        moverel(tc, ARG0(1), 0);
        break;
    case 'D': // (CUB) Cursor Backward
        moverel(tc, -ARG0(1), 0);
        break;
    case 'E':
        moveto(tc, 0, tc->y + ARG0(1));
        break;
    case 'F':
        moveto(tc, 0, tc->y - ARG0(1));
        break;
    case 'G': // move xpos absolute
        x = ARG0(1);
        moveto(tc, x ? (x - 1) : 0, tc->y);
        break;
    case 'H': // (CUP) Cursor Position
    case 'f': // (HVP) Horizontal and Vertical Position
        x = ARG1(1);
        y = ARG0(1);
        moveto(tc, x ? (x - 1) : 0, y ? (y - 1) : 0);
        break;
    case 'J': // (ED) erase in display
        erase_screen(tc, ARG0(0));
        break;
    case 'K': // (EL) erase in line
        erase_line(tc, ARG0(0));
        break;
    case 'L': // (IL) insert line(s) at cursor
        scroll_at_pos(tc, -ARG0(1));
        break;
    case 'M': // (DL) delete line(s) at cursor
        scroll_at_pos(tc, ARG0(1));
        break;
    case 'P': // (DCH) delete character(s)
        erase_chars(tc, ARG0(1));
        break;
    case 'd': // move ypos absolute
        y = ARG0(1);
        moveto(tc, tc->x, y ? (y - 1) : 0);
        break;
    case 'm': // (SGR) Character Attributes
        for (int i = 0; i < tc->argn_count; i++) {
            int n = tc->argn[i];
            if ((n >= 30) && (n <= 37)) { // set fg
                tc->fg = (uint8_t)(n - 30);
            } else if ((n >= 40) && (n <= 47)) { // set bg
                tc->bg = (uint8_t)(n - 40);
            } else if ((n == 1) && (tc->fg <= 7)) { // bold
                tc->fg = (uint8_t)(tc->fg + 8);
            } else if (n == 0) { // reset
                tc->fg = 0;
                tc->bg = 15;
            } else if (n == 7) { // reverse
                uint8_t temp = tc->fg;
                tc->fg = tc->bg;
                tc->bg = temp;
            } else if (n == 39) { // default fg
                tc->fg = 0;
            } else if (n == 49) { // default bg
                tc->bg = 15;
            }
        }
        break;
    case 'r': // set scroll region
        set_scroll(tc, ARG0(1) - 1, ARG1(tc->h));
        break;
    case 's': // save cursor position ??
        savecursorpos(tc);
        break;
    case 'u': // restore cursor position ??
        restorecursorpos(tc);
        break;
    case '@': // (ICH) Insert Blank Character(s)
    case 'T': // Initiate Hilight Mouse Tracking (xterm)
    case 'c': // (DA) Send Device Attributes
    case 'g': // (TBC) Tab Clear
    case 'h': // (SM) Set Mode  (4=Insert,20=AutoNewline)
    case 'l': // (RM) Reset Mode (4=Replace,20=NormalLinefeed)
    case 'n': // (DSR) Device Status Report
    case 'x': // Request Terminal Parameters
    default:
        break;
    }
    movecursor(tc, tc->x, tc->y);
    tc->putc = putc_plain;
}

static void putc_escape(textcon_t* tc, uint8_t c) {
    switch (c) {
    case 27: // escape
        return;
    case '(':
    case ')':
    case '*':
    case '+':
        // select various character sets
        tc->putc = putc_ignore;
        return;
    case '[':
        tc->num = 0;
        tc->argn_count = 0;
        tc->putc = putc_escape2;
        return;
    case ']':
        tc->num = 0;
        tc->argn_count = 0;
        tc->putc = putc_osc;
        return;
    case '7': // (DECSC) Save Cursor
        savecursorpos(tc);
        // save attribute
        break;
    case '8': // (DECRC) Restore Cursor
        restorecursorpos(tc);
        movecursor(tc, tc->x, tc->y);
        break;
    case 'E': // (NEL) Next Line
        tc->x = 0;
        // Fall through
    case 'D': // (IND) Index
        tc->y++;
        if (tc->y >= tc->scroll_y1) {
            tc->y--;
            scroll_up(tc);
        }
        movecursor(tc, tc->x, tc->y);
        break;
    case 'M': // (RI) Reverse Index)
        tc->y--;
        if (tc->y < tc->scroll_y0) {
            tc->y++;
            scroll_at_pos(tc, -1);
        }
        movecursor(tc, tc->x, tc->y);
        break;
    }
    tc->putc = putc_plain;
}

static void putc_cr(textcon_t* tc) {
    tc->x = 0;
}

static void putc_lf(textcon_t* tc) {
    tc->y++;
    if (tc->y >= tc->scroll_y1) {
        tc->y--;
        scroll_up(tc);
    }
}

static void putc_plain(textcon_t* tc, uint8_t c) {
    switch (c) {
    case 7: // bell
        break;
    case 8: // backspace / ^H
        if (tc->x > 0)
            tc->x--;
        break;
    case 9: // tab / ^I
        moveto(tc, (tc->x + 8) & (~7), tc->y);
        break;
    case 10:         // newline
        putc_cr(tc); // should we imply this?
        putc_lf(tc);
        break;
    case 12:
        erase_screen(tc, 2);
        break;
    case 13: // carriage return
        putc_cr(tc);
        break;
    case 27: // escape
        tc->putc = putc_escape;
        return;
    default:
        if ((c < ' ') || (c > 127)) {
            return;
        }
        if (tc->x >= tc->w) {
            // apply deferred line wrap upon printing first character beyond
            // end of current line
            putc_cr(tc);
            putc_lf(tc);
        }
        dataxy(tc, tc->x, tc->y)[0] = make_vc_char(tc, c);
        invalidate(tc, tc->x, tc->y, 1, 1);
        tc->x++;
        break;
    }
    movecursor(tc, tc->x, tc->y);
}

void tc_init(textcon_t* tc, int w, int h, vc_char_t* data,
             uint8_t fg, uint8_t bg) {
    tc->w = w;
    tc->h = h;
    tc->x = 0;
    tc->y = 0;
    tc->data = data;
    tc->scroll_y0 = 0;
    tc->scroll_y1 = h;
    tc->save_x = 0;
    tc->save_y = 0;
    tc->fg = fg;
    tc->bg = bg;
    tc->putc = putc_plain;
}

void tc_seth(textcon_t* tc, int h) {
    // tc->data must be big enough for the new height
    int old_h = h;
    tc->h = h;

    // move contents
    int y = 0;
    if (old_h > h) {
        vc_char_t* dst = dataxy(tc, 0, tc->scroll_y0);
        vc_char_t* src = dataxy(tc, 0, tc->scroll_y0 + old_h - h);
        vc_char_t* end = dataxy(tc, 0, tc->scroll_y1);
        do {
            push_scrollback_line(tc, y);
        } while (++y < old_h - h);
        memmove(dst, src, (end - dst) * sizeof(vc_char_t));
        tc->y -= old_h - h;
    } else if (old_h < h) {
        do {
            fill(dataxy(tc, 0, tc->scroll_y1 + y), make_vc_char(tc, ' '), tc->w);
        } while (++y < h - old_h);
    }
    tc->y = clampy(tc, tc->y);

    // try to fixup the scroll region
    if (tc->scroll_y0 >= h) {
        tc->scroll_y0 = 0;
    }
    if (tc->scroll_y1 == old_h) {
        tc->scroll_y1 = h;
    } else {
        tc->scroll_y1 = tc->scroll_y1 >= h ? h : tc->scroll_y1;
    }

    invalidate(tc, 0, 0, tc->w, tc->h);
    movecursor(tc, tc->x, tc->y);
}
