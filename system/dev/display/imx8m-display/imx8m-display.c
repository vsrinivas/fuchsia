// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "imx8m-display.h"
#include <assert.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <hw/reg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/device/display.h>
#include <zircon/syscalls.h>

static zx_status_t vc_set_mode(void* ctx, zx_display_info_t* info) {
    return ZX_OK;
}

static zx_status_t vc_get_mode(void* ctx, zx_display_info_t* info) {
    imx8m_display_t* display = ctx;
    memcpy(info, &display->disp_info, sizeof(zx_display_info_t));
    return ZX_OK;
}

static zx_status_t vc_get_framebuffer(void* ctx, void** framebuffer) {
    if (!framebuffer) return ZX_ERR_INVALID_ARGS;
    imx8m_display_t* display = ctx;
    *framebuffer = io_buffer_virt(&display->fbuffer);
    return ZX_OK;
}

static void flush_framebuffer(imx8m_display_t* display) {
    io_buffer_cache_flush(&display->fbuffer, 0,
        (display->disp_info.stride * display->disp_info.height *
            ZX_PIXEL_FORMAT_BYTES(display->disp_info.format)));
}

static void vc_flush_framebuffer(void* ctx) {
    flush_framebuffer(ctx);
}

static void vc_display_set_ownership_change_callback(void* ctx, zx_display_cb_t callback,
                                                     void* cookie) {
    imx8m_display_t* display = ctx;
    display->ownership_change_callback = callback;
    display->ownership_change_cookie = cookie;
}

static void vc_display_acquire_or_release_display(void* ctx, bool acquire) {
    imx8m_display_t* display = ctx;

    if (acquire) {
        display->console_visible = true;
        if (display->ownership_change_callback)
            display->ownership_change_callback(true, display->ownership_change_cookie);
    } else if (!acquire) {
        display->console_visible = false;
        if (display->ownership_change_callback)
            display->ownership_change_callback(false, display->ownership_change_cookie);
    }
}

static display_protocol_ops_t vc_display_proto = {
    .set_mode = vc_set_mode,
    .get_mode = vc_get_mode,
    .get_framebuffer = vc_get_framebuffer,
    .flush = vc_flush_framebuffer,
    .set_ownership_change_callback = vc_display_set_ownership_change_callback,
    .acquire_or_release_display = vc_display_acquire_or_release_display,
};

static void display_release(void* ctx) {
    imx8m_display_t* display = ctx;

    if (display) {
        io_buffer_release(&display->fbuffer);
        zx_handle_close(display->bti);
    }
    free(display);
}

static zx_protocol_device_t main_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release =  display_release,
};

struct display_client_device {
    imx8m_display_t* display;
    zx_device_t* device;
};

static zx_status_t display_client_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                        void* out_buf, size_t out_len, size_t* out_actual) {
    struct display_client_device* client_struct = ctx;
    imx8m_display_t* display = client_struct->display;
    switch (op) {
    case IOCTL_DISPLAY_GET_FB: {
        if (out_len < sizeof(ioctl_display_get_fb_t))
            return ZX_ERR_INVALID_ARGS;
        ioctl_display_get_fb_t* description = (ioctl_display_get_fb_t*)(out_buf);
        zx_status_t status = zx_handle_duplicate(display->fbuffer.vmo_handle, ZX_RIGHT_SAME_RIGHTS,
                                                    &description->vmo);
        if (status != ZX_OK)
            return ZX_ERR_NO_RESOURCES;
        description->info = display->disp_info;
        *out_actual = sizeof(ioctl_display_get_fb_t);
        if (display->ownership_change_callback)
            display->ownership_change_callback(false, display->ownership_change_cookie);
        return ZX_OK;
    }
    case IOCTL_DISPLAY_FLUSH_FB:
    case IOCTL_DISPLAY_FLUSH_FB_REGION:
        flush_framebuffer(display);
        return ZX_OK;
    default:
        DISP_ERROR("Invalid ioctl %d\n", op);
        return ZX_ERR_INVALID_ARGS;
    }
}

static zx_status_t display_client_close(void* ctx, uint32_t flags) {
    struct display_client_device* client_struct = ctx;
    imx8m_display_t* display = client_struct->display;
    if (display->ownership_change_callback)
        display->ownership_change_callback(true, display->ownership_change_cookie);
    free(ctx);
    return ZX_OK;
}

