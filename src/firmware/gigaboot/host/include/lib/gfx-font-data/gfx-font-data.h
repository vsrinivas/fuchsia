// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_HOST_INCLUDE_LIB_GFX_FONT_DATA_GFX_FONT_DATA_H_
#define SRC_FIRMWARE_GIGABOOT_HOST_INCLUDE_LIB_GFX_FONT_DATA_GFX_FONT_DATA_H_

// Fake header, currently //zircon/system/ulib/gfx cannot build for the host
// as it uses Zircon syscalls and APIs, so we copy the necessary defs here.

#include <stdint.h>

typedef struct gfx_font {
  uint32_t width, height;
  uint16_t data[];
} gfx_font;

#endif  // SRC_FIRMWARE_GIGABOOT_HOST_INCLUDE_LIB_GFX_FONT_DATA_GFX_FONT_DATA_H_
