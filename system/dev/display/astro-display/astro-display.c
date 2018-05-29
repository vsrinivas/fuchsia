// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "astro-display.h"

 // Astro Display Configuration. These configuration comes directly from
 // from LCD vendor and hardware team.
const display_setting_t g_disp_setting_TV070WSM_FT = {
    .h_active                   = 600,
    .v_active                   = 1024,
    .h_period                   = 700,
    .v_period                   = 1053,
    .hsync_width                = 24,
    .hsync_bp                   = 36,
    .hsync_pol                  = 0,
    .vsync_width                = 2,
    .vsync_bp                   = 8,
    .vsync_pol                  = 0,
    .lcd_clock                  = 44250000,
    .clock_factor               = 8,
    .lane_num                   = 4,
    .bit_rate_max               = 360,
};
const display_setting_t g_disp_setting_P070ACB_FT = {
    .h_active                   = 600,
    .v_active                   = 1024,
    .h_period                   = 770,
    .v_period                   = 1070,
    .hsync_width                = 10,
    .hsync_bp                   = 80,
    .hsync_pol                  = 0,
    .vsync_width                = 6,
    .vsync_bp                   = 20,
    .vsync_pol                  = 0,
    .lcd_clock                  = 49434000,
    .clock_factor               = 8,
    .lane_num                   = 4,
    .bit_rate_max               = 400,
};

// This global variable will point to either of the display settings based on
// the detected panel type
const display_setting_t* g_disp_setting;

// List of supported pixel formats
static const zx_pixel_format_t _gsupported_pixel_formats = { ZX_PIXEL_FORMAT_RGB_x888 };

// This function copies the display settings into our internal structure
static void copy_disp_setting(astro_display_t* display) {
    ZX_DEBUG_ASSERT(display);
    ZX_DEBUG_ASSERT(g_disp_setting);

    display->disp_setting.h_active = g_disp_setting->h_active;
    display->disp_setting.v_active = g_disp_setting->v_active;
    display->disp_setting.h_period = g_disp_setting->h_period;
    display->disp_setting.v_period = g_disp_setting->v_period;
    display->disp_setting.hsync_width = g_disp_setting->hsync_width;
    display->disp_setting.hsync_bp = g_disp_setting->hsync_bp;
    display->disp_setting.hsync_pol = g_disp_setting->hsync_pol;
    display->disp_setting.vsync_width = g_disp_setting->vsync_width;
    display->disp_setting.vsync_bp = g_disp_setting->vsync_bp;
    display->disp_setting.vsync_pol = g_disp_setting->vsync_pol;
    display->disp_setting.lcd_clock = g_disp_setting->lcd_clock;
    display->disp_setting.clock_factor = g_disp_setting->clock_factor;
    display->disp_setting.lane_num = g_disp_setting->lane_num;
    display->disp_setting.bit_rate_max = g_disp_setting->bit_rate_max;
}