static zx_protocol_device_t client_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = display_client_ioctl,
    .close = display_client_close,
};

static zx_status_t vc_open(void* ctx, zx_device_t** dev_out, uint32_t flags) {
    struct display_client_device* s = calloc(1, sizeof(struct display_client_device));

    s->display = ctx;

    device_add_args_t vc_fbuff_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "imx8m-display",
        .ctx = s,
        .ops = &client_device_proto,
        .flags = DEVICE_ADD_INSTANCE,
    };
    zx_status_t status = device_add(s->display->fbdevice, &vc_fbuff_args, &s->device);
    if (status != ZX_OK) {
        free(s);
        return status;
    }
    *dev_out = s->device;
    return ZX_OK;
}

static zx_protocol_device_t display_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .open = vc_open,
};

static zx_status_t setup_hdmi(imx8m_display_t* display)
{
    zx_status_t status;

    // allocate frame buffer
    display->disp_info.format = ZX_PIXEL_FORMAT_RGB_x888;
    display->disp_info.width  = 1920;
    display->disp_info.height = 1080;
    display->disp_info.pixelsize = ZX_PIXEL_FORMAT_BYTES(display->disp_info.format);
    // The imx8m display controller needs buffers with a stride that is an even
    // multiple of 32.
    display->disp_info.stride = ROUNDUP(display->disp_info.width,
                                        32 / display->disp_info.pixelsize);

    status = io_buffer_init(&display->fbuffer, display->bti,
                            (display->disp_info.stride * display->disp_info.height *
                             ZX_PIXEL_FORMAT_BYTES(display->disp_info.format)),
                            IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        return status;
    }

    writel(io_buffer_phys(&display->fbuffer), io_buffer_virt(&display->mmio_dc) +  0x80c0);

    zx_set_framebuffer(get_root_resource(), io_buffer_virt(&display->fbuffer),
                       display->fbuffer.size, display->disp_info.format,
                       display->disp_info.width, display->disp_info.height,
                       display->disp_info.stride);

    device_add_args_t vc_fbuff_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "imx8m-display",
        .ctx = display,
        .ops = &display_device_proto,
        .proto_id = ZX_PROTOCOL_DISPLAY,
        .proto_ops = &vc_display_proto,
    };

    status = device_add(display->mydevice, &vc_fbuff_args, &display->fbdevice);
    if (status != ZX_OK) {
        free(display);
        return status;
    }
    return ZX_OK;
}

static int main_hdmi_thread(void *arg)
{
    imx8m_display_t* display = arg;
    setup_hdmi(display);
    return ZX_OK;
}

zx_status_t imx8m_display_bind(void* ctx, zx_device_t* parent) {
    imx8m_display_t* display = calloc(1, sizeof(imx8m_display_t));
    if (!display) {
        DISP_ERROR("Could not allocated display structure\n");
        return ZX_ERR_NO_MEMORY;
    }

    display->parent = parent;
    display->console_visible = true;

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &display->pdev);
    if (status !=  ZX_OK) {
        DISP_ERROR("Could not get parent protocol\n");
        goto fail;
    }

    status = pdev_get_bti(&display->pdev, 0, &display->bti);
    if (status != ZX_OK) {
        DISP_ERROR("Could not get BTI handle\n");
        goto fail;
    }

    // Map all the various MMIOs
    status = pdev_map_mmio_buffer(&display->pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_dc);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO DC\n");
        goto fail;
    }

    device_add_args_t vc_fbuff_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "imx8m-display",
        .ctx = display,
        .ops = &main_device_proto,
        .flags = (DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_INVISIBLE),
    };

    status = device_add(display->parent, &vc_fbuff_args, &display->mydevice);

    thrd_create_with_name(&display->main_thread, main_hdmi_thread, display, "main_hdmi_thread");
    return ZX_OK;

fail:
    DISP_ERROR("bind failed! %d\n", status);
    display_release(display);
    return status;

}

static zx_driver_ops_t imx8m_display_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = imx8m_display_bind,
};

ZIRCON_DRIVER_BEGIN(imx8m_display, imx8m_display_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_NXP),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_IMX8MEVK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_IMX_DISPLAY),
ZIRCON_DRIVER_END(vim_2display)
