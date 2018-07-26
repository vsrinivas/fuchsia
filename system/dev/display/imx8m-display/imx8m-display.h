// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <ddk/io-buffer.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/display-controller.h>
#include <zircon/listnode.h>
#include <zircon/types.h>
#include <threads.h>

#define DISP_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_TRACE  zxlogf(INFO, "[%s %d]\n", __func__, __LINE__)

typedef struct {
    zx_device_t*                        zxdev;
    zx_device_t*                        parent;
    platform_device_protocol_t          pdev;
    zx_handle_t                         bti;

    thrd_t                              main_thread;
    // Lock for general display state, in particular display_id.
    mtx_t                               display_lock;
    // Lock for imported images.
    mtx_t                               image_lock;
    // Lock for the display callback, for enforcing an ordering on
    // hotplug callbacks. Should be acquired before display_lock.
    mtx_t                               cb_lock;

    io_buffer_t                         mmio_dc;
    io_buffer_t                         fbuffer;

    display_controller_cb_t*            dc_cb;
    void*                               dc_cb_ctx;
    list_node_t                         imported_images;
} imx8m_display_t;
