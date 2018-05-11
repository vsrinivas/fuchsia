// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/display.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/listnode.h>
#include <zircon/types.h>
#include <threads.h>

#define DISP_ERROR(fmt, ...)    zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_INFO(fmt, ...)     zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_TRACE              zxlogf(INFO, "[%s %d]\n", __func__, __LINE__)

#define READ32_DMC_REG(a)                readl(io_buffer_virt(&display->mmio_dmc) + a)
#define WRITE32_DMC_REG(a, v)            writel(v, io_buffer_virt(&display->mmio_dmc) + a)


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

#define OSD2_DMC_CAV_INDEX 0x40


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
    thrd_t                              main_thread;

    io_buffer_t                         mmio_dmc;
    io_buffer_t                         fbuffer;
    zx_display_info_t                   disp_info;

    uint8_t                             input_color_format;
    uint8_t                             output_color_format;
    uint8_t                             color_depth;

    bool                                console_visible;
    zx_display_cb_t                     ownership_change_callback;
    void*                               ownership_change_cookie;
} astro_display_t;


