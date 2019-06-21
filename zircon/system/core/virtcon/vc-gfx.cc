// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gfx/gfx.h>
#include <string.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "vc.h"

gfx_surface* vc_gfx;
gfx_surface* vc_tb_gfx;

const gfx_font* vc_font;

void vc_gfx_draw_char(vc_t* vc, vc_char_t ch, unsigned x, unsigned y,
                      bool invert) {
    uint8_t fg_color = vc_char_get_fg_color(ch);
    uint8_t bg_color = vc_char_get_bg_color(ch);
    if (invert) {
        // Swap the colors.
        uint8_t temp = fg_color;
        fg_color = bg_color;
        bg_color = temp;
    }
    gfx_putchar(vc_gfx, vc->font, vc_char_get_char(ch),
                x * vc->charw, y * vc->charh,
                palette_to_color(vc, fg_color),
                palette_to_color(vc, bg_color));
}

#if BUILD_FOR_TEST
static gfx_surface* vc_test_gfx;

zx_status_t vc_init_gfx(gfx_surface* test) {
    const gfx_font* font = vc_get_font();
    vc_font = font;

    vc_test_gfx = test;

    // init the status bar
    vc_tb_gfx = gfx_create_surface(NULL, test->width, font->height,
                                   test->stride, test->format, 0);
    if (!vc_tb_gfx) {
        return ZX_ERR_NO_MEMORY;
    }

    // init the main surface
    vc_gfx = gfx_create_surface(NULL, test->width, test->height,
                                test->stride, test->format, 0);
    if (!vc_gfx) {
        gfx_surface_destroy(vc_tb_gfx);
        vc_tb_gfx = NULL;
        return ZX_ERR_NO_MEMORY;
    }

    g_status_width = vc_gfx->width / font->width;

    return ZX_OK;
}

void vc_gfx_invalidate_all(vc_t* vc) {
    gfx_copylines(vc_test_gfx, vc_tb_gfx, 0, 0, vc_tb_gfx->height);
    gfx_copylines(vc_test_gfx, vc_gfx, 0, vc_tb_gfx->height, vc_gfx->height - vc_tb_gfx->height);
}

void vc_gfx_invalidate_status() {
    gfx_copylines(vc_test_gfx, vc_tb_gfx, 0, 0, vc_tb_gfx->height);
}

void vc_gfx_invalidate(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
    unsigned desty = vc_tb_gfx->height + y * vc->charh;
    if ((x == 0) && (w == vc->columns)) {
        gfx_copylines(vc_test_gfx, vc_gfx, y * vc->charh, desty, h * vc->charh);
    } else {
        gfx_blend(vc_test_gfx, vc_gfx, x * vc->charw, y * vc->charh,
                  w * vc->charw, h * vc->charh, x * vc->charw, desty);
    }
}

void vc_gfx_invalidate_region(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
    unsigned desty = vc_tb_gfx->height + y;
    if ((x == 0) && (w == vc->columns)) {
        gfx_copylines(vc_test_gfx, vc_gfx, y, desty, h);
    } else {
        gfx_blend(vc_test_gfx, vc_gfx, x, y, w, h, x, desty);
    }
}
#else
static zx_handle_t vc_gfx_vmo = ZX_HANDLE_INVALID;
static uintptr_t vc_gfx_mem = 0;
static size_t vc_gfx_size = 0;

static zx_handle_t vc_hw_gfx_vmo = ZX_HANDLE_INVALID;
static gfx_surface* vc_hw_gfx = 0;
static uintptr_t vc_hw_gfx_mem = 0;

void vc_free_gfx() {
    if (vc_gfx) {
        gfx_surface_destroy(vc_gfx);
        vc_gfx = NULL;
    }
    if (vc_tb_gfx) {
        gfx_surface_destroy(vc_tb_gfx);
        vc_tb_gfx = NULL;
    }
    if (vc_gfx_mem) {
        zx_vmar_unmap(zx_vmar_root_self(), vc_gfx_mem, vc_gfx_size);
        vc_gfx_mem = 0;
    }
    if (vc_gfx_vmo) {
        zx_handle_close(vc_gfx_vmo);
        vc_gfx_vmo = ZX_HANDLE_INVALID;
    }
    if (vc_hw_gfx_mem) {
        zx_vmar_unmap(zx_vmar_root_self(), vc_hw_gfx_mem, vc_gfx_size);
        vc_hw_gfx_mem = 0;
    }
    if (vc_hw_gfx_vmo) {
        zx_handle_close(vc_hw_gfx_vmo);
        vc_hw_gfx_vmo = ZX_HANDLE_INVALID;
    }
}

