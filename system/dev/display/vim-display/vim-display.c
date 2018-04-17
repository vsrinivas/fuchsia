// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vim-display.h"
#include "hdmitx.h"
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

/* Default formats */
static const uint8_t _ginput_color_format   = HDMI_COLOR_FORMAT_444;
static const uint8_t _gcolor_depth          = HDMI_COLOR_DEPTH_24B;

// MMIO indices (based on vim2_display_mmios)
enum {
    MMIO_PRESET,
    MMIO_HDMITX,
    MMIO_HIU,
    MMIO_VPU,
    MMIO_HDMTX_SEC,
    MMIO_DMC,
    MMIO_CBUS,
};


static zx_status_t vc_set_mode(void* ctx, zx_display_info_t* info) {
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
    *framebuffer = io_buffer_virt(&display->fbuffer);
    return ZX_OK;
}

static void flush_framebuffer(vim2_display_t* display) {
    io_buffer_cache_flush(&display->fbuffer, 0,
        (display->disp_info.stride * display->disp_info.height *
            ZX_PIXEL_FORMAT_BYTES(display->disp_info.format)));
}

static void vc_flush_framebuffer(void* ctx) {
    flush_framebuffer(ctx);
}

static void vc_display_set_ownership_change_callback(void* ctx, zx_display_cb_t callback,
                                                     void* cookie) {
    vim2_display_t* display = ctx;
    display->ownership_change_callback = callback;
    display->ownership_change_cookie = cookie;
}

static void vc_display_acquire_or_release_display(void* ctx, bool acquire) {
    vim2_display_t* display = ctx;

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
    vim2_display_t* display = ctx;

    gpio_release_interrupt(&display->gpio, 0);
    zx_handle_close(display->inth);
    if (display) {
        io_buffer_release(&display->mmio_preset);
        io_buffer_release(&display->mmio_hdmitx);
        io_buffer_release(&display->mmio_hiu);
        io_buffer_release(&display->mmio_vpu);
        io_buffer_release(&display->mmio_hdmitx_sec);
        io_buffer_release(&display->mmio_dmc);
        io_buffer_release(&display->mmio_cbus);
        io_buffer_release(&display->fbuffer);
        zx_handle_close(display->bti);
        free(display->edid_buf);
        free(display->p);
    }
    free(display);
}

static zx_protocol_device_t main_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release =  display_release,
};

struct display_client_device {
    vim2_display_t* display;
    zx_device_t* device;
};

