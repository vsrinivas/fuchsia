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
#include <ddk/protocol/display-controller.h>
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

static const zx_pixel_format_t _gsupported_pixel_formats = { ZX_PIXEL_FORMAT_RGB_565 };

typedef struct image_info {
    zx_handle_t pmt;
    uint8_t canvas_idx;

    list_node_t node;
} image_info_t;

static zx_status_t config_canvas(astro_display_t* display, zx_paddr_t paddr, uint8_t* idx) {
    uint32_t fbh = display->height * 2;
    uint32_t fbw = display->stride * 2;

    // TODO: Find index dynamically
    *idx = OSD2_DMC_CAV_INDEX;

    DISP_INFO("Canvas Diminsions: w=%d h=%d\n", fbw, fbh);

    // set framebuffer address in DMC, read/modify/write
    WRITE32_DMC_REG(DMC_CAV_LUT_DATAL,
        (((paddr + 7) >> 3) & DMC_CAV_ADDR_LMASK) |
             ((((fbw + 7) >> 3) & DMC_CAV_WIDTH_LMASK) << DMC_CAV_WIDTH_LBIT));

    WRITE32_DMC_REG(DMC_CAV_LUT_DATAH,
        ((((fbw + 7) >> 3) >> DMC_CAV_WIDTH_LWID) << DMC_CAV_WIDTH_HBIT) |
             ((fbh & DMC_CAV_HEIGHT_MASK) << DMC_CAV_HEIGHT_BIT));

    WRITE32_DMC_REG(DMC_CAV_LUT_ADDR, DMC_CAV_LUT_ADDR_WR_EN | OSD2_DMC_CAV_INDEX );
    // read a cbus to make sure last write finish.
    READ32_DMC_REG(DMC_CAV_LUT_DATAH);

    return ZX_OK;

}

static void astro_set_display_controller_cb(void* ctx, void* cb_ctx, display_controller_cb_t* cb) {
    astro_display_t* display = ctx;
    mtx_lock(&display->cb_lock);

    mtx_lock(&display->display_lock);

    display->dc_cb = cb;
    display->dc_cb_ctx = cb_ctx;

    mtx_unlock(&display->display_lock);

    uint64_t display_id = PANEL_DISPLAY_ID;
    display->dc_cb->on_displays_changed(display->dc_cb_ctx, &display_id, 1, NULL, 0);
    mtx_unlock(&display->cb_lock);
}

static zx_status_t astro_get_display_info(void* ctx, uint64_t display_id, display_info_t* info) {
    ZX_DEBUG_ASSERT(display_id == PANEL_DISPLAY_ID);

    astro_display_t* display = ctx;
    mtx_lock(&display->display_lock);

    info->edid_present = false;
    info->panel.params.height = display->height;
    info->panel.params.width = display->width;
    info->panel.params.refresh_rate_e2 = 3000; // Just guess that it's 30fps
    info->pixel_formats = &_gsupported_pixel_formats;
    info->pixel_format_count = sizeof(_gsupported_pixel_formats) / sizeof(zx_pixel_format_t);

    mtx_unlock(&display->display_lock);
    return ZX_OK;
}

