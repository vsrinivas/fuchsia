// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_SRC_FRAMEBUFFER_H_
#define ZIRCON_BOOTLOADER_SRC_FRAMEBUFFER_H_

#include <lib/gfx-font-data/gfx-font-data.h>

#include <efi/protocol/graphics-output.h>
#include <efi/system-table.h>

// Gets the current framebuffer graphics mode.
uint32_t get_gfx_mode(void);

// Gets the maximum framebuffer graphics mode index.
uint32_t get_gfx_max_mode(void);

// Returns the horizontal or vertical resolution of the current mode.
uint32_t get_gfx_hres(void);
uint32_t get_gfx_vres(void);

// Sets the framebuffer graphics mode.
void set_gfx_mode(uint32_t mode);

// Sets the graphics mode based on a string of the form "WxH" where W and H are
// integers representing width and height of the mode. This is usually obtained
// from the bootloader.fbres commandline argument.
void set_gfx_mode_from_cmdline(const char* fbres);

// Print all the supported framebuffer modes to the system console.
void print_fb_modes(void);

// Clears the screen and draws the Fuchsia logo.
void draw_logo(void);

typedef struct fb_font {
  const gfx_font* font;
  efi_graphics_output_blt_pixel* color;
} fb_font;

// Draws provided text at coordinate x and y of the framebuffer.
void draw_text(const char* text, size_t length, const fb_font* font, int x, int y);
void draw_version(const char*);

// Draws nodename in appropriate location based on mode.
void draw_nodename(const char* text);

#endif  // ZIRCON_BOOTLOADER_SRC_FRAMEBUFFER_H_
