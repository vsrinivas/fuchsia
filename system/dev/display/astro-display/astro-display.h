// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/canvas.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ddk/protocol/display-controller.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/listnode.h>
#include <zircon/types.h>
#include <threads.h>

#define DISP_ERROR(fmt, ...)    zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_INFO(fmt, ...)     zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_TRACE              zxlogf(INFO, "[%s %d]\n", __func__, __LINE__)

#define DISPLAY_MASK(start, count) (((1 << (count)) - 1) << (start))
#define DISPLAY_SET_MASK(mask, start, count, value) \
                        ((mask & ~DISPLAY_MASK(start, count)) | \
                                (((value) << (start)) & DISPLAY_MASK(start, count)))

#define READ32_DMC_REG(a)               readl(io_buffer_virt(&display->mmio_dmc) + a)
#define WRITE32_DMC_REG(a, v)           writel(v, io_buffer_virt(&display->mmio_dmc) + a)

#define READ32_VPU_REG(a)               readl(io_buffer_virt(&display->mmio_vpu) + a)
#define WRITE32_VPU_REG(a, v)           writel(v, io_buffer_virt(&display->mmio_vpu) + a)

#define SET_BIT32(x, dest, value, count, start) \
            WRITE32_##x##_REG(dest, (READ32_##x##_REG(dest) & ~DISPLAY_MASK(start, count)) | \
                                (((value) << (start)) & DISPLAY_MASK(start, count)))

#define WRITE32_REG(x, a, v)    WRITE32_##x##_REG(a, v)
#define READ32_REG(x, a)        READ32_##x##_REG(a)

#define DMC_CAV_LUT_DATAL               (0x12 << 2)
#define DMC_CAV_LUT_DATAH               (0x13 << 2)
#define DMC_CAV_LUT_ADDR                (0x14 << 2)

#define DMC_CAV_ADDR_LMASK              (0x1fffffff)
#define DMC_CAV_WIDTH_LMASK             (0x7)
#define DMC_CAV_WIDTH_LWID              (3)
#define DMC_CAV_WIDTH_LBIT              (29)

#define DMC_CAV_WIDTH_HMASK             (0x1ff)
#define DMC_CAV_WIDTH_HBIT              (0)
#define DMC_CAV_HEIGHT_MASK             (0x1fff)
#define DMC_CAV_HEIGHT_BIT              (9)

#define DMC_CAV_LUT_ADDR_INDEX_MASK     (0x7)
#define DMC_CAV_LUT_ADDR_RD_EN          (1 << 8)
#define DMC_CAV_LUT_ADDR_WR_EN          (2 << 8)

#define CANVAS_BYTE_STRIDE              (32)

#define PANEL_DISPLAY_ID                (1)

typedef struct {
    zx_device_t*                        zxdev;
    platform_device_protocol_t          pdev;
    zx_device_t*                        parent;
    zx_device_t*                        mydevice;
    zx_device_t*                        fbdevice;
    zx_handle_t                         bti;
    zx_handle_t                         inth;

    gpio_protocol_t                     gpio;
    i2c_protocol_t                      i2c;
    canvas_protocol_t                   canvas;

    thrd_t                              main_thread;
    thrd_t                              vsync_thread;
    // Lock for general display state, in particular display_id.
    mtx_t                               display_lock;
    // Lock for imported images.
    mtx_t                               image_lock;
    // Lock for the display callback, for enforcing an ordering on
    // hotplug callbacks. Should be acquired before display_lock.
    mtx_t                               cb_lock;
    // TODO(stevensd): This can race if this is changed right after
    // vsync but before the interrupt is handled.
    uint8_t                             current_image;

    io_buffer_t                         mmio_dmc;
    io_buffer_t                         mmio_vpu;
    io_buffer_t                         fbuffer;
    zx_handle_t                         fb_vmo;
    uint8_t                             fb_canvas_idx;
    zx_handle_t                         vsync_interrupt;

    uint32_t                            width;
    uint32_t                            height;
    uint32_t                            stride;
    zx_pixel_format_t                   format;

    uint8_t                             input_color_format;
    uint8_t                             output_color_format;
    uint8_t                             color_depth;

    bool                                console_visible;
    zx_display_cb_t                     ownership_change_callback;
    void*                               ownership_change_cookie;

    display_controller_cb_t*            dc_cb;
    void*                               dc_cb_ctx;
    list_node_t                         imported_images;

} astro_display_t;

zx_status_t configure_osd(astro_display_t* display, uint8_t default_idx);
void flip_osd(astro_display_t* display, uint8_t idx);

