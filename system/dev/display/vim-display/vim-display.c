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
#include <zircon/syscalls.h>

/* Default formats */
static const uint8_t _ginput_color_format   = HDMI_COLOR_FORMAT_444;
static const uint8_t _gcolor_depth          = HDMI_COLOR_DEPTH_24B;

static const zx_pixel_format_t _gsupported_pixel_formats = { ZX_PIXEL_FORMAT_RGB_x888 };

typedef struct image_info {
    zx_handle_t pmt;
    uint8_t canvas_idx;

    list_node_t node;
} image_info_t;

// MMIO indices (based on vim2_display_mmios)
enum {
    MMIO_PRESET = 0,
    MMIO_HDMITX,
    MMIO_HIU,
    MMIO_VPU,
    MMIO_HDMTX_SEC,
    MMIO_DMC,
    MMIO_CBUS,
    MMIO_COUNT  // Must be the final entry
};

static uint32_t vim_compute_linear_stride(void* ctx, uint32_t width, zx_pixel_format_t format) {
    // The vim2 display controller needs buffers with a stride that is an even
    // multiple of 32.
    return ROUNDUP(width, 32 / ZX_PIXEL_FORMAT_BYTES(format));
}

static void vim_set_display_controller_cb(void* ctx, void* cb_ctx, display_controller_cb_t* cb) {
    vim2_display_t* display = ctx;
    mtx_lock(&display->cb_lock);

    mtx_lock(&display->display_lock);

    display->dc_cb = cb;
    display->dc_cb_ctx = cb_ctx;

    uint64_t display_id = display->display_id;
    bool attached = display->display_attached;
    mtx_unlock(&display->display_lock);

    if (attached) {
        display->dc_cb->on_displays_changed(display->dc_cb_ctx, &display_id, 1, NULL, 0);
    }
    mtx_unlock(&display->cb_lock);
}

static zx_status_t vim_get_display_info(void* ctx, uint64_t display_id, display_info_t* info) {
    vim2_display_t* display = ctx;
    mtx_lock(&display->display_lock);
    if (!display->display_attached || display_id != display->display_id) {
        mtx_unlock(&display->display_lock);
        return ZX_ERR_NOT_FOUND;
    }

    info->edid_present = true;
    info->panel.edid.data = display->edid_buf;
    info->panel.edid.length = EDID_BUF_SIZE;
    info->pixel_formats = &_gsupported_pixel_formats;
    info->pixel_format_count = sizeof(_gsupported_pixel_formats) / sizeof(zx_pixel_format_t);

    mtx_unlock(&display->display_lock);
    return ZX_OK;
}

static zx_status_t vim_import_vmo_image(void* ctx, image_t* image, zx_handle_t vmo, size_t offset) {
    image_info_t* import_info = calloc(1, sizeof(image_info_t));
    if (import_info == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    vim2_display_t* display = ctx;
    zx_status_t status = ZX_OK;
    mtx_lock(&display->image_lock);

    if (image->type != IMAGE_TYPE_SIMPLE || image->pixel_format != display->format) {
        status = ZX_ERR_INVALID_ARGS;
        goto fail;
    }

    uint32_t stride = vim_compute_linear_stride(display, image->width, image->pixel_format);

    canvas_info_t info;
    info.height         = image->height;
    info.stride_bytes   = stride * ZX_PIXEL_FORMAT_BYTES(image->pixel_format);
    info.wrap           = 0;
    info.blkmode        = 0;
    info.endianness     = 0;

    zx_handle_t dup_vmo;
    status = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
    if (status != ZX_OK) {
        goto fail;
    }

    status = canvas_config(&display->canvas, dup_vmo, offset,
                           &info, &import_info->canvas_idx);
    if (status != ZX_OK) {
        status = ZX_ERR_NO_RESOURCES;
        goto fail;
    }

    list_add_head(&display->imported_images, &import_info->node);
    image->handle = (void*) (uint64_t) import_info->canvas_idx;

    mtx_unlock(&display->image_lock);

    return ZX_OK;
fail:
    mtx_unlock(&display->image_lock);
    free(import_info);
    return status;
}

static void vim_release_image(void* ctx, image_t* image) {
    vim2_display_t* display = ctx;
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
        canvas_free(&display->canvas, info->canvas_idx);
        free(info);
    }
}

