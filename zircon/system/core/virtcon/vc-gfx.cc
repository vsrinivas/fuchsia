// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <gfx/gfx.h>

#include "vc.h"

void vc_gfx_draw_char(vc_gfx_t* gfx, vc_t* vc, vc_char_t ch, unsigned x, unsigned y, bool invert) {
  uint8_t fg_color = vc_char_get_fg_color(ch);
  uint8_t bg_color = vc_char_get_bg_color(ch);
  if (invert) {
    // Swap the colors.
    uint8_t temp = fg_color;
    fg_color = bg_color;
    bg_color = temp;
  }
  gfx_putchar(gfx->vc_gfx, vc->font, vc_char_get_char(ch), x * vc->charw, y * vc->charh,
              palette_to_color(vc, fg_color), palette_to_color(vc, bg_color));
}

#if BUILD_FOR_TEST

zx_status_t vc_init_gfx(vc_gfx_t* gfx, gfx_surface* test) {
  const gfx_font* font = vc_get_font();
  gfx->vc_font = font;

  gfx->vc_test_gfx = test;

  // init the status bar
  gfx->vc_status_bar_gfx =
      gfx_create_surface(NULL, test->width, font->height, test->stride, test->format, 0);
  if (!gfx->vc_status_bar_gfx) {
    return ZX_ERR_NO_MEMORY;
  }

  // init the main surface
  gfx->vc_gfx = gfx_create_surface(NULL, test->width, test->height, test->stride, test->format, 0);
  if (!gfx->vc_gfx) {
    gfx_surface_destroy(gfx->vc_status_bar_gfx);
    gfx->vc_status_bar_gfx = NULL;
    return ZX_ERR_NO_MEMORY;
  }

  g_status_width = gfx->vc_gfx->width / font->width;

  return ZX_OK;
}

void vc_gfx_invalidate_all(vc_gfx_t* gfx, vc_t* vc) {
  gfx_copylines(gfx->vc_test_gfx, gfx->vc_status_bar_gfx, 0, 0, gfx->vc_status_bar_gfx->height);
  gfx_copylines(gfx->vc_test_gfx, gfx->vc_gfx, 0, gfx->vc_status_bar_gfx->height,
                gfx->vc_gfx->height - gfx->vc_status_bar_gfx->height);
}

void vc_gfx_invalidate_status(vc_gfx_t* gfx) {
  gfx_copylines(gfx->vc_test_gfx, gfx->vc_status_bar_gfx, 0, 0, gfx->vc_status_bar_gfx->height);
}

void vc_gfx_invalidate(vc_gfx_t* gfx, vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
  unsigned desty = gfx->vc_status_bar_gfx->height + y * vc->charh;
  if ((x == 0) && (w == vc->columns)) {
    gfx_copylines(gfx->vc_test_gfx, gfx->vc_gfx, y * vc->charh, desty, h * vc->charh);
  } else {
    gfx_blend(gfx->vc_test_gfx, gfx->vc_gfx, x * vc->charw, y * vc->charh, w * vc->charw,
              h * vc->charh, x * vc->charw, desty);
  }
}

void vc_gfx_invalidate_region(vc_gfx_t* gfx, vc_t* vc, unsigned x, unsigned y, unsigned w,
                              unsigned h) {
  unsigned desty = gfx->vc_status_bar_gfx->height + y;
  if ((x == 0) && (w == vc->columns)) {
    gfx_copylines(gfx->vc_test_gfx, gfx->vc_gfx, y, desty, h);
  } else {
    gfx_blend(gfx->vc_test_gfx, gfx->vc_gfx, x, y, w, h, x, desty);
  }
}
#else

void vc_free_gfx(vc_gfx_t* gfx) {
  if (gfx->vc_gfx) {
    gfx_surface_destroy(gfx->vc_gfx);
    gfx->vc_gfx = NULL;
  }
  if (gfx->vc_status_bar_gfx) {
    gfx_surface_destroy(gfx->vc_status_bar_gfx);
    gfx->vc_status_bar_gfx = NULL;
  }
  if (gfx->vc_gfx_mem) {
    zx_vmar_unmap(zx_vmar_root_self(), gfx->vc_gfx_mem, gfx->vc_gfx_size);
    gfx->vc_gfx_mem = 0;
  }
  if (gfx->vc_gfx_vmo) {
    zx_handle_close(gfx->vc_gfx_vmo);
    gfx->vc_gfx_vmo = ZX_HANDLE_INVALID;
  }
  if (gfx->vc_hw_gfx_mem) {
    zx_vmar_unmap(zx_vmar_root_self(), gfx->vc_hw_gfx_mem, gfx->vc_gfx_size);
    gfx->vc_hw_gfx_mem = 0;
  }
}

