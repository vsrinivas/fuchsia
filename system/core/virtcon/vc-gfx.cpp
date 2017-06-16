// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gfx/gfx.h>

#include <magenta/device/display.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>

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

mx_status_t vc_init_gfx(gfx_surface* test) {
    const gfx_font* font = vc_get_font();
    vc_font = font;

    vc_test_gfx = test;

    // init the status bar
    vc_tb_gfx = gfx_create_surface(NULL, test->width, font->height,
                                   test->stride, test->format, 0);
    if (!vc_tb_gfx) {
        return MX_ERR_NO_MEMORY;
    }

    // init the main surface
    vc_gfx = gfx_create_surface(NULL, test->width, test->height,
                                test->stride, test->format, 0);
    if (!vc_gfx) {
        gfx_surface_destroy(vc_tb_gfx);
        vc_tb_gfx = NULL;
        return MX_ERR_NO_MEMORY;
    }

    return MX_OK;
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
static int vc_gfx_fd = -1;
static mx_handle_t vc_gfx_vmo = 0;
static uintptr_t vc_gfx_mem = 0;
static size_t vc_gfx_size = 0;

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
        mx_vmar_unmap(mx_vmar_root_self(), vc_gfx_mem, vc_gfx_size);
        vc_gfx_mem = 0;
    }
    if (vc_gfx_fd >= 0) {
        close(vc_gfx_fd);
        vc_gfx_fd = -1;
    }
}

mx_status_t vc_init_gfx(int fd) {
    const gfx_font* font = vc_get_font();
    vc_font = font;

    ioctl_display_get_fb_t fb;
    vc_gfx_fd = fd;
    uintptr_t ptr;


    mx_status_t r;
    if (ioctl_display_get_fb(fd, &fb) < 0) {
        printf("vc_alloc: cannot get fb from driver instance\n");
        r = MX_ERR_INTERNAL;
        goto fail;
    }

    vc_gfx_vmo = fb.vmo;
    vc_gfx_size = fb.info.stride * fb.info.pixelsize * fb.info.height;

    if ((r = mx_vmar_map(mx_vmar_root_self(), 0, vc_gfx_vmo, 0, vc_gfx_size,
                         MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &vc_gfx_mem)) < 0) {
        goto fail;
    }

    r = MX_ERR_NO_MEMORY;
    // init the status bar
    if ((vc_tb_gfx = gfx_create_surface((void*) vc_gfx_mem, fb.info.width, font->height,
                                        fb.info.stride, fb.info.format, 0)) == NULL) {
        goto fail;
    }

    // init the main surface
    ptr = vc_gfx_mem + fb.info.stride * font->height * fb.info.pixelsize;
    if ((vc_gfx = gfx_create_surface((void*) ptr, fb.info.width, fb.info.height - font->height,
                                     fb.info.stride, fb.info.format, 0)) == NULL) {
        goto fail;
    }

    return MX_OK;

fail:
    vc_free_gfx();
    return r;
}

void vc_gfx_invalidate_all(vc_t* vc) {
    if (vc->active) {
        ioctl_display_flush_fb(vc_gfx_fd);
    }
}

void vc_gfx_invalidate_status() {
    ioctl_display_region_t r = {
        .x = 0,
        .y = 0,
        .width = vc_tb_gfx->width,
        .height = vc_tb_gfx->height,
    };
    ioctl_display_flush_fb_region(vc_gfx_fd, &r);
}

// pixel coords
void vc_gfx_invalidate_region(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
    if (vc->active) {
        ioctl_display_region_t r = {
            .x = x,
            .y = vc->charh + y,
            .width = w,
            .height = h,
        };
        ioctl_display_flush_fb_region(vc_gfx_fd, &r);
    }
}

// text coords
void vc_gfx_invalidate(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
    if (vc->active) {
        ioctl_display_region_t r = {
            .x = x * vc->charw,
            .y = vc->charh + y * vc->charh,
            .width = w * vc->charw,
            .height = h * vc->charh,
        };
        ioctl_display_flush_fb_region(vc_gfx_fd, &r);
    }
}
#endif