static void vim_check_configuration(void* ctx,
                                    const display_config_t** display_configs,
                                    uint32_t* display_cfg_result,
                                    uint32_t** layer_cfg_results,
                                    uint32_t display_count) {
    *display_cfg_result = CONFIG_DISPLAY_OK;
    if (display_count != 1) {
        if (display_count > 1) {
            // The core display driver should never see a configuration with more
            // than 1 display, so this is a bug in the core driver.
            ZX_DEBUG_ASSERT(false);
            *display_cfg_result = CONFIG_DISPLAY_TOO_MANY;
        }
        return;
    }
    vim2_display_t* display = ctx;
    mtx_lock(&display->display_lock);

    // no-op, just wait for the client to try a new config
    if (!display->display_attached || display_configs[0]->display_id != display->display_id) {
        mtx_unlock(&display->display_lock);
        return;
    }

    // TODO: Add support for modesetting
    if (display_configs[0]->mode.h_addressable != display->width
            || display_configs[0]->mode.v_addressable != display->height) {
        mtx_unlock(&display->display_lock);
        *display_cfg_result = CONFIG_DISPLAY_UNSUPPORTED_MODES;
        return;
    }

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

static void vim_apply_configuration(void* ctx,
                                    const display_config_t** display_configs,
                                    uint32_t display_count) {
    vim2_display_t* display = ctx;
    mtx_lock(&display->display_lock);

    uint8_t addr;
    if (display_count == 1 && display_configs[0]->layer_count) {
        // The only way a checked configuration could now be invalid is if display was
        // unplugged. If that's the case, then the upper layers will give a new configuration
        // once they finish handling the unplug event. So just return.
        if (!display->display_attached || display_configs[0]->display_id != display->display_id) {
            mtx_unlock(&display->display_lock);
            return;
        }
        addr = (uint8_t) (uint64_t) display_configs[0]->layers[0]->cfg.primary.image.handle;
    } else {
        addr = display->fb_canvas_idx;
    }

    flip_osd2(display, addr);

    mtx_unlock(&display->display_lock);
}

static zx_status_t allocate_vmo(void* ctx, uint64_t size, zx_handle_t* vmo_out) {
    vim2_display_t* display = ctx;
    return zx_vmo_create_contiguous(display->bti, size, 0, vmo_out);
}

static display_controller_protocol_ops_t display_controller_ops = {
    .set_display_controller_cb = vim_set_display_controller_cb,
    .get_display_info = vim_get_display_info,
    .import_vmo_image = vim_import_vmo_image,
    .release_image = vim_release_image,
    .check_configuration = vim_check_configuration,
    .apply_configuration = vim_apply_configuration,
    .compute_linear_stride = vim_compute_linear_stride,
    .allocate_vmo = allocate_vmo,
};

static void display_release(void* ctx) {
    vim2_display_t* display = ctx;

    if (display) {
        bool wait_for_vsync_shutdown = false;
        if (display->vsync_interrupt != ZX_HANDLE_INVALID) {
            zx_interrupt_trigger(display->vsync_interrupt, 0, 0);
            wait_for_vsync_shutdown = true;
        }

        bool wait_for_main_shutdown = false;
        if (display->inth != ZX_HANDLE_INVALID) {
            zx_interrupt_trigger(display->inth, 0, 0);
            wait_for_main_shutdown = true;
        }

        int res;
        if (wait_for_vsync_shutdown) {
            thrd_join(display->vsync_thread, &res);
        }

        if (wait_for_main_shutdown) {
            thrd_join(display->main_thread, &res);
        }

        gpio_release_interrupt(&display->gpio, 0);
        io_buffer_release(&display->mmio_preset);
        io_buffer_release(&display->mmio_hdmitx);
        io_buffer_release(&display->mmio_hiu);
        io_buffer_release(&display->mmio_vpu);
        io_buffer_release(&display->mmio_hdmitx_sec);
        io_buffer_release(&display->mmio_cbus);
        zx_handle_close(display->fb_vmo);
        zx_handle_close(display->bti);
        zx_handle_close(display->vsync_interrupt);
        zx_handle_close(display->inth);
        free(display->edid_buf);
        free(display->p);
    }
    free(display);
}

static void display_unbind(void* ctx) {
    vim2_display_t* display = ctx;
    device_remove(display->mydevice);
}

static zx_protocol_device_t main_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release =  display_release,
    .unbind = display_unbind,
};

