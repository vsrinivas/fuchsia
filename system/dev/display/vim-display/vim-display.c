// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>

#include <zircon/syscalls.h>
#include <zircon/assert.h>

typedef struct {
    zx_display_info_t disp_info;
    pdev_vmo_buffer_t buffer;
} vim_display_t;

static zx_status_t vc_set_mode(void* ctx, zx_display_info_t* info) {
    return ZX_OK;
}

static zx_status_t vc_get_mode(void* ctx, zx_display_info_t* info) {
    vim_display_t* display = ctx;
    memcpy(info, &display->disp_info, sizeof(zx_display_info_t));
    return ZX_OK;
}

static zx_status_t vc_get_framebuffer(void* ctx, void** framebuffer) {
    if (!framebuffer) return ZX_ERR_INVALID_ARGS;
    vim_display_t* display = ctx;
    *framebuffer = display->buffer.vaddr;
    return ZX_OK;
}

static void vc_flush_framebuffer(void* ctx) {
    vim_display_t* display = ctx;
    pdev_vmo_buffer_cache_flush(&display->buffer, 0, display->buffer.size);
}

static display_protocol_ops_t vc_display_proto = {
    .set_mode = vc_set_mode,
    .get_mode = vc_get_mode,
    .get_framebuffer = vc_get_framebuffer,
    .flush = vc_flush_framebuffer
};

static zx_protocol_device_t empty_device_proto = {
    .version = DEVICE_OPS_VERSION,
};

zx_status_t vim_display_bind(void* ctx, zx_device_t* parent) {
    vim_display_t* display = calloc(1, sizeof(vim_display_t));
    if (!display) {
        return ZX_ERR_NO_MEMORY;
    }

   platform_device_protocol_t pdev;
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pdev);
    if (status !=  ZX_OK) {
        free(display);
        return status;
    }

    status = pdev_map_mmio_buffer(&pdev, 0, ZX_CACHE_POLICY_CACHED, &display->buffer);
    if (status != ZX_OK) {
        return status;
    }

    display->disp_info.format = ZX_PIXEL_FORMAT_RGB_565;
    display->disp_info.width = 1920;
    display->disp_info.height = 1080;
    display->disp_info.stride = 1920;

    zx_set_framebuffer(get_root_resource(), display->buffer.vaddr,
                       display->buffer.size, display->disp_info.format,
                       display->disp_info.width, display->disp_info.height,
                       display->disp_info.stride);

    device_add_args_t vc_fbuff_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "vim-display",
        .ctx = display,
        .ops = &empty_device_proto,
        .proto_id = ZX_PROTOCOL_DISPLAY,
        .proto_ops = &vc_display_proto,
    };

    status = device_add(parent, &vc_fbuff_args, NULL);
    if (status != ZX_OK) {
        free(display);
    }
    return status;
}

static zx_driver_ops_t vim_display_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = vim_display_bind,
};

ZIRCON_DRIVER_BEGIN(vim_display, vim_display_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_KHADAS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_VIM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_PID_VIM_DISPLAY),
ZIRCON_DRIVER_END(vim_display)
