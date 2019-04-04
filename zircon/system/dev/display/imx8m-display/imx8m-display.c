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
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/platform-device-lib.h>
#include <hw/reg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/pixelformat.h>
#include <zircon/syscalls.h>

#define PANEL_DISPLAY_ID 1
#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080
#define DISPLAY_FORMAT ZX_PIXEL_FORMAT_RGB_x888
static zx_pixel_format_t supported_pixel_formats[] = { DISPLAY_FORMAT };

typedef struct image_info {
    zx_handle_t pmt;
    zx_paddr_t paddr;
    list_node_t node;
} image_info_t;

static uint32_t imx8m_compute_linear_stride(void* ctx, uint32_t width, zx_pixel_format_t format) {
    // The imx8m display controller needs buffers with a stride that is an even
    // multiple of 32.
    return ROUNDUP(width, 32 / ZX_PIXEL_FORMAT_BYTES(format));
}

static void populate_added_display_args(imx8m_display_t* display, added_display_args_t* args) {
    args->display_id = PANEL_DISPLAY_ID;
    args->edid_present = false;
    args->panel.params.height = DISPLAY_HEIGHT;
    args->panel.params.width = DISPLAY_WIDTH;
    args->panel.params.refresh_rate_e2 = 3000; // Just guess that it's 30fps
    args->pixel_format_list = supported_pixel_formats;
    args->pixel_format_count = countof(supported_pixel_formats);
    args->cursor_info_count = 0;
}

static void imx8m_set_display_controller_interface(void* ctx,
                                                   const display_controller_interface_protocol_t* intf) {
    imx8m_display_t* display = ctx;

    mtx_lock(&display->display_lock);

    bool notify_display = io_buffer_is_valid(&display->fbuffer);
    display->dc_intf = *intf;

    added_display_args_t args;
    populate_added_display_args(display, &args);
    if (notify_display) {
        display_controller_interface_on_displays_changed(&display->dc_intf, &args, 1, NULL, 0,
                                                         NULL, 0, NULL);
    }
    mtx_unlock(&display->display_lock);
}

static zx_status_t imx8m_import_vmo_image(void* ctx, image_t* image,
                                          zx_handle_t vmo, size_t offset) {
    image_info_t* import_info = calloc(1, sizeof(image_info_t));
    if (import_info == NULL) {
        zx_handle_close(vmo);
        return ZX_ERR_NO_MEMORY;
    }

    unsigned pixel_size = ZX_PIXEL_FORMAT_BYTES(image->pixel_format);
    unsigned size = ROUNDUP(image->width * image->height * pixel_size, PAGE_SIZE);
    unsigned num_pages = size / PAGE_SIZE;
    zx_paddr_t paddr[num_pages];

    imx8m_display_t* display = ctx;
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

    import_info->paddr = paddr[0];
    list_add_head(&display->imported_images, &import_info->node);
    image->handle = paddr[0];

    mtx_unlock(&display->image_lock);
    zx_handle_close(vmo);

    return ZX_OK;
fail:
    mtx_unlock(&display->image_lock);

    if (import_info->pmt != ZX_HANDLE_INVALID) {
        zx_pmt_unpin(import_info->pmt);
    }
    zx_handle_close(vmo);
    free(import_info);
    return status;
}

static void imx8m_release_image(void* ctx, image_t* image) {
    imx8m_display_t* display = ctx;
    mtx_lock(&display->image_lock);

    image_info_t* info;
    list_for_every_entry(&display->imported_images, info, image_info_t, node) {
        if (info->paddr == image->handle) {
            list_delete(&info->node);
            break;
        }
    }

    mtx_unlock(&display->image_lock);

    if (info) {
        zx_pmt_unpin(info->pmt);
        free(info);
    }
}

