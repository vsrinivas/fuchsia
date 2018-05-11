// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "astro-display.h"
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
    astro_display_t* display = ctx;
    memcpy(info, &display->disp_info, sizeof(zx_display_info_t));
    return ZX_OK;
}

static zx_status_t vc_get_framebuffer(void* ctx, void** framebuffer) {
    if (!framebuffer) return ZX_ERR_INVALID_ARGS;
    astro_display_t* display = ctx;
    *framebuffer = io_buffer_virt(&display->fbuffer);
    return ZX_OK;
}

static void flush_framebuffer(astro_display_t* display) {
    io_buffer_cache_flush(&display->fbuffer, 0,
        (display->disp_info.stride * display->disp_info.height *
            ZX_PIXEL_FORMAT_BYTES(display->disp_info.format)));
}

static void vc_flush_framebuffer(void* ctx) {
    flush_framebuffer(ctx);
}

static void vc_display_set_ownership_change_callback(void* ctx, zx_display_cb_t callback,
                                                     void* cookie) {
    astro_display_t* display = ctx;
    display->ownership_change_callback = callback;
    display->ownership_change_cookie = cookie;
}

static void vc_display_acquire_or_release_display(void* ctx, bool acquire) {
    astro_display_t* display = ctx;

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
    astro_display_t* display = ctx;

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
    astro_display_t* display;
    zx_device_t* device;
};

static zx_status_t display_client_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                        void* out_buf, size_t out_len, size_t* out_actual) {
    struct display_client_device* client_struct = ctx;
    astro_display_t* display = client_struct->display;
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
    astro_display_t* display = client_struct->display;
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
        .name = "astro-display",
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

/* Table from Linux source */
/* TODO: Need to separate backlight driver from display driver */
static const uint8_t backlight_init_table[] = {
    0xa2, 0x20,
    0xa5, 0x54,
    0x00, 0xff,
    0x01, 0x05,
    0xa2, 0x20,
    0xa5, 0x54,
    0xa1, 0xb7,
    0xa0, 0xff,
    0x00, 0x80,
};

static void init_backlight(astro_display_t* display) {

    // power on backlight
    gpio_config(&display->gpio, 0, GPIO_DIR_OUT);
    gpio_write(&display->gpio, 0, 1);
    usleep(1000);

    for (size_t i = 0; i < sizeof(backlight_init_table); i+=2) {
        if(i2c_transact_sync(&display->i2c, 0, &backlight_init_table[i], 2, NULL, 0) != ZX_OK) {
            DISP_ERROR("Backlight write failed: reg[0x%x]: 0x%x\n", backlight_init_table[i],
                                            backlight_init_table[i+1]);
        }
    }
}

static void config_canvas(astro_display_t* display) {
    uint32_t fbh = display->disp_info.height * 2;
    uint32_t fbw = display->disp_info.stride * 2;

    DISP_INFO("Canvas Diminsions: w=%d h=%d\n", fbw, fbh);

    // set framebuffer address in DMC, read/modify/write
    WRITE32_DMC_REG(DMC_CAV_LUT_DATAL,
        (((io_buffer_phys(&display->fbuffer) + 7) >> 3) & DMC_CAV_ADDR_LMASK) |
             ((((fbw + 7) >> 3) & DMC_CAV_WIDTH_LMASK) << DMC_CAV_WIDTH_LBIT));

    WRITE32_DMC_REG(DMC_CAV_LUT_DATAH,
        ((((fbw + 7) >> 3) >> DMC_CAV_WIDTH_LWID) << DMC_CAV_WIDTH_HBIT) |
             ((fbh & DMC_CAV_HEIGHT_MASK) << DMC_CAV_HEIGHT_BIT));

    WRITE32_DMC_REG(DMC_CAV_LUT_ADDR, DMC_CAV_LUT_ADDR_WR_EN | OSD2_DMC_CAV_INDEX );
    // read a cbus to make sure last write finish.
    READ32_DMC_REG(DMC_CAV_LUT_DATAH);

}

static zx_status_t setup_display_if(astro_display_t* display) {
    zx_status_t status;

    // allocate frame buffer
    display->disp_info.format = ZX_PIXEL_FORMAT_RGB_565;
    display->disp_info.width  = 608;
    display->disp_info.height = 1024;
    display->disp_info.pixelsize = ZX_PIXEL_FORMAT_BYTES(display->disp_info.format);
    // The astro display controller needs buffers with a stride that is an even
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

    config_canvas(display);
    init_backlight(display);

    zx_set_framebuffer(get_root_resource(), io_buffer_virt(&display->fbuffer),
                       display->fbuffer.size, display->disp_info.format,
                       display->disp_info.width, display->disp_info.height,
                       display->disp_info.stride);

    device_add_args_t vc_fbuff_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "astro-display",
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

static int main_astro_display_thread(void *arg) {
    astro_display_t* display = arg;
    setup_display_if(display);
    return ZX_OK;
}

zx_status_t astro_display_bind(void* ctx, zx_device_t* parent) {
    astro_display_t* display = calloc(1, sizeof(astro_display_t));
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

    // Obtain I2C Protocol
    status = device_get_protocol(parent, ZX_PROTOCOL_I2C, &display->i2c);
    if (status != ZX_OK) {
        DISP_ERROR("Could not obtain I2C protocol\n");
        goto fail;
    }

    // Obtain GPIO Protocol
    status = device_get_protocol(parent, ZX_PROTOCOL_GPIO, &display->gpio);
    if (status != ZX_OK) {
        DISP_ERROR("Could not obtain GPIO protocol\n");
        goto fail;
    }

    status = pdev_get_bti(&display->pdev, 0, &display->bti);
    if (status != ZX_OK) {
        DISP_ERROR("Could not get BTI handle\n");
        goto fail;
    }

    // Map all the various MMIOs
    status = pdev_map_mmio_buffer(&display->pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_dmc);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO DC\n");
        goto fail;
    }

    device_add_args_t vc_fbuff_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "astro-display",
        .ctx = display,
        .ops = &main_device_proto,
        .flags = (DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_INVISIBLE),
    };

    status = device_add(display->parent, &vc_fbuff_args, &display->mydevice);

    thrd_create_with_name(&display->main_thread, main_astro_display_thread, display,
                                                    "main_astro_display_thread");
    return ZX_OK;

fail:
    DISP_ERROR("bind failed! %d\n", status);
    display_release(display);
    return status;

}

static zx_driver_ops_t astro_display_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = astro_display_bind,
};

ZIRCON_DRIVER_BEGIN(astro_display, astro_display_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_DISPLAY),
ZIRCON_DRIVER_END(astro_display)