zx_status_t vc_init_gfx(vc_gfx_t* gfx, zx_handle_t fb_vmo, int32_t width, int32_t height,
                        zx_pixel_format_t format, int32_t stride) {
  const gfx_font* font = vc_get_font();
  gfx->vc_font = font;

  gfx->vc_gfx_size = stride * ZX_PIXEL_FORMAT_BYTES(format) * height;

  zx_status_t r;
  // If we can't efficiently read from the framebuffer VMO, create a secondary
  // surface using a regular VMO and blit contents between the two.
  if ((r = zx_vmo_set_cache_policy(fb_vmo, ZX_CACHE_POLICY_CACHED)) == ZX_ERR_BAD_STATE) {
    if ((r = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, fb_vmo, 0,
                         gfx->vc_gfx_size, &gfx->vc_hw_gfx_mem)) < 0) {
      goto fail;
    }

    if ((gfx->vc_hw_gfx = gfx_create_surface((void*)gfx->vc_hw_gfx_mem, width, height, stride,
                                             format, 0)) == NULL) {
      r = ZX_ERR_INTERNAL;
      goto fail;
    }

    if ((r = zx_vmo_create(gfx->vc_gfx_size, 0, &fb_vmo)) < 0) {
      goto fail;
    }
  } else if (r != ZX_OK) {
    goto fail;
  }

  uintptr_t ptr;
  gfx->vc_gfx_vmo = fb_vmo;
  if ((r = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, gfx->vc_gfx_vmo,
                       0, gfx->vc_gfx_size, &gfx->vc_gfx_mem)) < 0) {
    goto fail;
  }

  r = ZX_ERR_NO_MEMORY;
  // init the status bar
  if ((gfx->vc_status_bar_gfx = gfx_create_surface((void*)gfx->vc_gfx_mem, width, font->height,
                                                   stride, format, 0)) == NULL) {
    goto fail;
  }

  // init the main surface
  ptr = gfx->vc_gfx_mem + stride * font->height * ZX_PIXEL_FORMAT_BYTES(format);
  if ((gfx->vc_gfx = gfx_create_surface((void*)ptr, width, height - font->height, stride, format,
                                        0)) == NULL) {
    goto fail;
  }

  g_status_width = gfx->vc_gfx->width / font->width;

  return ZX_OK;

fail:
  vc_free_gfx(gfx);
  return r;
}

void vc_gfx_invalidate_mem(vc_gfx_t* gfx, size_t offset, size_t size) {
  void* ptr;
  if (gfx->vc_hw_gfx_mem) {
    void* data_ptr = reinterpret_cast<void*>(gfx->vc_gfx_mem + offset);
    ptr = reinterpret_cast<void*>(gfx->vc_hw_gfx_mem + offset);
    memcpy(ptr, data_ptr, size);
  } else {
    ptr = reinterpret_cast<void*>(gfx->vc_gfx_mem + offset);
  }
  zx_cache_flush(ptr, size, ZX_CACHE_FLUSH_DATA);
}

void vc_gfx_invalidate_all(vc_gfx_t* gfx, vc_t* vc) {
  if (g_vc_owns_display || vc->active) {
    vc_gfx_invalidate_mem(gfx, 0, gfx->vc_gfx_size);
  }
}

void vc_gfx_invalidate_status(vc_gfx_t* gfx) {
  vc_gfx_invalidate_mem(gfx, 0,
                        gfx->vc_status_bar_gfx->stride * gfx->vc_status_bar_gfx->height *
                            gfx->vc_status_bar_gfx->pixelsize);
}

// pixel coords
void vc_gfx_invalidate_region(vc_gfx_t* gfx, vc_t* vc, unsigned x, unsigned y, unsigned w,
                              unsigned h) {
  if (!g_vc_owns_display || !vc->active) {
    return;
  }
  uint32_t flush_size = w * gfx->vc_gfx->pixelsize;
  size_t offset = gfx->vc_gfx->stride * (vc->charh + y) * gfx->vc_gfx->pixelsize;
  for (unsigned i = 0; i < h; i++, offset += (gfx->vc_gfx->stride * gfx->vc_gfx->pixelsize)) {
    vc_gfx_invalidate_mem(gfx, offset, flush_size);
  }
}

// text coords
void vc_gfx_invalidate(vc_gfx_t* gfx, vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
  vc_gfx_invalidate_region(gfx, vc, x * vc->charw, y * vc->charh, w * vc->charw, h * vc->charh);
}
#endif
