// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PIXELFORMAT_H_
#define ZIRCON_PIXELFORMAT_H_

#include <stdint.h>

typedef uint32_t zx_pixel_format_t;
// clang-format off

#define ZX_PIXEL_FORMAT_NONE       ((zx_pixel_format_t)0x00000000)

#define ZX_PIXEL_FORMAT_RGB_565    ((zx_pixel_format_t)0x00020001)
#define ZX_PIXEL_FORMAT_RGB_332    ((zx_pixel_format_t)0x00010002)
#define ZX_PIXEL_FORMAT_RGB_2220   ((zx_pixel_format_t)0x00010003)
#define ZX_PIXEL_FORMAT_ARGB_8888  ((zx_pixel_format_t)0x00040004)
#define ZX_PIXEL_FORMAT_RGB_x888   ((zx_pixel_format_t)0x00040005)
#define ZX_PIXEL_FORMAT_MONO_8     ((zx_pixel_format_t)0x00010007)
#define ZX_PIXEL_FORMAT_GRAY_8     ((zx_pixel_format_t)0x00010007)

// to be removed:
#define ZX_PIXEL_FORMAT_MONO_1     ((zx_pixel_format_t)0x00000006)

#define ZX_PIXEL_FORMAT_BYTES(pf)  (((pf) >> 16) & 7)

#endif // ZIRCON_PIXELFORMAT_H_
