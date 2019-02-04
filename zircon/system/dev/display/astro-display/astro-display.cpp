// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "astro-display.h"
#include <fbl/auto_call.h>
#include <ddk/platform-defs.h>

namespace astro_display {

namespace {
// List of supported pixel formats
zx_pixel_format_t kSupportedPixelFormats[] = { ZX_PIXEL_FORMAT_RGB_x888 };

constexpr uint64_t kDisplayId = PANEL_DISPLAY_ID;

// Astro/Sherlock Display Configuration. These configuration comes directly from
// from LCD vendor and hardware team.
constexpr DisplaySetting kDisplaySettingTV070WSM_FT = {
    .lane_num                   = 4,
    .bit_rate_max               = 360,
    .clock_factor               = 8,
    .lcd_clock                  = 44250000,
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
};
constexpr DisplaySetting kDisplaySettingP070ACB_FT = {
    .lane_num                   = 4,
    .bit_rate_max               = 400,
    .clock_factor               = 8,
    .lcd_clock                  = 49434000,
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
};
constexpr DisplaySetting kDisplaySettingG101B158_FT = {
    .lane_num                   = 4,
    .bit_rate_max               = 566,
    .clock_factor               = 8,
    .lcd_clock                  = 70701600,
    .h_active                   = 800,
    .v_active                   = 1280,
    .h_period                   = 890,
    .v_period                   = 1324,
    .hsync_width                = 24,
    .hsync_bp                   = 20,
    .hsync_pol                  = 0,
    .vsync_width                = 4,
    .vsync_bp                   = 20,
    .vsync_pol                  = 0,
};
constexpr DisplaySetting kDisplaySettingTV101WXM_FT = {
    .lane_num                   = 4,
    .bit_rate_max               = 566,
    .clock_factor               = 8,
    .lcd_clock                  = 70701600,
    .h_active                   = 800,
    .v_active                   = 1280,
    .h_period                   = 890,
    .v_period                   = 1324,
    .hsync_width                = 20,
    .hsync_bp                   = 50,
    .hsync_pol                  = 0,
    .vsync_width                = 4,
    .vsync_bp                   = 20,
    .vsync_pol                  = 0,
};

} // namespace

// This function copies the display settings into our internal structure
void AstroDisplay::CopyDisplaySettings() {
    ZX_DEBUG_ASSERT(init_disp_table_);

    disp_setting_.h_active = init_disp_table_->h_active;
    disp_setting_.v_active = init_disp_table_->v_active;
    disp_setting_.h_period = init_disp_table_->h_period;
    disp_setting_.v_period = init_disp_table_->v_period;
    disp_setting_.hsync_width = init_disp_table_->hsync_width;
    disp_setting_.hsync_bp = init_disp_table_->hsync_bp;
    disp_setting_.hsync_pol = init_disp_table_->hsync_pol;
    disp_setting_.vsync_width = init_disp_table_->vsync_width;
    disp_setting_.vsync_bp = init_disp_table_->vsync_bp;
    disp_setting_.vsync_pol = init_disp_table_->vsync_pol;
    disp_setting_.lcd_clock = init_disp_table_->lcd_clock;
    disp_setting_.clock_factor = init_disp_table_->clock_factor;
    disp_setting_.lane_num = init_disp_table_->lane_num;
    disp_setting_.bit_rate_max = init_disp_table_->bit_rate_max;
}

void AstroDisplay::PopulateAddedDisplayArgs(added_display_args_t* args) {
    args->display_id = kDisplayId;
    args->edid_present = false;
    args->panel.params.height = height_;
    args->panel.params.width = width_;
    args->panel.params.refresh_rate_e2 = 3000; // Just guess that it's 30fps
    args->pixel_format_list = kSupportedPixelFormats;
    args->pixel_format_count = countof(kSupportedPixelFormats);
    args->cursor_info_count = 0;
}

zx_status_t AstroDisplay::DisplayInit() {
    zx_status_t status;
    fbl::AllocChecker ac;
    if (!skip_disp_init_) {
        // Detect panel type
        PopulatePanelType();
        if (panel_type_ == PANEL_TV070WSM_FT) {
            init_disp_table_ = &kDisplaySettingTV070WSM_FT;
        } else if (panel_type_ == PANEL_P070ACB_FT) {
            init_disp_table_ = &kDisplaySettingP070ACB_FT;
        } else if (panel_type_ == PANEL_TV101WXM_FT) {
            init_disp_table_ = &kDisplaySettingTV101WXM_FT;
        } else if (panel_type_ == PANEL_G101B158_FT) {
            init_disp_table_ = &kDisplaySettingG101B158_FT;
        } else {
            DISP_ERROR("Unsupported panel detected!\n");
            status = ZX_ERR_NOT_SUPPORTED;
            return status;
        }

        // Populated internal structures based on predefined tables
        CopyDisplaySettings();

        // Ensure Max Bit Rate / pixel clock ~= 8 (8.xxx). This is because the clock calculation
        // part of code assumes a clock factor of 1. All the LCD tables from Astro have this
        // relationship established. We'll have to revisit the calculation if this ratio cannot
        // be met.
        if (init_disp_table_->bit_rate_max / (init_disp_table_->lcd_clock / 1000 / 1000) != 8) {
            DISP_ERROR("Max Bit Rate / pixel clock != 8\n");
            status = ZX_ERR_INVALID_ARGS;
            return status;
        }

        // Setup VPU and VPP units first
        vpu_ = fbl::make_unique_checked<astro_display::Vpu>(&ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        status = vpu_->Init(parent_);
        if (status != ZX_OK) {
            DISP_ERROR("Could not initialize VPU object\n");
            return status;
        }

        vpu_->PowerOff();
        vpu_->PowerOn();
        vpu_->VppInit();
        clock_ = fbl::make_unique_checked<astro_display::AstroDisplayClock>(&ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        status = clock_->Init(parent_);
        if (status != ZX_OK) {
            DISP_ERROR("Could not initialize Clock object\n");
            return status;
        }

        // Enable all display related clocks
        status = clock_->Enable(disp_setting_);
        if (status != ZX_OK) {
            DISP_ERROR("Could not enable display clocks!\n");
            return status;
        }

        // Program and Enable DSI Host Interface
        dsi_host_ = fbl::make_unique_checked<astro_display::AmlDsiHost>(&ac,
                                                                        parent_,
                                                                        clock_->GetBitrate(),
                                                                        panel_type_);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        status = dsi_host_->Init();
        if (status != ZX_OK) {
            DISP_ERROR("Could not initialize DSI Host\n");
            return status;
        }

        status = dsi_host_->HostOn(disp_setting_);
        if (status != ZX_OK) {
            DISP_ERROR("DSI Host On failed! %d\n", status);
            return status;
        }
    }
    osd_ = fbl::make_unique_checked<astro_display::Osd>(&ac,
                                                        width_,
                                                        height_,
                                                        disp_setting_.h_active,
                                                        disp_setting_.v_active);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    // Initialize osd object
    status = osd_->Init(parent_);
    if (status != ZX_OK) {
        DISP_ERROR("Could not initialize OSD object\n");
        return status;
    }

    if (!skip_disp_init_) {
        osd_->HwInit();
    }

    // Configure osd layer
    current_image_valid_= false;
    status = osd_->Configure();
    if (status != ZX_OK) {
        DISP_ERROR("OSD configuration failed!\n");
        return status;
    }

    return ZX_OK;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
uint32_t AstroDisplay::DisplayControllerImplComputeLinearStride(uint32_t width,
                                                                zx_pixel_format_t format) {
    // The astro display controller needs buffers with a stride that is an even
    // multiple of 32.
    return ROUNDUP(width, 32 / ZX_PIXEL_FORMAT_BYTES(format));
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void AstroDisplay::DisplayControllerImplSetDisplayControllerInterface(
    const display_controller_interface_t* intf) {
    fbl::AutoLock lock(&display_lock_);
    dc_intf_ = ddk::DisplayControllerInterfaceClient(intf);
    added_display_args_t args;
    PopulateAddedDisplayArgs(&args);
    dc_intf_.OnDisplaysChanged(&args, 1, nullptr, 0, nullptr, 0, nullptr);
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t AstroDisplay::DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo,
                                                              size_t offset) {
    zx_status_t status = ZX_OK;
    fbl::AutoLock lock(&image_lock_);

    if (image->type != IMAGE_TYPE_SIMPLE || image->pixel_format != format_) {
        status = ZX_ERR_INVALID_ARGS;
        return status;
    }

    uint32_t stride = DisplayControllerImplComputeLinearStride(image->width, image->pixel_format);

    canvas_info_t canvas_info;
    canvas_info.height          = image->height;
    canvas_info.stride_bytes    = stride * ZX_PIXEL_FORMAT_BYTES(image->pixel_format);
    canvas_info.wrap            = 0;
    canvas_info.blkmode         = 0;
    canvas_info.endianness      = 0;

    uint8_t local_canvas_idx;
    status = amlogic_canvas_config(&canvas_, vmo.release(), offset, &canvas_info,
        &local_canvas_idx);
    if (status != ZX_OK) {
        DISP_ERROR("Could not configure canvas: %d\n", status);
        status = ZX_ERR_NO_RESOURCES;
        return status;
    }
    if (imported_images_.GetOne(local_canvas_idx)) {
        DISP_INFO("Reusing previously allocated canvas (index = %d)\n", local_canvas_idx);
    }
    imported_images_.SetOne(local_canvas_idx);
    image->handle = local_canvas_idx;
    return status;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void AstroDisplay::DisplayControllerImplReleaseImage(image_t* image) {
    fbl::AutoLock lock(&image_lock_);
    size_t local_canvas_idx = (size_t)image->handle;
    if (imported_images_.GetOne(local_canvas_idx)) {
        imported_images_.ClearOne(local_canvas_idx);
        amlogic_canvas_free(&canvas_, static_cast<uint8_t>(local_canvas_idx));
    }
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
uint32_t AstroDisplay::DisplayControllerImplCheckConfiguration(
    const display_config_t** display_configs, size_t display_count, uint32_t** layer_cfg_results,
    size_t* layer_cfg_result_count) {

    if (display_count != 1) {
        ZX_DEBUG_ASSERT(display_count == 0);
        return CONFIG_DISPLAY_OK;
    }
    ZX_DEBUG_ASSERT(display_configs[0]->display_id == PANEL_DISPLAY_ID);

    fbl::AutoLock lock(&display_lock_);

    bool success;
    if (display_configs[0]->layer_count != 1) {
        success = display_configs[0]->layer_count == 0;
    } else {
        const primary_layer_t& layer = display_configs[0]->layer_list[0]->cfg.primary;
        frame_t frame = {
            .x_pos = 0, .y_pos = 0, .width = width_, .height = height_,
        };
        success = display_configs[0]->layer_list[0]->type == LAYER_TYPE_PRIMARY
                && layer.transform_mode == FRAME_TRANSFORM_IDENTITY
                && layer.image.width == width_
                && layer.image.height == height_
                && memcmp(&layer.dest_frame, &frame, sizeof(frame_t)) == 0
                && memcmp(&layer.src_frame, &frame, sizeof(frame_t)) == 0
                && display_configs[0]->cc_flags == 0
                && layer.alpha_mode == ALPHA_DISABLE;
    }
    if (!success) {
        layer_cfg_results[0][0] = CLIENT_MERGE_BASE;
        for (unsigned i = 1; i < display_configs[0]->layer_count; i++) {
            layer_cfg_results[0][i] = CLIENT_MERGE_SRC;
        }
        layer_cfg_result_count[0] = display_configs[0]->layer_count;
    }
    return CONFIG_DISPLAY_OK;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void AstroDisplay::DisplayControllerImplApplyConfiguration(const display_config_t** display_configs,
                                                        size_t display_count) {
    ZX_DEBUG_ASSERT(display_configs);

    fbl::AutoLock lock(&display_lock_);
    uint8_t addr;
    if (display_count == 1 && display_configs[0]->layer_count) {
       if (!full_init_done_) {
            zx_status_t status;
            if ((status = DisplayInit()) != ZX_OK) {
                DISP_ERROR("Display Hardware Initialization failed! %d\n", status);
                ZX_ASSERT(0);
            }
            full_init_done_ = true;
        }

        // Since Astro does not support plug'n play (fixed display), there is no way
        // a checked configuration could be invalid at this point.
        addr = (uint8_t) (uint64_t) display_configs[0]->layer_list[0]->cfg.primary.image.handle;
        current_image_valid_= true;
        current_image_ = addr;
        osd_->FlipOnVsync(addr);
    } else {
        current_image_valid_= false;
        if (full_init_done_) {
            osd_->Disable();
        }
    }

    // If bootloader does not enable any of the display hardware, no vsync will be generated.
    // This fakes a vsync to let clients know we are ready until we actually initialize hardware
    if (!full_init_done_) {
        if (dc_intf_.is_valid()) {
            if (display_count == 0 || display_configs[0]->layer_count == 0) {
                dc_intf_.OnDisplayVsync(kDisplayId, zx_clock_get(ZX_CLOCK_MONOTONIC), nullptr, 0);
            }
        }
    }
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t AstroDisplay::DisplayControllerImplAllocateVmo(uint64_t size, zx::vmo* vmo_out) {
    return zx::vmo::create_contiguous(bti_, size, 0, vmo_out);
}

void AstroDisplay::DdkUnbind() {
    DdkRemove();
}

void AstroDisplay::DdkRelease() {
    if (osd_) {
        osd_->Release();
    }
    vsync_irq_.destroy();
    thrd_join(vsync_thread_, NULL);
    delete this;
}

// This function detect the panel type based.
void AstroDisplay::PopulatePanelType() {
    uint8_t pt;
    if ((gpio_config_in(&gpio_, GPIO_NO_PULL) == ZX_OK) &&
        (gpio_read(&gpio_, &pt) == ZX_OK)) {
        panel_type_ = pt;
        if (board_info_.pid == PDEV_PID_ASTRO) {
            DISP_INFO("Detected panel type = %s (%d)\n",
                      panel_type_ ? "P070ACB_FT" : "TV070WSM_FT", panel_type_);
        } else if (board_info_.pid == PDEV_PID_SHERLOCK) {
            DISP_INFO("Detected panel type = %s (%d)\n",
                      panel_type_ ? "G101B158_FT" : "TV101WXM_FT", panel_type_);
            panel_type_ = static_cast<uint8_t>(pt + PANEL_TV101WXM_FT);
        } else {
            DISP_ERROR("Panel detection attempted on Unsupported hardware\n");
            ZX_ASSERT(0);
        }
    } else {
        panel_type_ = PANEL_UNKNOWN;
        DISP_ERROR("Failed to detect a valid panel\n");
    }
}

zx_status_t AstroDisplay::SetupDisplayInterface() {
    fbl::AutoLock lock(&display_lock_);

    // Figure out board rev and panel type for Astro only
    if (board_info_.pid == PDEV_PID_ASTRO) {
        panel_type_ = PANEL_UNKNOWN;
        skip_disp_init_ = false;
        if (board_info_.board_revision < BOARD_REV_EVT_1) {
            DISP_INFO("Unsupported Board REV (%d). Will skip display driver initialization\n",
                board_info_.board_revision);
            skip_disp_init_ = true;
        }
    } else if (board_info_.pid == PDEV_PID_SHERLOCK) {
        panel_type_ = PANEL_UNKNOWN;
        skip_disp_init_ = false;
    } else {
        skip_disp_init_ = true;
    }

    format_ = ZX_PIXEL_FORMAT_RGB_x888;
    stride_ = DisplayControllerImplComputeLinearStride(width_, format_);

    {
        // Reset imported_images_ bitmap
        fbl::AutoLock lock(&image_lock_);
        imported_images_.Reset(kMaxImportedImages);
    }

    if (dc_intf_.is_valid()) {
        added_display_args_t args;
        PopulateAddedDisplayArgs(&args);
        dc_intf_.OnDisplaysChanged(&args, 1, nullptr, 0, nullptr, 0, nullptr);
    }

    return ZX_OK;
}

int AstroDisplay::VSyncThread() {
    zx_status_t status;
    while (1) {
        status = vsync_irq_.wait(nullptr);
        if (status != ZX_OK) {
            DISP_ERROR("VSync Interrupt Wait failed\n");
            break;
        }
        fbl::AutoLock lock(&display_lock_);
        uint64_t live[] = { current_image_ };
        bool current_image_valid = current_image_valid_;
        if (dc_intf_.is_valid()) {
            dc_intf_.OnDisplayVsync(kDisplayId, zx_clock_get(ZX_CLOCK_MONOTONIC),
                                    live, current_image_valid);
        }
    }

    return status;
}

// TODO(payamm): make sure unbind/release are called if we return error
zx_status_t AstroDisplay::Bind() {
    zx_status_t status;

    status = device_get_protocol(parent_, ZX_PROTOCOL_PDEV, &pdev_);
    if (status !=  ZX_OK) {
        DISP_ERROR("Could not get parent protocol\n");
        return status;
    }

    // Get board info
    status = pdev_get_board_info(&pdev_, &board_info_);
    if (status != ZX_OK) {
        DISP_ERROR("Could not obtain board info\n");
        return status;
    }

    if (board_info_.pid == PDEV_PID_ASTRO) {
        width_ = ASTRO_DISPLAY_WIDTH;
        height_ = ASTRO_DISPLAY_HEIGHT;
    } else if (board_info_.pid == PDEV_PID_SHERLOCK) {
        width_ = SHERLOCK_DISPLAY_WIDTH;
        height_ = SHERLOCK_DISPLAY_HEIGHT;
    } else {
        DISP_ERROR("Running on Unsupported hardware. Use at your own risk\n");
    }

    // Obtain GPIO Protocol for Panel reset
    size_t actual;
    status = pdev_get_protocol(&pdev_, ZX_PROTOCOL_GPIO, GPIO_PANEL_DETECT,
                               &gpio_,
                               sizeof(gpio_),
                               &actual);
    if (status != ZX_OK) {
        DISP_ERROR("Could not obtain GPIO protocol.\n");
        return status;
    }

    status = device_get_protocol(parent_, ZX_PROTOCOL_AMLOGIC_CANVAS, &canvas_);
    if (status != ZX_OK) {
        DISP_ERROR("Could not obtain CANVAS protocol\n");
        return status;
    }

    status = pdev_get_bti(&pdev_, 0, bti_.reset_and_get_address());
    if (status != ZX_OK) {
        DISP_ERROR("Could not get BTI handle\n");
        return status;
    }

    // Setup Display Interface
    status = SetupDisplayInterface();
    if (status != ZX_OK) {
        DISP_ERROR("Astro display setup failed! %d\n", status);
        return status;
    }

    // Map VSync Interrupt
    status = pdev_get_interrupt(&pdev_, IRQ_VSYNC, 0, vsync_irq_.reset_and_get_address());
    if (status  != ZX_OK) {
        DISP_ERROR("Could not map vsync interrupt\n");
        return status;
    }

    auto start_thread = [](void* arg) { return static_cast<AstroDisplay*>(arg)->VSyncThread(); };
    status = thrd_create_with_name(&vsync_thread_, start_thread, this, "vsync_thread");
    if (status  != ZX_OK) {
        DISP_ERROR("Could not create vsync_thread\n");
        return status;
    }

    auto cleanup = fbl::MakeAutoCall([&]() { DdkRelease(); });

    status = DdkAdd("astro-display");
    if (status != ZX_OK) {
        DISP_ERROR("Could not add device\n");
        return status;
    }

    cleanup.cancel();
    return ZX_OK;
}

void AstroDisplay::Dump() {
    DISP_INFO("#############################\n");
    DISP_INFO("Dumping disp_setting structure:\n");
    DISP_INFO("#############################\n");
    DISP_INFO("h_active = 0x%x (%u)\n", disp_setting_.h_active,
              disp_setting_.h_active);
    DISP_INFO("v_active = 0x%x (%u)\n", disp_setting_.v_active,
              disp_setting_.v_active);
    DISP_INFO("h_period = 0x%x (%u)\n", disp_setting_.h_period,
              disp_setting_.h_period);
    DISP_INFO("v_period = 0x%x (%u)\n", disp_setting_.v_period,
              disp_setting_.v_period);
    DISP_INFO("hsync_width = 0x%x (%u)\n", disp_setting_.hsync_width,
              disp_setting_.hsync_width);
    DISP_INFO("hsync_bp = 0x%x (%u)\n", disp_setting_.hsync_bp,
              disp_setting_.hsync_bp);
    DISP_INFO("hsync_pol = 0x%x (%u)\n", disp_setting_.hsync_pol,
              disp_setting_.hsync_pol);
    DISP_INFO("vsync_width = 0x%x (%u)\n", disp_setting_.vsync_width,
              disp_setting_.vsync_width);
    DISP_INFO("vsync_bp = 0x%x (%u)\n", disp_setting_.vsync_bp,
              disp_setting_.vsync_bp);
    DISP_INFO("vsync_pol = 0x%x (%u)\n", disp_setting_.vsync_pol,
              disp_setting_.vsync_pol);
    DISP_INFO("lcd_clock = 0x%x (%u)\n", disp_setting_.lcd_clock,
              disp_setting_.lcd_clock);
    DISP_INFO("lane_num = 0x%x (%u)\n", disp_setting_.lane_num,
              disp_setting_.lane_num);
    DISP_INFO("bit_rate_max = 0x%x (%u)\n", disp_setting_.bit_rate_max,
              disp_setting_.bit_rate_max);
    DISP_INFO("clock_factor = 0x%x (%u)\n", disp_setting_.clock_factor,
              disp_setting_.clock_factor);
}

} // namespace astro_display

// main bind function called from dev manager
extern "C" zx_status_t astro_display_bind(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<astro_display::AstroDisplay>(&ac,
        parent);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto status = dev->Bind();
    if (status == ZX_OK) {
        // devmgr is now in charge of the memory for dev
        __UNUSED auto ptr = dev.release();
    }
    return status;
}