static zx_status_t astro_import_vmo_image(void* ctx, image_t* image, zx_handle_t vmo, size_t offset) {
    image_info_t* import_info = calloc(1, sizeof(image_info_t));
    if (import_info == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    unsigned pixel_size = ZX_PIXEL_FORMAT_BYTES(image->pixel_format);
    unsigned size = ROUNDUP(image->width * image->height * pixel_size, PAGE_SIZE);
    unsigned num_pages = size / PAGE_SIZE;
    zx_paddr_t paddr[num_pages];

    astro_display_t* display = ctx;
    mtx_lock(&display->image_lock);

    zx_status_t status = zx_bti_pin(display->bti, ZX_BTI_PERM_READ, vmo, offset, size,
                                    paddr, num_pages, &import_info->pmt);
    if (status != ZX_OK) {
        goto fail;
    }

    for (unsigned i = 0; i < num_pages - 1; i++) {
        if (paddr[i] + PAGE_SIZE != paddr[i + 1]) {
            status = ZX_ERR_INVALID_ARGS;
            goto fail;
        }
    }

    if (config_canvas(display, paddr[0], &import_info->canvas_idx) != ZX_OK) {
        status = ZX_ERR_NO_RESOURCES;
        goto fail;
    }

    list_add_head(&display->imported_images, &import_info->node);
    image->handle = (void*) (uint64_t) import_info->canvas_idx;

    mtx_unlock(&display->image_lock);

    return ZX_OK;
fail:
    mtx_unlock(&display->image_lock);

    if (import_info->pmt != ZX_HANDLE_INVALID) {
        zx_handle_close(import_info->pmt);
    }
    free(import_info);
    return status;
}

static void astro_release_image(void* ctx, image_t* image) {
    astro_display_t* display = ctx;
    mtx_lock(&display->image_lock);

    image_info_t* info;
    list_for_every_entry(&display->imported_images, info, image_info_t, node) {
        if ((void*) (uint64_t) info->canvas_idx == image->handle) {
            list_delete(&info->node);
            break;
        }
    }

    mtx_unlock(&display->image_lock);

    if (info) {
        // free_canvas_entry(display, info->canvas_idx);
        zx_handle_close(info->pmt);
        free(info);
    }
}

static void astro_check_configuration(void* ctx,
                                      const display_config_t** display_configs,
                                      uint32_t** layer_cfg_results,
                                      uint32_t display_count) {
    if (display_count != 1) {
        ZX_DEBUG_ASSERT(display_count == 0);
        return;
    }
    ZX_DEBUG_ASSERT(display_configs[0]->display_id == PANEL_DISPLAY_ID);

    astro_display_t* display = ctx;
    mtx_lock(&display->display_lock);

    bool success;
    if (display_configs[0]->layer_count != 1) {
        success = display_configs[0]->layer_count == 0;
    } else {
        primary_layer_t* layer = &display_configs[0]->layers[0]->cfg.primary;
        frame_t frame = {
            .x_pos = 0, .y_pos = 0, .width = display->width, .height = display->height,
        };
        success = display_configs[0]->layers[0]->type == LAYER_PRIMARY
                && layer->transform_mode == FRAME_TRANSFORM_IDENTITY
                && layer->image.width == display->width
                && layer->image.height == display->height
                && memcmp(&layer->dest_frame, &frame, sizeof(frame_t)) == 0
                && memcmp(&layer->src_frame, &frame, sizeof(frame_t)) == 0
                && display_configs[0]->cc_flags == 0
                && layer->alpha_mode == ALPHA_DISABLE;
    }
    if (!success) {
        layer_cfg_results[0][0] = CLIENT_MERGE_BASE;
        for (unsigned i = 1; i < display_configs[0]->layer_count; i++) {
            layer_cfg_results[0][i] = CLIENT_MERGE_SRC;
        }
    }
    mtx_unlock(&display->display_lock);
}

static void astro_apply_configuration(void* ctx,
                                      const display_config_t** display_configs,
                                      uint32_t display_count) {
    // TODO: Nothing to do for now
}

static uint32_t astro_compute_linear_stride(void* ctx, uint32_t width, zx_pixel_format_t format) {
    // The astro display controller needs buffers with a stride that is an even
    // multiple of 32.
    return ROUNDUP(width, 32 / ZX_PIXEL_FORMAT_BYTES(format));
}

static zx_status_t allocate_vmo(void* ctx, uint64_t size, zx_handle_t* vmo_out) {
    astro_display_t* display = ctx;
    return zx_vmo_create_contiguous(display->bti, size, 0, vmo_out);
}

static display_controller_protocol_ops_t display_controller_ops = {
    .set_display_controller_cb = astro_set_display_controller_cb,
    .get_display_info = astro_get_display_info,
    .import_vmo_image = astro_import_vmo_image,
    .release_image = astro_release_image,
    .check_configuration = astro_check_configuration,
    .apply_configuration = astro_apply_configuration,
    .compute_linear_stride = astro_compute_linear_stride,
    .allocate_vmo = allocate_vmo,
};

static void display_release(void* ctx) {
    astro_display_t* display = ctx;

    if (display) {
        zx_interrupt_destroy(display->vsync_interrupt);
        int res;
        thrd_join(display->vsync_thread, &res);
        io_buffer_release(&display->mmio_dmc);
        io_buffer_release(&display->fbuffer);
        zx_handle_close(display->bti);
        zx_handle_close(display->vsync_interrupt);
    }
    free(display);
}

static zx_protocol_device_t main_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release =  display_release,
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

static zx_status_t setup_display_if(astro_display_t* display) {
    zx_status_t status;

    mtx_lock(&display->cb_lock);
    mtx_lock(&display->display_lock);

    // allocate frame buffer
    display->format = ZX_PIXEL_FORMAT_RGB_565;
    display->width  = 608;
    display->height = 1024;
    display->stride = astro_compute_linear_stride(
            display, display->width, display->format);

    status = io_buffer_init(&display->fbuffer, display->bti,
                            (display->stride * display->height *
                             ZX_PIXEL_FORMAT_BYTES(display->format)),
                            IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        goto fail;
    }

    config_canvas(display, io_buffer_phys(&display->fbuffer), &display->fb_canvas_idx);
    init_backlight(display);

    zx_framebuffer_set_range(get_root_resource(), display->fbuffer.vmo_handle,
                             display->fbuffer.size, display->disp_info.format,
                             display->disp_info.width, display->disp_info.height,
                             display->disp_info.stride);


    mtx_unlock(&display->display_lock);

    if (display->dc_cb) {
        uint64_t display_added = PANEL_DISPLAY_ID;
        display->dc_cb->on_displays_changed(display->dc_cb_ctx, &display_added, 1, NULL, 0);
    }
    mtx_unlock(&display->cb_lock);

    return ZX_OK;

fail:
    mtx_unlock(&display->display_lock);
    mtx_unlock(&display->cb_lock);
    return status;
}

static int main_astro_display_thread(void *arg) {
    astro_display_t* display = arg;
    setup_display_if(display);
    return ZX_OK;
}

static zx_status_t vsync_thread(void *arg) {
    zx_status_t status = ZX_OK;
    astro_display_t* display = arg;

    while(1) {
        status = zx_interrupt_wait(display->vsync_interrupt, NULL);
        if (status != ZX_OK) {
            DISP_ERROR("VSync Interrupt Wait failed\n");
            break;
        }

        mtx_lock(&display->cb_lock);
        mtx_lock(&display->display_lock);

        void* live = (void*)(uint64_t) display->current_image;
        uint8_t is_client_handle = display->current_image != display->fb_canvas_idx;
        mtx_unlock(&display->display_lock);

        if (display->dc_cb) {
            display->dc_cb->on_display_vsync(display->dc_cb_ctx, PANEL_DISPLAY_ID, &live,
                                                is_client_handle);
        }
        mtx_unlock(&display->cb_lock);
    }

    return status;
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

    // Map VSync Interrupt
    status = pdev_map_interrupt(&display->pdev, 0, &display->vsync_interrupt);
    if (status  != ZX_OK) {
        DISP_ERROR("Could not map vsync interrupt\n");
        goto fail;
    }

    device_add_args_t add_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "astro-display",
        .ctx = display,
        .ops = &main_device_proto,
        .proto_id = ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL,
        .proto_ops = &display_controller_ops,
    };

    status = device_add(display->parent, &add_args, &display->mydevice);
    if (status != ZX_OK) {
        DISP_ERROR("Could not add device\n");
        goto fail;
    }

    list_initialize(&display->imported_images);
    mtx_init(&display->display_lock, mtx_plain);
    mtx_init(&display->image_lock, mtx_plain);
    mtx_init(&display->cb_lock, mtx_plain);

    thrd_create_with_name(&display->main_thread, main_astro_display_thread, display,
                                                    "main_astro_display_thread");
    thrd_create_with_name(&display->vsync_thread, vsync_thread, display, "vsync_thread");
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
