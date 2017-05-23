// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <efi/protocol/graphics-output.h>
#include <efi/system-table.h>

// Gets the current framebuffer graphics mode.
uint32_t get_gfx_mode();

// Gets the maximum framebuffer graphics mode index.
uint32_t get_gfx_max_mode();

// Returns the horizontal or vertical resolution of the current mode.
uint32_t get_gfx_hres();
uint32_t get_gfx_vres();

// Sets the framebuffer graphics mode.
void set_gfx_mode(uint32_t mode);

// Sets the graphics mode based on a string of the form "WxH" where W and H are
// integers representing width and height of the mode. This is usually obtained
// from the bootloader.fbres commandline argument.
void set_gfx_mode_from_cmdline(const char* fbres);

// Print all the supported framebuffer modes to the system console.
void print_fb_modes();

// Clears the screen and draws the Fuchsia logo.
void draw_logo();

typedef struct font_t {
    const uint16_t* data;
    unsigned width;
    unsigned height;
    efi_graphics_output_blt_pixel* color;
} fb_font;

// Draws provided text at coordinate x and y of the framebuffer.
void draw_text(const char* text, size_t length, fb_font* font, int x, int y);
void draw_version(const char*);

// Draws nodename in appropriate location based on mode.
void draw_nodename(const char* text);
