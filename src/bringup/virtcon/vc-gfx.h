// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_VIRTCON_VC_GFX_H_
#define SRC_BRINGUP_VIRTCON_VC_GFX_H_

#include <zircon/types.h>

#include <gfx/gfx.h>

typedef struct vc vc_t;

typedef struct vc_gfx {
  gfx_surface* vc_gfx = nullptr;
  gfx_surface* vc_status_bar_gfx = nullptr;
  const gfx_font* vc_font = nullptr;

#if BUILD_FOR_TEST
  gfx_surface* vc_test_gfx = nullptr;
#else
  zx_handle_t vc_gfx_vmo = ZX_HANDLE_INVALID;
  uintptr_t vc_gfx_mem = 0;
  size_t vc_gfx_size = 0;

  zx_handle_t vc_hw_gfx_vmo = ZX_HANDLE_INVALID;
  gfx_surface* vc_hw_gfx = 0;
  uintptr_t vc_hw_gfx_mem = 0;
#endif
} vc_gfx_t;

#if BUILD_FOR_TEST
zx_status_t vc_init_gfx(vc_gfx_t* gfx, gfx_surface* test);
#else
zx_status_t vc_init_gfx(vc_gfx_t* gfx, zx_handle_t fb_vmo, int32_t width, int32_t height,
                        zx_pixel_format_t format, int32_t stride);
void vc_free_gfx(vc_gfx_t* gfx);
#endif

void vc_gfx_invalidate(vc_gfx_t* gfx, vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h);
void vc_gfx_invalidate_all(vc_gfx_t* gfx, vc_t* vc);
void vc_gfx_invalidate_status(vc_gfx_t* gfx);
void vc_gfx_invalidate_region(vc_gfx_t* gfx, vc_t* vc, unsigned x, unsigned y, unsigned w,
                              unsigned h);
void vc_gfx_draw_char(vc_gfx_t* gfx, vc_t* vc, vc_char_t ch, unsigned x, unsigned y, bool invert);

void vc_gfx_invalidate_mem(vc_gfx_t* gfx, size_t offset, size_t size);

#endif  // SRC_BRINGUP_VIRTCON_VC_GFX_H_
