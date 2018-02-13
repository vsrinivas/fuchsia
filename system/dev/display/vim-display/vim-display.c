// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/debug.h>
#include <ddk/binding.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>

#include <zircon/syscalls.h>
#include <zircon/assert.h>
#include <hw/reg.h>
#include "vim-display.h"
#include "hdmitx.h"

/* Default formats */
static const uint8_t _ginput_color_format   = HDMI_COLOR_FORMAT_444;
static const uint8_t _goutput_color_format  = HDMI_COLOR_FORMAT_444;
static const uint8_t _gcolor_depth          = HDMI_COLOR_DEPTH_24B;

// MMIO indices (based on vim2_display_mmios)
enum {
    MMIO_PRESET,
    MMIO_HDMITX,
    MMIO_HIU,
    MMIO_VPU,
    MMIO_HDMTX_SEC,
    MMIO_DMC,
};


static zx_status_t vc_set_mode(void* ctx, zx_display_info_t* info) {
    DISP_INFO("?\n");
    return ZX_OK;
}

static zx_status_t vc_get_mode(void* ctx, zx_display_info_t* info) {
    vim2_display_t* display = ctx;
    memcpy(info, &display->disp_info, sizeof(zx_display_info_t));
    return ZX_OK;
}

static zx_status_t vc_get_framebuffer(void* ctx, void** framebuffer) {
    if (!framebuffer) return ZX_ERR_INVALID_ARGS;
    vim2_display_t* display = ctx;
    *framebuffer = display->fbuffer.vaddr;
    return ZX_OK;
}

static void vc_flush_framebuffer(void* ctx) {
    vim2_display_t* display = ctx;
    pdev_vmo_buffer_cache_flush(&display->fbuffer, 0,
        (display->disp_info.stride * display->disp_info.height *
            ZX_PIXEL_FORMAT_BYTES(display->disp_info.format)));
}

static display_protocol_ops_t vc_display_proto = {
    .set_mode = vc_set_mode,
    .get_mode = vc_get_mode,
    .get_framebuffer = vc_get_framebuffer,
    .flush = vc_flush_framebuffer
};

static void display_release(void* ctx) {
    vim2_display_t* display = ctx;

    if (display) {
        pdev_vmo_buffer_release(&display->mmio_preset);
        pdev_vmo_buffer_release(&display->mmio_hdmitx);
        pdev_vmo_buffer_release(&display->mmio_hiu);
        pdev_vmo_buffer_release(&display->mmio_vpu);
        pdev_vmo_buffer_release(&display->mmio_hdmitx_sec);
        pdev_vmo_buffer_release(&display->mmio_dmc);
        pdev_vmo_buffer_release(&display->fbuffer);
        free(display->edid_buf);
    }
    free(display);
}

static zx_protocol_device_t display_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release =  display_release,
};

zx_status_t vim2_display_bind(void* ctx, zx_device_t* parent) {
    vim2_display_t* display = calloc(1, sizeof(vim2_display_t));
    if (!display) {
        DISP_ERROR("Could not allocated display structure\n");
        return ZX_ERR_NO_MEMORY;
    }

   platform_device_protocol_t pdev;
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pdev);
    if (status !=  ZX_OK) {
        DISP_ERROR("Could not get parent protocol\n");
        goto fail;
    }

    // Map all the various MMIOs
    status = pdev_map_mmio_buffer(&pdev, MMIO_PRESET, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_preset);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO PRESET\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&pdev, MMIO_HDMITX, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_hdmitx);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO HDMITX\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&pdev, MMIO_HIU, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_hiu);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO HIU\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&pdev, MMIO_VPU, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_vpu);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO VPU\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&pdev, MMIO_HDMTX_SEC, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_hdmitx_sec);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO HDMITX SEC\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&pdev, MMIO_DMC, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_dmc);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO DMC\n");
        goto fail;
    }

    // initialize HDMI
    status = init_hdmi_hardware(display);
    if (status != ZX_OK) {
        DISP_ERROR("HDMI hardware initialization failed\n");
        goto fail;
    }

    // Create EDID Buffer
    display->edid_buf = calloc(1, EDID_BUF_SIZE);
    if (!display->edid_buf) {
        DISP_ERROR("Could not allocated EDID BUf of size %d\n", EDID_BUF_SIZE);
        goto fail;
    }

    status = get_preferred_res(display, EDID_BUF_SIZE);
    if (status != ZX_OK) {
        DISP_ERROR("No display connected!\n");
        goto fail;

    }

    // allocate frame buffer
    display->disp_info.format = ZX_PIXEL_FORMAT_RGB_565;
    display->disp_info.width  = display->p->timings.hactive;
    display->disp_info.height = display->p->timings.vactive;
    display->disp_info.stride = display->p->timings.hactive;

   status = pdev_map_contig_buffer(&pdev,
                        (display->disp_info.stride * display->disp_info.height *
                            ZX_PIXEL_FORMAT_BYTES(display->disp_info.format)),
                        0,
                        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                        &display->fbuffer);
    if (status != ZX_OK) {
        goto fail;
    }


    display->input_color_format = _ginput_color_format;
    display->output_color_format = _goutput_color_format;
    display->color_depth = _gcolor_depth;


    status = init_hdmi_interface(display, display->p);
    if (status != ZX_OK) {
        DISP_ERROR("HDMI interface initialization failed\n");
        goto fail;
    }

    /* Configure Canvas memory */
    configure_canvas(display);

    /* OSD2 setup */
    configure_osd2(display);

    zx_set_framebuffer(get_root_resource(), display->fbuffer.vaddr,
                       display->fbuffer.size, display->disp_info.format,
                       display->disp_info.width, display->disp_info.height,
                       display->disp_info.stride);

    device_add_args_t vc_fbuff_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "vim2-display",
        .ctx = display,
        .ops = &display_device_proto,
        .proto_id = ZX_PROTOCOL_DISPLAY,
        .proto_ops = &vc_display_proto,
    };

    status = device_add(parent, &vc_fbuff_args, NULL);
    if (status != ZX_OK) {
        free(display);
    }
    return status;

    return ZX_OK;

fail:
    DISP_ERROR("bind failed! %d\n", status);
    display_release(display);
    return status;

}

static zx_driver_ops_t vim2_display_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = vim2_display_bind,
};

ZIRCON_DRIVER_BEGIN(vim2_display, vim2_display_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_KHADAS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_VIM2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_VIM_DISPLAY),
ZIRCON_DRIVER_END(vim_2display)
