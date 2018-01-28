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
#include <ddk/protocol/display.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <hw/reg.h>
#include <soc/aml-s912/s912-hw.h>

#include <zircon/syscalls.h>
#include <zircon/assert.h>

#define DISPLAY_WIDTH       1920
#define DISPLAY_HEIGHT      1080
#define DISPLAY_STRIDE      DISPLAY_WIDTH
#define DISPLAY_PIXEL_SIZE  sizeof(uint16_t)
#define DISPLAY_SIZE        (DISPLAY_STRIDE * DISPLAY_HEIGHT * DISPLAY_PIXEL_SIZE)

typedef struct {
    zx_display_info_t disp_info;
    pdev_vmo_buffer_t dmc_buffer;
    pdev_vmo_buffer_t fb_buffer;
} vim_display_t;

static zx_status_t vim_set_mode(void* ctx, zx_display_info_t* info) {
    return ZX_OK;
}

static zx_status_t vim_get_mode(void* ctx, zx_display_info_t* info) {
    vim_display_t* display = ctx;
    memcpy(info, &display->disp_info, sizeof(zx_display_info_t));
    return ZX_OK;
}

static zx_status_t vim_get_framebuffer(void* ctx, void** framebuffer) {
    if (!framebuffer) return ZX_ERR_INVALID_ARGS;
    vim_display_t* display = ctx;
    *framebuffer = display->fb_buffer.vaddr;
    return ZX_OK;
}

static void vim_flush_framebuffer(void* ctx) {
    vim_display_t* display = ctx;
    pdev_vmo_buffer_cache_flush(&display->fb_buffer, 0, DISPLAY_SIZE);
}

static display_protocol_ops_t vim_display_proto = {
    .set_mode = vim_set_mode,
    .get_mode = vim_get_mode,
    .get_framebuffer = vim_get_framebuffer,
    .flush = vim_flush_framebuffer
};

static void vim_display_release(void* ctx) {
    vim_display_t* display = ctx;
    pdev_vmo_buffer_release(&display->fb_buffer);
    pdev_vmo_buffer_release(&display->dmc_buffer);
    free(display);
}

static zx_protocol_device_t vim_display_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = vim_display_release,
};

zx_status_t vim_display_bind(void* ctx, zx_device_t* parent) {
    vim_display_t* display = calloc(1, sizeof(vim_display_t));
    if (!display) {
        return ZX_ERR_NO_MEMORY;
    }

   platform_device_protocol_t pdev;
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pdev);
    if (status !=  ZX_OK) {
        goto fail;
    }

    // map DMC registers
    status = pdev_map_mmio_buffer(&pdev, 0, ZX_CACHE_POLICY_CACHED, &display->dmc_buffer);
    if (status != ZX_OK) {
        goto fail;
    }

    // allocate frame buffer
   status = pdev_map_contig_buffer(&pdev, DISPLAY_SIZE, 0,
                                   ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                   &display->fb_buffer);
    if (status != ZX_OK) {
        goto fail;
    }

    // set framebuffer address in DMC, read/modify/write
    void* dmc = display->dmc_buffer.vaddr;

    readl(dmc + DMC_CAV_LUT_DATAL);
    uint32_t lut_datah = readl(dmc + DMC_CAV_LUT_DATAH);
    uint32_t lut_addr = readl(dmc + DC_CAV_LUT_ADDR);
    writel(display->fb_buffer.paddr >> 3, dmc + DMC_CAV_LUT_DATAL);
    writel(lut_datah, dmc + DMC_CAV_LUT_DATAH);
    lut_addr |= DC_CAV_LUT_ADDR_WR_EN;
    writel(lut_addr, dmc + DC_CAV_LUT_ADDR);

    display->disp_info.format = ZX_PIXEL_FORMAT_RGB_565;
    display->disp_info.width = DISPLAY_WIDTH;
    display->disp_info.height = DISPLAY_HEIGHT;
    display->disp_info.stride = DISPLAY_STRIDE;

    zx_set_framebuffer(get_root_resource(), display->fb_buffer.vaddr,
                       DISPLAY_SIZE, display->disp_info.format,
                       display->disp_info.width, display->disp_info.height,
                       display->disp_info.stride);

    device_add_args_t vim_fbuff_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "vim-display",
        .ctx = display,
        .ops = &vim_display_device_proto,
        .proto_id = ZX_PROTOCOL_DISPLAY,
        .proto_ops = &vim_display_proto,
    };

    status = device_add(parent, &vim_fbuff_args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }
    return ZX_OK;

fail:
    vim_display_release(display);
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
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_VIM_DISPLAY),
ZIRCON_DRIVER_END(vim_display)
