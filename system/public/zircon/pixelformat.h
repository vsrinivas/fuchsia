// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

typedef uint32_t zx_pixel_format_t;
// clang-format off

#define ZX_PIXEL_FORMAT_NONE       (0x00000000)

#define ZX_PIXEL_FORMAT_RGB_565    (0x00020001)
#define ZX_PIXEL_FORMAT_RGB_332    (0x00010002)
#define ZX_PIXEL_FORMAT_RGB_2220   (0x00010003)
#define ZX_PIXEL_FORMAT_ARGB_8888  (0x00040004)
#define ZX_PIXEL_FORMAT_RGB_x888   (0x00040005)
#define ZX_PIXEL_FORMAT_MONO_8     (0x00010007)
#define ZX_PIXEL_FORMAT_GRAY_8     (0x00010007)

// to be removed:
#define ZX_PIXEL_FORMAT_MONO_1     (0x00000006)


#define ZX_PIXEL_FORMAT_BYTES(pf)  (((pf) >> 16) & 7)