static zx_status_t setup_hdmi(vim2_display_t* display)
{
    zx_status_t status;
    size_t size;
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
    display->format = ZX_PIXEL_FORMAT_RGB_x888;
    display->width  = display->p->timings.hactive;
    display->height = display->p->timings.vactive;
    display->stride = vim_compute_linear_stride(
                      display, display->p->timings.hactive, display->format);
    display->input_color_format = _ginput_color_format;
    display->color_depth = _gcolor_depth;

    size = display->stride * display->height * ZX_PIXEL_FORMAT_BYTES(display->format);
    status = allocate_vmo(display, size, &display->fb_vmo);
    if (status != ZX_OK) {
        return status;
    }

    // Create a duplicate handle
    zx_handle_t fb_vmo_dup_handle;
    status = zx_handle_duplicate(display->fb_vmo, ZX_RIGHT_SAME_RIGHTS, &fb_vmo_dup_handle);
    if (status != ZX_OK) {
        DISP_ERROR("Unable to duplicate FB VMO handle\n");
        zx_handle_close(display->fb_vmo);
        return status;
    }

    zx_vaddr_t virt;
    status = zx_vmar_map(zx_vmar_root_self(), 0, display->fb_vmo, 0,
                         size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &virt);
    if (status != ZX_OK) {
        DISP_ERROR("zx_vmar_map failed %d size: %zu\n", status, size);
        zx_handle_close(display->fb_vmo);
        return status;
    }

    status = init_hdmi_interface(display, display->p);
    if (status != ZX_OK) {
        DISP_ERROR("HDMI interface initialization failed\n");
        return status;
    }

    /* Configure Canvas memory */
    canvas_info_t info;
    info.height         = display->height;
    info.stride_bytes   = display->stride * ZX_PIXEL_FORMAT_BYTES(display->format);
    info.wrap           = 0;
    info.blkmode        = 0;
    info.endianness     = 0;

    status = canvas_config(&display->canvas, fb_vmo_dup_handle,
                           0, &info, &display->fb_canvas_idx);
    if (status != ZX_OK) {
        DISP_ERROR("Unable to configure canvas %d\n", status);
        return status;
    }

    /* OSD2 setup */
    configure_osd2(display, display->fb_canvas_idx);

    zx_framebuffer_set_range(get_root_resource(), display->fb_vmo,
                             size, display->format,
                             display->width, display->height, display->stride);

    return ZX_OK;
}

static int hdmi_irq_handler(void *arg) {
    vim2_display_t* display = arg;
    zx_status_t status;
    while(1) {
        status = zx_interrupt_wait(display->inth, NULL);
        if (status != ZX_OK) {
            DISP_ERROR("Waiting in Interrupt failed %d\n", status);
            return -1;
        }
        usleep(500000);
        uint8_t hpd;
        status = gpio_read(&display->gpio, 0, &hpd);
        if (status != ZX_OK) {
            DISP_ERROR("gpio_read failed HDMI HPD\n");
            continue;
        }

        mtx_lock(&display->cb_lock);
        mtx_lock(&display->display_lock);

        uint64_t display_added = INVALID_DISPLAY_ID;
        uint64_t display_removed = INVALID_DISPLAY_ID;
        if (hpd && !display->display_attached) {
            DISP_ERROR("Display is connected\n");
            if (setup_hdmi(display) == ZX_OK) {
                display->display_attached = true;
                display_added = display->display_id;
                gpio_set_polarity(&display->gpio, 0, GPIO_POLARITY_LOW);
            }
        } else if (!hpd && display->display_attached) {
            DISP_ERROR("Display Disconnected!\n");
            hdmi_shutdown(display);
            canvas_free(&display->canvas, display->fb_canvas_idx);
            zx_handle_close(display->fb_vmo);

            display_removed = display->display_id;
            display->display_id++;
            display->display_attached = false;

            gpio_set_polarity(&display->gpio, 0, GPIO_POLARITY_HIGH);
        }

        mtx_unlock(&display->display_lock);

        if (display->dc_cb &&
                (display_removed != INVALID_DISPLAY_ID || display_added != INVALID_DISPLAY_ID)) {
            display->dc_cb->on_displays_changed(display->dc_cb_ctx,
                                                &display_added,
                                                display_added != INVALID_DISPLAY_ID,
                                                &display_removed,
                                                display_removed != INVALID_DISPLAY_ID);
        }

        mtx_unlock(&display->cb_lock);
    }
}