static uint32_t imx8m_check_configuration(void* ctx,
                                          const display_config_t** display_configs,
                                          size_t display_count,
                                          uint32_t** layer_cfg_results,
                                          size_t* layer_cfg_result_count) {
    if (display_count != 1) {
        ZX_DEBUG_ASSERT(display_count == 0);
        return CONFIG_DISPLAY_OK;
    }
    ZX_DEBUG_ASSERT(display_configs[0]->display_id == PANEL_DISPLAY_ID);

    imx8m_display_t* display = ctx;
    mtx_lock(&display->display_lock);

    bool success;
    if (display_configs[0]->layer_count != 1) {
        success = display_configs[0]->layer_count == 0;
    } else {
        primary_layer_t* layer = &display_configs[0]->layer_list[0]->cfg.primary;
        frame_t frame = {
            .x_pos = 0, .y_pos = 0, .width = DISPLAY_WIDTH, .height = DISPLAY_HEIGHT,
        };
        success = display_configs[0]->layer_list[0]->type == LAYER_TYPE_PRIMARY
                && layer->transform_mode == FRAME_TRANSFORM_IDENTITY
                && layer->image.width == DISPLAY_WIDTH
                && layer->image.height == DISPLAY_HEIGHT
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
        layer_cfg_result_count[0] = display_configs[0]->layer_count;
    }
    mtx_unlock(&display->display_lock);
    return CONFIG_DISPLAY_OK;
}

static void imx8m_apply_configuration(void* ctx, const display_config_t** display_configs,
                                      size_t display_count) {
    imx8m_display_t* display = ctx;
    mtx_lock(&display->display_lock);

    zx_paddr_t addr;
    if (display_count == 1 && display_configs[0]->layer_count) {
        addr = (zx_paddr_t) display_configs[0]->layer_list[0]->cfg.primary.image.handle;
    } else {
        addr = 0;
    }

    writel(addr, display->mmio_dc.vaddr +  0x80c0);

    mtx_unlock(&display->display_lock);
}

static zx_status_t allocate_vmo(void* ctx, uint64_t size, zx_handle_t* vmo_out) {
    imx8m_display_t* display = ctx;
    return zx_vmo_create_contiguous(display->bti, size, 0, vmo_out);
}

static display_controller_impl_protocol_ops_t display_controller_ops = {
    .set_display_controller_interface = imx8m_set_display_controller_interface,
    .import_vmo_image = imx8m_import_vmo_image,
    .release_image = imx8m_release_image,
    .check_configuration = imx8m_check_configuration,
    .apply_configuration = imx8m_apply_configuration,
    .compute_linear_stride = imx8m_compute_linear_stride,
    .allocate_vmo = allocate_vmo,
};

static void display_unbind(void* ctx) {
    imx8m_display_t* display = ctx;
    device_remove(display->zxdev);
}

static void display_release(void* ctx) {
    imx8m_display_t* display = ctx;

    if (display) {
        int res;
        thrd_join(display->main_thread, &res);

        mmio_buffer_release(&display->mmio_dc);
        io_buffer_release(&display->fbuffer);
        zx_handle_close(display->bti);
    }
    free(display);
}

static zx_protocol_device_t main_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = display_unbind,
    .release =  display_release,
};

static int main_hdmi_thread(void *arg) {
    imx8m_display_t* display = arg;
    zx_status_t status;

    mtx_lock(&display->display_lock);

    uint32_t stride = imx8m_compute_linear_stride(display, DISPLAY_WIDTH, DISPLAY_FORMAT);
    status = io_buffer_init(&display->fbuffer, display->bti,
                            (stride * DISPLAY_HEIGHT* ZX_PIXEL_FORMAT_BYTES(DISPLAY_FORMAT)),
                            IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        mtx_unlock(&display->display_lock);
        return status;
    }

    writel(io_buffer_phys(&display->fbuffer), display->mmio_dc.vaddr +  0x80c0);

    if (display->dc_intf.ops) {
        added_display_args_t args;
        populate_added_display_args(display, &args);
        display_controller_interface_on_displays_changed(&display->dc_intf, &args, 1, NULL, 0,
                                                         NULL, 0, NULL);
    }
    mtx_unlock(&display->display_lock);

    return ZX_OK;
}

zx_status_t imx8m_display_bind(void* ctx, zx_device_t* parent) {
    imx8m_display_t* display = calloc(1, sizeof(imx8m_display_t));
    if (!display) {
        DISP_ERROR("Could not allocated display structure\n");
        return ZX_ERR_NO_MEMORY;
    }

    display->parent = parent;
    list_initialize(&display->imported_images);
    mtx_init(&display->display_lock, mtx_plain);
    mtx_init(&display->image_lock, mtx_plain);

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &display->pdev);
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

    device_add_args_t dc_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "imx8m-display",
        .ctx = display,
        .ops = &main_device_proto,
        .proto_id = ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL,
        .proto_ops = &display_controller_ops,
    };

    status = device_add(display->parent, &dc_args, &display->zxdev);

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
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_NXP),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_IMX8MMEVK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_IMX_DISPLAY),
ZIRCON_DRIVER_END(vim_2display)
