// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_VIRTCON_VC_H_
#define SRC_BRINGUP_BIN_VIRTCON_VC_H_

#include <assert.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/vfs.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdbool.h>
#include <threads.h>

#include <gfx/gfx.h>
#include <hid/hid.h>

#include "src/lib/listnode/listnode.h"
#include "textcon.h"
#include "vc-colors.h"
#include "vc-gfx.h"

#define MAX_COLOR 0xf

typedef void (*keypress_handler_t)(uint8_t keycode, int modifiers);

zx_status_t new_input_device(int fd, keypress_handler_t handler);

// constraints on status bar tabs
#define MIN_TAB_WIDTH 16
#define MAX_TAB_WIDTH 32

#define STATUS_COLOR_BG 0
#define STATUS_COLOR_DEFAULT 7
#define STATUS_COLOR_ACTIVE 11
#define STATUS_COLOR_UPDATED 10

typedef struct vc {
  char title[MAX_TAB_WIDTH];
  // vc title, shown in status bar
  bool active;
  unsigned flags;

  zx_handle_t gfx_vmo;

  int fd;

  // backing store
  const gfx_font* font;

  vc_char_t* text_buf;
  // text buffer

  // Buffer containing scrollback lines.  This is a circular buffer.
  vc_char_t* scrollback_buf;
  // Maximum number of rows that may be stored in the scrollback buffer.
  unsigned scrollback_rows_max;
  // Number of rows currently stored in the scrollback buffer.
  unsigned scrollback_rows_count;
  // Offset, in rows, of the oldest row in the scrollback buffer.
  unsigned scrollback_offset;

  unsigned rows, columns;
  // screen size
  unsigned charw, charh;
  // size of character cell

  int invy0, invy1;
  // offscreen invalid lines, tracked during textcon drawing

  unsigned cursor_x, cursor_y;
  // cursor
  bool hide_cursor;
  // cursor visibility
  int viewport_y;
  // viewport position, must be <= 0

  uint32_t palette[16];
  uint8_t front_color;
  uint8_t back_color;
  // color

  textcon_t textcon;

  const keychar_t* keymap;

  struct list_node node;
  // for virtual console list

  vc_gfx_t* graphics;

#if !BUILD_FOR_TEST
  // This has to be a pointer since vc has to have a standard layout.
  std::unique_ptr<async::Wait> pty_wait;
  fdio_t* io;
  zx_handle_t proc;
  bool is_shell;
#endif
} vc_t;

// When VC_FLAG_HASOUTPUT is set, this indicates that there was output to
// the console that hasn't been displayed yet, because this console isn't
// visible.
#define VC_FLAG_HASOUTPUT (1 << 0)
#define VC_FLAG_FULLSCREEN (1 << 1)

const gfx_font* vc_get_font();

zx_status_t vc_alloc(vc_t** out, const color_scheme_t* color_scheme);
void vc_attach_gfx(vc_t* vc);
void vc_free(vc_t* vc);
void vc_flush(vc_t* vc);
void vc_flush_all(vc_t* vc);

// called to re-draw the status bar after
// status-worthy vc or global state has changed
void vc_status_update();

// used by vc_status_invalidate to draw the status bar
void vc_status_clear();
void vc_status_write(int x, unsigned color, const char* text);
void vc_status_commit();

void vc_render(vc_t* vc);
void vc_full_repaint(vc_t* vc);
int vc_get_scrollback_lines(vc_t* vc);
vc_char_t* vc_get_scrollback_line_ptr(vc_t* vc, unsigned row);
void vc_scroll_viewport(vc_t* vc, int dir);
void vc_scroll_viewport_top(vc_t* vc);
void vc_scroll_viewport_bottom(vc_t* vc);
void vc_set_fullscreen(vc_t* vc, bool fullscreen);

ssize_t vc_write(vc_t* vc, const void* buf, size_t count, zx_off_t off);

static inline int vc_rows(vc_t* vc) {
  return vc->flags & VC_FLAG_FULLSCREEN ? vc->rows : vc->rows - 1;
}

static inline uint32_t palette_to_color(vc_t* vc, uint8_t color) {
  assert(color <= MAX_COLOR);
  return vc->palette[color];
}

extern bool g_vc_owns_display;
extern vc_t* g_active_vc;
extern int g_status_width;
extern vc_t* g_log_vc;

void handle_key_press(uint8_t keycode, int modifiers);
void vc_toggle_framebuffer();

zx_status_t vc_create(vc_t** out, const color_scheme_t* color_scheme);
void vc_destroy(vc_t* vc);
ssize_t vc_write(vc_t* vc, const void* buf, size_t count, zx_off_t off);
zx_status_t vc_set_active(int num, vc_t* vc);
void vc_show_active();
void vc_change_graphics(vc_gfx_t* graphics);

void set_log_listener_active(bool active);
int log_start(async_dispatcher_t* dispatcher);
zx_status_t log_create_vc(vc_gfx_t* graphics, vc_t** vc_out);
void log_delete_vc(vc_t* vc);

bool vc_display_init(async_dispatcher_t* dispatcher);
bool vc_sysmem_connect(void);
void vc_attach_to_main_display(vc_t* vc);
bool is_primary_bound();

void set_log_listener_active(bool active);

#endif  // SRC_BRINGUP_BIN_VIRTCON_VC_H_
