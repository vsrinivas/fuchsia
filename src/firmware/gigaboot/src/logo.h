// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_LOGO_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_LOGO_H_

#include <zircon/compiler.h>

#include <efi/protocol/graphics-output.h>

__BEGIN_CDECLS

static const int logo_width = 512;
static const int logo_height = 512;

// Loads the logo into |pixels|, which must be an array of size
// |logo_width| x |logo_height|.
void logo_load(efi_graphics_output_blt_pixel* pixels);

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_LOGO_H_