zx_status_t vc_init_gfx(zx_handle_t fb_vmo, int32_t width, int32_t height,
                        zx_pixel_format_t format, int32_t stride) {
    const gfx_font* font = vc_get_font();
    vc_font = font;

    vc_gfx_size = stride * ZX_PIXEL_FORMAT_BYTES(format) * height;

    zx_status_t r;
    // If we can't efficiently read from the framebuffer VMO, create a secondary
    // surface using a regular VMO and blit contents between the two.
    if ((r = zx_vmo_set_cache_policy(fb_vmo, ZX_CACHE_POLICY_CACHED)) == ZX_ERR_BAD_STATE) {
        if ((r = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                             0, fb_vmo, 0, vc_gfx_size, &vc_hw_gfx_mem)) < 0) {
            goto fail;
        }

        if ((vc_hw_gfx = gfx_create_surface((void*) vc_hw_gfx_mem, width, height,
                                            stride, format, 0)) == NULL) {
            r = ZX_ERR_INTERNAL;
            goto fail;
        }

        vc_hw_gfx_vmo = fb_vmo;

        if ((r = zx_vmo_create(vc_gfx_size, 0, &fb_vmo)) < 0) {
            goto fail;
        }
    } else if (r != ZX_OK) {
        goto fail;
    }

    uintptr_t ptr;
    vc_gfx_vmo = fb_vmo;
    if ((r = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                         0, vc_gfx_vmo, 0, vc_gfx_size, &vc_gfx_mem)) < 0) {
        goto fail;
    }

    r = ZX_ERR_NO_MEMORY;
    // init the status bar
    if ((vc_tb_gfx = gfx_create_surface((void*) vc_gfx_mem, width, font->height,
                                        stride, format, 0)) == NULL) {
        goto fail;
    }

    // init the main surface
    ptr = vc_gfx_mem + stride * font->height * ZX_PIXEL_FORMAT_BYTES(format);
    if ((vc_gfx = gfx_create_surface((void*) ptr, width, height - font->height,
                                     stride, format, 0)) == NULL) {
        goto fail;
    }

    g_status_width = vc_gfx->width / font->width;

    return ZX_OK;

fail:
    vc_free_gfx();
    return r;
}

static void vc_gfx_invalidate_mem(size_t offset, size_t size) {
    void* ptr;
    if (vc_hw_gfx_mem) {
        void* data_ptr = reinterpret_cast<void*>(vc_gfx_mem + offset);
        ptr = reinterpret_cast<void*>(vc_hw_gfx_mem + offset);
        memcpy(ptr, data_ptr, size);
    } else {
        ptr = reinterpret_cast<void*>(vc_gfx_mem + offset);
    }
    zx_cache_flush(ptr, size, ZX_CACHE_FLUSH_DATA);
}

void vc_gfx_invalidate_all(vc_t* vc) {
    if (g_vc_owns_display || vc->active) {
        vc_gfx_invalidate_mem(0, vc_gfx_size);
    }
}

void vc_gfx_invalidate_status() {
    vc_gfx_invalidate_mem(0, vc_tb_gfx->stride * vc_tb_gfx->height * vc_tb_gfx->pixelsize);
}

// pixel coords
void vc_gfx_invalidate_region(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
    if (!g_vc_owns_display || !vc->active) {
        return;
    }
    uint32_t flush_size = w * vc_gfx->pixelsize;
    size_t offset = vc_gfx->stride * (vc->charh + y) * vc_gfx->pixelsize;
    for (unsigned i = 0; i < h; i++, offset += (vc_gfx->stride * vc_gfx->pixelsize)) {
        vc_gfx_invalidate_mem(offset, flush_size);
    }
}

// text coords
void vc_gfx_invalidate(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
    vc_gfx_invalidate_region(vc, x * vc->charw, y * vc->charh, w * vc->charw, h * vc->charh);
}
#endif
