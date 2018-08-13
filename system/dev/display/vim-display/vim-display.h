// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "edid.h"
#include <assert.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/canvas.h>
#include <ddk/protocol/display-controller.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/listnode.h>
#include <zircon/pixelformat.h>

#define DISP_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_TRACE  zxlogf(INFO, "[%s %d]\n", __func__, __LINE__)

#define NUM_CANVAS_ENTRIES 256
#define CANVAS_BYTE_STRIDE 32

// From uBoot source
#define VFIFO2VD_TO_HDMI_LATENCY 2
#define EDID_BUF_SIZE       256

typedef struct vim2_display {
    zx_device_t*                        zxdev;
    platform_device_protocol_t          pdev;
    zx_device_t*                        parent;
    zx_device_t*                        mydevice;
    zx_handle_t                         bti;
    zx_handle_t                         inth;

    gpio_protocol_t                     gpio;
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
    bool                                current_image_valid;
    uint8_t                             current_image;
    uint8_t                             canvas_entries[NUM_CANVAS_ENTRIES / 8];

    io_buffer_t                         mmio_preset;
    io_buffer_t                         mmio_hdmitx;
    io_buffer_t                         mmio_hiu;
    io_buffer_t                         mmio_vpu;
    io_buffer_t                         mmio_hdmitx_sec;
    io_buffer_t                         mmio_dmc;
    io_buffer_t                         mmio_cbus;

    zx_handle_t                         vsync_interrupt;

    bool                                display_attached;
    // The current display id (if display_attached), or the next display id
    uint64_t                            display_id;
    uint32_t                            width;
    uint32_t                            height;
    uint32_t                            stride;
    zx_pixel_format_t                   format;

    uint8_t                             input_color_format;
    uint8_t                             output_color_format;
    uint8_t                             color_depth;

    uint8_t*                            edid_buf;
    uint16_t                            edid_length;
    struct hdmi_param*                  p;
    detailed_timing_t                   std_raw_dtd;
    disp_timing_t                       std_disp_timing;
    disp_timing_t                       pref_disp_timing;

    display_controller_cb_t*            dc_cb;
    void*                               dc_cb_ctx;
    list_node_t                         imported_images;
} vim2_display_t;

void disable_osd2(vim2_display_t* display);
zx_status_t configure_osd2(vim2_display_t* display);
void flip_osd2(vim2_display_t* display, uint8_t idx);
void osd_debug_dump_register_all(vim2_display_t* display);
void osd_dump(vim2_display_t* display);
zx_status_t get_preferred_res(vim2_display_t* display, uint16_t edid_buf_size);
struct hdmi_param** get_supported_formats(void);