static zx_status_t display_client_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                        void* out_buf, size_t out_len, size_t* out_actual) {
    struct display_client_device* client_struct = ctx;
    vim2_display_t* display = client_struct->display;
    switch (op) {
    case IOCTL_DISPLAY_GET_FB: {
        if (out_len < sizeof(ioctl_display_get_fb_t))
            return ZX_ERR_INVALID_ARGS;
        ioctl_display_get_fb_t* description = (ioctl_display_get_fb_t*)(out_buf);
        zx_status_t status = zx_handle_duplicate(display->fbuffer.vmo_handle, ZX_RIGHT_SAME_RIGHTS, &description->vmo);
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
    vim2_display_t* display = client_struct->display;
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
        .name = "vim2-display",
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

static zx_status_t setup_hdmi(vim2_display_t* display)
{
    zx_status_t status;
    // initialize HDMI
    status = init_hdmi_hardware(display);
    if (status != ZX_OK) {
        DISP_ERROR("HDMI hardware initialization failed\n");
        return status;
    }

    status = get_preferred_res(display, EDID_BUF_SIZE);
    if (status != ZX_OK) {
        DISP_ERROR("No display connected!\n");
        return status;

    }

    // allocate frame buffer
    display->disp_info.format = ZX_PIXEL_FORMAT_RGB_x888;
    display->disp_info.width  = display->p->timings.hactive;
    display->disp_info.height = display->p->timings.vactive;
    display->disp_info.stride = display->p->timings.hactive;
    display->disp_info.pixelsize = ZX_PIXEL_FORMAT_BYTES(display->disp_info.format);

    status = io_buffer_init(&display->fbuffer, display->bti,
                            (display->disp_info.stride * display->disp_info.height *
                             ZX_PIXEL_FORMAT_BYTES(display->disp_info.format)),
                            IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        return status;
    }


    display->input_color_format = _ginput_color_format;
    display->color_depth = _gcolor_depth;


    status = init_hdmi_interface(display, display->p);
    if (status != ZX_OK) {
        DISP_ERROR("HDMI interface initialization failed\n");
        return status;
    }

    /* Configure Canvas memory */
    configure_canvas(display);

    /* OSD2 setup */
    configure_osd2(display);

    zx_set_framebuffer(get_root_resource(), io_buffer_virt(&display->fbuffer),
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

    status = device_add(display->mydevice, &vc_fbuff_args, &display->fbdevice);
    if (status != ZX_OK) {
        free(display);
        return status;
    }
    return ZX_OK;
}

static int hdmi_irq_handler(void *arg) {
    vim2_display_t* display = arg;
    zx_status_t status;
    while(1) {
#if ENABLE_NEW_IRQ_API
        status = zx_irq_wait(display->inth, NULL);
#else
        uint64_t slots;
        status = zx_interrupt_wait(display->inth, &slots);
#endif
        if (status != ZX_OK) {
            DISP_ERROR("Waiting in Interrupt failed %d\n", status);
            return -1;
        }
        usleep(500000);
        uint8_t hpd;
        status = gpio_read(&display->gpio, 0, &hpd);
        if (status != ZX_OK) {
            DISP_ERROR("gpio_read failed HDMI HPD\n");
        }
        if(hpd & !display->hdmi_inited) {
            if (setup_hdmi(display) == ZX_OK) {
                display->hdmi_inited = true;
                DISP_ERROR("Display is connected\n");
                gpio_set_polarity(&display->gpio, 0, GPIO_POLARITY_LOW);
            }
        } else if (display->hdmi_inited) {
            DISP_ERROR("Display Disconnected!\n");
            hdmi_shutdown(display);
            io_buffer_release(&display->fbuffer);
            device_remove(display->fbdevice);
            display->hdmi_inited = false;
            gpio_set_polarity(&display->gpio, 0, GPIO_POLARITY_HIGH);
        }
    }
    return 0;
}

static int main_hdmi_thread(void *arg)
{
    vim2_display_t* display = arg;
    zx_status_t status;
    status = gpio_config(&display->gpio, 0, GPIO_DIR_IN | GPIO_PULL_DOWN);
    if (status!= ZX_OK) {
        DISP_ERROR("gpio_config failed for gpio\n");
        return status;
    }

    status = gpio_get_interrupt(&display->gpio, 0,
                       ZX_INTERRUPT_MODE_LEVEL_HIGH,
                       &display->inth);
    if (status != ZX_OK) {
        DISP_ERROR("gpio_get_interrupt failed for gpio\n");
        return status;
    }

    thrd_create_with_name(&display->main_thread, hdmi_irq_handler, display, "hdmi_irq_handler thread");
    return ZX_OK;
}

zx_status_t vim2_display_bind(void* ctx, zx_device_t* parent) {
    vim2_display_t* display = calloc(1, sizeof(vim2_display_t));
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

    status = device_get_protocol(parent, ZX_PROTOCOL_GPIO, &display->gpio);
    if (status != ZX_OK) {
        DISP_ERROR("Could not get Display GPIO protocol\n");
        goto fail;
    }

    // Map all the various MMIOs
    status = pdev_map_mmio_buffer(&display->pdev, MMIO_PRESET, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_preset);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO PRESET\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_HDMITX, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_hdmitx);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO HDMITX\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_HIU, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_hiu);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO HIU\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_VPU, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_vpu);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO VPU\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_HDMTX_SEC, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_hdmitx_sec);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO HDMITX SEC\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_DMC, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_dmc);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO DMC\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_CBUS, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_cbus);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO CBUS\n");
        goto fail;
    }

    // Create EDID Buffer
    display->edid_buf = calloc(1, EDID_BUF_SIZE);
    if (!display->edid_buf) {
        DISP_ERROR("Could not allocated EDID BUf of size %d\n", EDID_BUF_SIZE);
        goto fail;
    }

    display->p = calloc(1, sizeof(struct hdmi_param));
    if (!display->p) {
        DISP_ERROR("Could not allocated hdmi param structure\n");
        goto fail;
    }

    device_add_args_t vc_fbuff_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "vim2-display",
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