static int vsync_thread(void *arg)
{
    vim2_display_t* display = arg;

    for (;;) {
        zx_status_t status;
        zx_time_t timestamp;
        status = zx_interrupt_wait(display->vsync_interrupt, &timestamp);
        if (status != ZX_OK) {
            DISP_INFO("Vsync wait failed");
            break;
        }

        mtx_lock(&display->cb_lock);
        mtx_lock(&display->display_lock);

        uint64_t display_id = display->display_id;
        bool attached = display->display_attached;
        void* live = (void*) (uint64_t) display->current_image;
        uint8_t is_client_handle = display->current_image != display->fb_canvas_idx;
        mtx_unlock(&display->display_lock);

        if (display->dc_cb && attached) {
            display->dc_cb->on_display_vsync(display->dc_cb_ctx, display_id, timestamp,
                                             &live, is_client_handle);
        }

        mtx_unlock(&display->cb_lock);
    }

    return 0;
}

zx_status_t vim2_display_bind(void* ctx, zx_device_t* parent) {
    vim2_display_t* display = calloc(1, sizeof(vim2_display_t));
    if (!display) {
        DISP_ERROR("Could not allocated display structure\n");
        return ZX_ERR_NO_MEMORY;
    }

    display->parent = parent;

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

    status = device_get_protocol(parent, ZX_PROTOCOL_CANVAS, &display->canvas);
    if (status != ZX_OK) {
        DISP_ERROR("Could not get Display CANVAS protocol\n");
        goto fail;
    }

    // Map all the various MMIOs
    pdev_device_info_t dev_info;
    status = pdev_get_device_info(&display->pdev, &dev_info);
    if (status != ZX_OK) {
        DISP_ERROR("Failed to fetch device info (status %d)\n", status);
        goto fail;
    }

    if (dev_info.mmio_count != MMIO_COUNT) {
        DISP_ERROR("MMIO region count mismatch!  Expected %u regions to be supplied by board "
                   "driver, but only %u were passed\n", MMIO_COUNT, dev_info.mmio_count);
        goto fail;
    }

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

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_CBUS, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_cbus);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO CBUS\n");
        goto fail;
    }

    status = gpio_config(&display->gpio, 0, GPIO_DIR_IN | GPIO_PULL_DOWN);
    if (status != ZX_OK) {
        DISP_ERROR("gpio_config failed for gpio\n");
        goto fail;
    }

    status = gpio_get_interrupt(&display->gpio, 0, ZX_INTERRUPT_MODE_LEVEL_HIGH, &display->inth);
    if (status != ZX_OK) {
        DISP_ERROR("gpio_get_interrupt failed for gpio\n");
        goto fail;
    }

    status = pdev_map_interrupt(&display->pdev, 0, &display->vsync_interrupt);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map vsync interrupt\n");
        goto fail;
    }
    // For some reason the vsync interrupt enable bit needs to be cleared for
    // vsync interrupts to occur at the correct rate.
    *((uint32_t*)(display->mmio_vpu.virt + VPU_VIU_MISC_CTRL0)) &= ~(1 << 8);

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

    device_add_args_t add_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "vim2-display",
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

    display->display_id = 1;
    display->display_attached = false;
    list_initialize(&display->imported_images);
    mtx_init(&display->display_lock, mtx_plain);
    mtx_init(&display->image_lock, mtx_plain);
    mtx_init(&display->cb_lock, mtx_plain);

    thrd_create_with_name(&display->main_thread, hdmi_irq_handler, display, "hdmi_irq_handler");
    thrd_create_with_name(&display->vsync_thread, vsync_thread, display, "vsync_thread");

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
