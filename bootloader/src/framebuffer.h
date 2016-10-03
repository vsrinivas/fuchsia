// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi/system-table.h>
#include <efi/protocol/graphics-output.h>

// Sets the graphics mode based on a string of the form "WxH" where W and H are
// integers representing width and height of the mode. This is usually obtained
// from the bootloader.fbres commandline argument.
void set_graphics_mode(efi_system_table* sys, efi_graphics_output_protocol* gop, char* fbres);

// Clears the screen and draws the Fuchsia logo.
void draw_logo(efi_graphics_output_protocol* gop);