typedef struct image_info {
    zx_handle_t     pmt;
    uint8_t         canvas_idx;
    list_node_t     node;
} image_info_t;

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
static uint32_t astro_compute_linear_stride(void* ctx, uint32_t width, zx_pixel_format_t format) {
    // The astro display controller needs buffers with a stride that is an even
    // multiple of 32.
    return ROUNDUP(width, 32 / ZX_PIXEL_FORMAT_BYTES(format));
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
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

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
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

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
static zx_status_t astro_import_vmo_image(void* ctx, image_t* image, zx_handle_t vmo,
                                          size_t offset) {
    image_info_t* import_info = calloc(1, sizeof(image_info_t));
    if (import_info == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    astro_display_t* display = ctx;
    zx_status_t status = ZX_OK;
    mtx_lock(&display->image_lock);

    if (image->type != IMAGE_TYPE_SIMPLE || image->pixel_format != display->format) {
        status = ZX_ERR_INVALID_ARGS;
        goto fail;
    }

    uint32_t stride = astro_compute_linear_stride(display, image->width, image->pixel_format);

    canvas_info_t canvas_info;
    canvas_info.height =        image->height;
    canvas_info.stride_bytes =  stride * ZX_PIXEL_FORMAT_BYTES(image->pixel_format);
    canvas_info.wrap =          0;
    canvas_info.blkmode =       0;
    canvas_info.endianness =    0;

    zx_handle_t dup_vmo;
    status = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
    if (status != ZX_OK) {
        goto fail;
    }

    status = canvas_config(&display->canvas, dup_vmo, offset, &canvas_info,
        &import_info->canvas_idx);
    if (status != ZX_OK) {
        DISP_ERROR("Could not configure canvas: %d\n", status);
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

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
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
        canvas_free(&display->canvas, info->canvas_idx);
        free(info);
    }
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
static void astro_check_configuration(void* ctx,
                                      const display_config_t** display_configs,
                                      uint32_t* display_cfg_result,
                                      uint32_t** layer_cfg_results,
                                      uint32_t display_count) {
    *display_cfg_result = CONFIG_DISPLAY_OK;
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

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
static void astro_apply_configuration(void* ctx,
                                      const display_config_t** display_configs,
                                      uint32_t display_count) {
    ZX_DEBUG_ASSERT(ctx);
    ZX_DEBUG_ASSERT(display_configs);
    ZX_DEBUG_ASSERT(&display_configs[0]);

    astro_display_t* display = ctx;
    mtx_lock(&display->display_lock);

    uint8_t addr;
    if (display_count == 1 && display_configs[0]->layer_count) {
        // Since Astro does not support plug'n play (fixed display), there is no way
        // a checked configuration could be invalid at this point.
        addr = (uint8_t) (uint64_t) display_configs[0]->layers[0]->cfg.primary.image.handle;
    } else {
        addr = display->fb_canvas_idx;
    }
    flip_osd(display, addr);

    mtx_unlock(&display->display_lock);
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
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
        io_buffer_release(&display->mmio_mipi_dsi);
        io_buffer_release(&display->mmio_dsi_phy);
        io_buffer_release(&display->mmio_hhi);
        io_buffer_release(&display->mmio_vpu);
        io_buffer_release(&display->fbuffer);
        zx_handle_close(display->bti);
        zx_handle_close(display->vsync_interrupt);
    }
    free(display);
}

static zx_protocol_device_t main_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = display_release,
};

// This function determines the board ID. This information should ideally come from
// the board driver.
// TODO: ZX-2452 Add board info API to platform device
static void populate_board_rev(astro_display_t* display) {
    uint8_t id0, id1, id2;

    // Vital info. Causing a panic here is better then returning an error.
    if ((gpio_config(&display->gpio, GPIO_HW_ID0, GPIO_DIR_IN | GPIO_NO_PULL) == ZX_OK) &&
        (gpio_config(&display->gpio, GPIO_HW_ID1, GPIO_DIR_IN | GPIO_NO_PULL) == ZX_OK) &&
        (gpio_config(&display->gpio, GPIO_HW_ID2, GPIO_DIR_IN | GPIO_NO_PULL) == ZX_OK) &&
        (gpio_read(&display->gpio, GPIO_HW_ID0, &id0) == ZX_OK) &&
        (gpio_read(&display->gpio, GPIO_HW_ID1, &id1) == ZX_OK) &&
        (gpio_read(&display->gpio, GPIO_HW_ID2, &id2) == ZX_OK)) {
        display->board_rev = id0 + (id1 << 1) + (id2 << 2);
        DISP_INFO("Detected Board ID = %d\n", display->board_rev);
    } else {
        display->board_rev = BOARD_REV_UNKNOWN;
        DISP_ERROR("Failed to detect a valid board id\n");
    }
}

// This function detect the panel type based.
static void populate_panel_type(astro_display_t* display) {
    uint8_t pt;
    if ((gpio_config(&display->gpio, GPIO_PANEL_DETECT, GPIO_DIR_IN | GPIO_NO_PULL) == ZX_OK) &&
        (gpio_read(&display->gpio, GPIO_PANEL_DETECT, &pt) == ZX_OK)) {
        display->panel_type = pt;
        DISP_INFO("Detected panel type = %s (%d)\n",
                  display->panel_type ? "P070ACB_FT" : "TV070WSM_FT", display->panel_type);
    } else {
        display->panel_type = PANEL_UNKNOWN;
        DISP_ERROR("Failed to detect a valid panel\n");
    }
}

// This function is the main function called to setup the display interface
static zx_status_t setup_display_interface(astro_display_t* display) {
    zx_status_t status;

    mtx_lock(&display->cb_lock);
    mtx_lock(&display->display_lock);

    display->skip_disp_init = false;
    display->panel_type = PANEL_UNKNOWN;
    display->board_rev = BOARD_REV_UNKNOWN;

    // Detect board ID first
    populate_board_rev(display);

    if ((display->board_rev == BOARD_REV_UNKNOWN) ||
        (display->board_rev < MIN_BOARD_REV_SUPPORTED)) {
        DISP_INFO("Unsupported Board REV. Will skip display driver initialization\n");
        display->skip_disp_init = true;
    }

    if (!display->skip_disp_init) {
        // Detect panel type
        populate_panel_type(display);

        if (display->panel_type == PANEL_TV070WSM_FT) {
            g_disp_setting = &g_disp_setting_TV070WSM_FT;
        } else if (display->panel_type == PANEL_P070ACB_FT) {
            g_disp_setting = &g_disp_setting_P070ACB_FT;
        } else {
            DISP_ERROR("Unsupported panel detected!\n");
            status = ZX_ERR_NOT_SUPPORTED;
            goto fail;
        }

        // Populated internal structures based on predefined tables
        copy_disp_setting(display);
    }

    // allocate frame buffer
    display->format = ZX_PIXEL_FORMAT_RGB_x888;
    display->width  = DISPLAY_WIDTH;
    display->height = DISPLAY_HEIGHT;
    display->stride = astro_compute_linear_stride(
            display, display->width, display->format);

    size_t size = display->stride * display->height * ZX_PIXEL_FORMAT_BYTES(display->format);
    if ((status = allocate_vmo(display, size, &display->fb_vmo)) != ZX_OK) {
        goto fail;
    }

    // Create a duplicate handle
    zx_handle_t fb_vmo_dup_handle;
    status = zx_handle_duplicate(display->fb_vmo, ZX_RIGHT_SAME_RIGHTS, &fb_vmo_dup_handle);
    if (status != ZX_OK) {
        DISP_ERROR("Unable to duplicate FB VMO handle\n");
        goto fail;
    }

    if (!display->skip_disp_init) {
        // Ensure Max Bit Rate / pixel clock ~= 8 (8.xxx). This is because the clock calculation
        // part of code assumes a clock factor of 1. All the LCD tables from Astro have this
        // relationship established. We'll have to revisit the calculation if this ratio cannot
        // be met.
        if (g_disp_setting->bit_rate_max / (g_disp_setting->lcd_clock/1000/1000) != 8) {
            DISP_ERROR("Max Bit Rate / pixel clock != 8\n");
            status = ZX_ERR_INVALID_ARGS;
            goto fail;
        }

        // Initialize all display related clocks
        if ((status = display_clock_init(display)) != ZX_OK) {
            DISP_ERROR("Display clock init failed! %d\n", status);
            goto fail;
        }

        // Program and Enable DSI Host Interface
        if ((status = aml_dsi_host_on(display)) != ZX_OK) {
            DISP_ERROR("AML DSI Hosy Init failed %d\n", status);
            goto fail;
        }
    }

    zx_vaddr_t virt;
    status = zx_vmar_map(zx_vmar_root_self(), 0, display->fb_vmo, 0, size,
                                ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &virt);
    if (status != ZX_OK) {
        DISP_ERROR("zx_vmar_map failed %d size %zu\n", status, size);
        goto fail;
    }

    // Configure Canvas memory
    canvas_info_t canvas_info;
    canvas_info.height              = display->height;
    canvas_info.stride_bytes        = display->stride * ZX_PIXEL_FORMAT_BYTES(display->format);
    canvas_info.wrap                = 0;
    canvas_info.blkmode             = 0;
    canvas_info.endianness          = 0;

    status = canvas_config(&display->canvas, fb_vmo_dup_handle, 0, &canvas_info,
                                &display->fb_canvas_idx);
    if (status != ZX_OK) {
        DISP_ERROR("Unable to configure canvas: %d\n", status);
        goto fail;
    }

    configure_osd(display, display->fb_canvas_idx);

    zx_framebuffer_set_range(get_root_resource(), display->fb_vmo,
                             size, display->format,
                             display->width, display->height,
                             display->stride);

    // Initialize and turn on backlight
    init_backlight(display);

    mtx_unlock(&display->display_lock);

    if (display->dc_cb) {
        uint64_t display_added = PANEL_DISPLAY_ID;
        display->dc_cb->on_displays_changed(display->dc_cb_ctx, &display_added, 1, NULL, 0);
    }
    mtx_unlock(&display->cb_lock);

    return ZX_OK;

fail:
    if (display->fb_vmo) {
        zx_handle_close(display->fb_vmo);
    }
    mtx_unlock(&display->display_lock);
    mtx_unlock(&display->cb_lock);
    return status;
}

static zx_status_t vsync_thread(void *arg) {
    zx_status_t status = ZX_OK;
    astro_display_t* display = arg;

    while (1) {
        zx_time_t timestamp;
        status = zx_interrupt_wait(display->vsync_interrupt, &timestamp);
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
            display->dc_cb->on_display_vsync(display->dc_cb_ctx, PANEL_DISPLAY_ID, timestamp,
                                             &live, is_client_handle);
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

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &display->pdev);
    if (status !=  ZX_OK) {
        DISP_ERROR("Could not get parent protocol\n");
        goto fail;
    }

    // Obtain I2C Protocol for backlight
    status = device_get_protocol(parent, ZX_PROTOCOL_I2C, &display->i2c);
    if (status != ZX_OK) {
        DISP_ERROR("Could not obtain I2C protocol\n");
        goto fail;
    }

    // Obtain GPIO Protocol for Panel reset
    status = device_get_protocol(parent, ZX_PROTOCOL_GPIO, &display->gpio);
    if (status != ZX_OK) {
        DISP_ERROR("Could not obtain GPIO protocol\n");
        goto fail;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_CANVAS, &display->canvas);
    if (status != ZX_OK) {
        DISP_ERROR("Could not obtain CANVAS protocol\n");
        goto fail;
    }

    status = pdev_get_bti(&display->pdev, 0, &display->bti);
    if (status != ZX_OK) {
        DISP_ERROR("Could not get BTI handle\n");
        goto fail;
    }

    // Map all the various MMIOs
    status = pdev_map_mmio_buffer(&display->pdev, MMIO_CANVAS, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_dmc);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO DMC\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_MPI_DSI, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_mipi_dsi);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO MIPI_DSI\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_DSI_PHY, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_dsi_phy);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO DSI PHY\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_HHI, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_hhi);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO HHI\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&display->pdev, MMIO_VPU, ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &display->mmio_vpu);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map display MMIO VPU\n");
        goto fail;
    }

    // Setup Display Interface
    status = setup_display_interface(display);
    if (status != ZX_OK) {
        DISP_ERROR("Astro display setup failed! %d\n", status);
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
