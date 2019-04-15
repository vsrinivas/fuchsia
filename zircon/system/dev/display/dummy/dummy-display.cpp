// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dummy-display.h"
#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <fbl/auto_call.h>
#include <fuchsia/sysmem/c/fidl.h>

namespace dummy_display {
#define DISP_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

namespace {
// List of supported pixel formats
zx_pixel_format_t kSupportedPixelFormats[] = {ZX_PIXEL_FORMAT_RGB_x888};
// Arbitrary dimensions - the same as astro.
constexpr uint32_t kWidth = 1024;
constexpr uint32_t kHeight = 600;

constexpr uint64_t kDisplayId = 1;

constexpr uint32_t kRefreshRateFps = 60;
} // namespace

void DummyDisplay::PopulateAddedDisplayArgs(added_display_args_t* args) {
    args->display_id = kDisplayId;
    args->edid_present = false;
    args->panel.params.height = kHeight;
    args->panel.params.width = kWidth;
    args->panel.params.refresh_rate_e2 = kRefreshRateFps * 100;
    args->pixel_format_list = kSupportedPixelFormats;
    args->pixel_format_count = countof(kSupportedPixelFormats);
    args->cursor_info_count = 0;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
uint32_t DummyDisplay::DisplayControllerImplComputeLinearStride(uint32_t width,
                                                                zx_pixel_format_t format) {
    return ROUNDUP(width, 32 / ZX_PIXEL_FORMAT_BYTES(format));
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void DummyDisplay::DisplayControllerImplSetDisplayControllerInterface(
    const display_controller_interface_protocol_t* intf) {
    fbl::AutoLock lock(&display_lock_);
    dc_intf_ = ddk::DisplayControllerInterfaceProtocolClient(intf);
    added_display_args_t args;
    PopulateAddedDisplayArgs(&args);
    dc_intf_.OnDisplaysChanged(&args, 1, nullptr, 0, nullptr, 0, nullptr);
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t DummyDisplay::DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo,
                                                              size_t offset) {
    zx_status_t status = ZX_OK;

    if (image->type != IMAGE_TYPE_SIMPLE || image->pixel_format != kSupportedPixelFormats[0]) {
        status = ZX_ERR_INVALID_ARGS;
        return status;
    }

    void* new_handle = malloc(1);
    image->handle = reinterpret_cast<uint64_t>(new_handle);

    return status;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t DummyDisplay::DisplayControllerImplImportImage(image_t* image,
                                                           zx_unowned_handle_t handle,
                                                           uint32_t index) {
    zx_status_t status = ZX_OK;

    if (image->type != IMAGE_TYPE_SIMPLE || image->pixel_format != kSupportedPixelFormats[0]) {
        status = ZX_ERR_INVALID_ARGS;
        return status;
    }

    void* new_handle = malloc(1);
    image->handle = reinterpret_cast<uint64_t>(new_handle);

    return status;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void DummyDisplay::DisplayControllerImplReleaseImage(image_t* image) {
    free(reinterpret_cast<void*>(image->handle));
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
uint32_t DummyDisplay::DisplayControllerImplCheckConfiguration(
    const display_config_t** display_configs, size_t display_count, uint32_t** layer_cfg_results,
    size_t* layer_cfg_result_count) {

    if (display_count != 1) {
        ZX_DEBUG_ASSERT(display_count == 0);
        return CONFIG_DISPLAY_OK;
    }
    ZX_DEBUG_ASSERT(display_configs[0]->display_id == kDisplayId);

    fbl::AutoLock lock(&display_lock_);

    bool success;
    if (display_configs[0]->layer_count != 1) {
        success = display_configs[0]->layer_count == 0;
    } else {
        const primary_layer_t& layer = display_configs[0]->layer_list[0]->cfg.primary;
        frame_t frame = {
            .x_pos = 0, .y_pos = 0, .width = kWidth, .height = kHeight,
        };
        success = display_configs[0]->layer_list[0]->type == LAYER_TYPE_PRIMARY
                && layer.transform_mode == FRAME_TRANSFORM_IDENTITY
                && layer.image.width == kWidth
                && layer.image.height == kHeight
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
void DummyDisplay::DisplayControllerImplApplyConfiguration(const display_config_t** display_configs,
                                                           size_t display_count) {
    ZX_DEBUG_ASSERT(display_configs);

    fbl::AutoLock lock(&display_lock_);

    uint64_t addr;
    if (display_count == 1 && display_configs[0]->layer_count) {
        // Only support one display.
        addr =
            reinterpret_cast<uint64_t>(display_configs[0]->layer_list[0]->cfg.primary.image.handle);
        current_image_valid_ = true;
        current_image_ = addr;
    } else {
        current_image_valid_ = false;
    }
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t DummyDisplay::DisplayControllerImplAllocateVmo(uint64_t size, zx::vmo* vmo_out) {
    return zx::vmo::create(size, 0, vmo_out);
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t DummyDisplay::DisplayControllerImplGetSysmemConnection(zx::channel connection) {
    zx_status_t status = sysmem_connect(&sysmem_, connection.release());
    if (status != ZX_OK) {
        DISP_ERROR("Could not connect to sysmem\n");
        return status;
    }

    return ZX_OK;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t DummyDisplay::DisplayControllerImplSetBufferCollectionConstraints(
    const image_t* config, zx_unowned_handle_t collection) {
    fuchsia_sysmem_BufferCollectionConstraints constraints = {};
    constraints.usage.display = fuchsia_sysmem_displayUsageLayer;
    constraints.has_buffer_memory_constraints = true;
    fuchsia_sysmem_BufferMemoryConstraints& buffer_constraints =
        constraints.buffer_memory_constraints;
    buffer_constraints.min_size_bytes = 0;
    buffer_constraints.max_size_bytes = 0xffffffff;
    buffer_constraints.physically_contiguous_required = false;
    buffer_constraints.secure_required = false;
    buffer_constraints.secure_permitted = false;
    buffer_constraints.ram_domain_supported = true;
    buffer_constraints.cpu_domain_supported = true;
    constraints.image_format_constraints_count = 1;
    fuchsia_sysmem_ImageFormatConstraints& image_constraints =
        constraints.image_format_constraints[0];
    image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_BGRA32;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = fuchsia_sysmem_ColorSpaceType_SRGB;
    image_constraints.min_coded_width = 0;
    image_constraints.max_coded_width = 0xffffffff;
    image_constraints.min_coded_height = 0;
    image_constraints.max_coded_height = 0xffffffff;
    image_constraints.min_bytes_per_row = 0;
    image_constraints.max_bytes_per_row = 0xffffffff;
    image_constraints.max_coded_width_times_coded_height = 0xffffffff;
    image_constraints.layers = 1;
    image_constraints.coded_width_divisor = 1;
    image_constraints.coded_height_divisor = 1;
    image_constraints.bytes_per_row_divisor = 1;
    image_constraints.start_offset_divisor = 1;
    image_constraints.display_width_divisor = 1;
    image_constraints.display_height_divisor = 1;

    zx_status_t status = fuchsia_sysmem_BufferCollectionSetConstraints(collection, true,
                                                                       &constraints);

    if (status != ZX_OK) {
        DISP_ERROR("Failed to set constraints");
        return status;
    }

    return ZX_OK;
}

void DummyDisplay::DdkUnbind() {
    DdkRemove();
}

void DummyDisplay::DdkRelease() {
    vsync_shutdown_flag_.store(true);
    thrd_join(vsync_thread_, NULL);
    delete this;
}

zx_status_t DummyDisplay::SetupDisplayInterface() {
    fbl::AutoLock lock(&display_lock_);

    current_image_valid_ = false;

    if (dc_intf_.is_valid()) {
        added_display_args_t args;
        PopulateAddedDisplayArgs(&args);
        dc_intf_.OnDisplaysChanged(&args, 1, nullptr, 0, nullptr, 0, nullptr);
    }

    return ZX_OK;
}

int DummyDisplay::VSyncThread() {
    zx_status_t status = ZX_OK;
    while (1) {
        zx::nanosleep(zx::deadline_after(zx::sec(1) / kRefreshRateFps));
        if (vsync_shutdown_flag_.load()) {
            break;
        }
        fbl::AutoLock lock(&display_lock_);
        uint64_t live[] = {current_image_};
        bool current_image_valid = current_image_valid_;
        if (dc_intf_.is_valid()) {
            dc_intf_.OnDisplayVsync(kDisplayId, zx_clock_get_monotonic(),
                                    live, current_image_valid);
        }
    }

    return status;
}

zx_status_t DummyDisplay::Bind() {
    zx_status_t status;

    status = device_get_protocol(parent_, ZX_PROTOCOL_SYSMEM, &sysmem_);
    if (status != ZX_OK) {
        DISP_ERROR("Could not get Display SYSMEM protocol\n");
        return status;
    }

    // Setup Display Interface
    status = SetupDisplayInterface();
    if (status != ZX_OK) {
        DISP_ERROR("Dummy display setup failed! %d\n", status);
        return status;
    }

    auto start_thread = [](void* arg) { return static_cast<DummyDisplay*>(arg)->VSyncThread(); };
    status = thrd_create_with_name(&vsync_thread_, start_thread, this, "vsync_thread");
    if (status != ZX_OK) {
        DISP_ERROR("Could not create vsync_thread\n");
        return status;
    }

    status = DdkAdd("dummy-display");
    if (status != ZX_OK) {
        DISP_ERROR("Could not add device\n");
        return status;
    }

    return ZX_OK;
}

} // namespace dummy_display

// main bind function called from dev manager
extern "C" zx_status_t dummy_display_bind(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<dummy_display::DummyDisplay>(&ac,
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

static zx_driver_ops_t dummy_display_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = nullptr,
    .bind = dummy_display_bind,
    .create = nullptr,
    .release = nullptr,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(dummy_display, dummy_display_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_DUMMY_DISPLAY),
ZIRCON_DRIVER_END(dummy_display)
