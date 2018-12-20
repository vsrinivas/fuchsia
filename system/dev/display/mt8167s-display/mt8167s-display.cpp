// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "mt8167s-display.h"
#include "common.h"
#include "registers-ovl.h"
#include <fbl/auto_call.h>
#include <lib/zx/pmt.h>
#include <zircon/pixelformat.h>
#include <ddk/binding.h>
#include <ddk/platform-defs.h>
namespace mt8167s_display {

namespace {
// List of supported pixel formats
zx_pixel_format_t kSupportedPixelFormats[] = {ZX_PIXEL_FORMAT_RGB_x888};
constexpr uint64_t kDisplayId = PANEL_DISPLAY_ID;

struct ImageInfo {
    zx::pmt pmt;
    zx_paddr_t paddr;
    // TODO(payamm): Use fbl lists instead
    list_node_t node;
};

} // namespace

void Mt8167sDisplay::PopulateAddedDisplayArgs(added_display_args_t* args) {
    args->display_id = kDisplayId;
    args->edid_present = false;
    args->panel.params.height = height_;
    args->panel.params.width = width_;
    args->panel.params.refresh_rate_e2 = 3000; // Just guess that it's 30fps
    args->pixel_format_list = kSupportedPixelFormats;
    args->pixel_format_count = countof(kSupportedPixelFormats);
    args->cursor_info_count = 0;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
uint32_t Mt8167sDisplay::DisplayControllerImplComputeLinearStride(uint32_t width,
                                                                  zx_pixel_format_t format) {
    return ROUNDUP(width, 32 / ZX_PIXEL_FORMAT_BYTES(format));
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void Mt8167sDisplay::DisplayControllerImplSetDisplayControllerInterface(
    const display_controller_interface_t* intf) {
    fbl::AutoLock lock(&display_lock_);
    dc_intf_ = ddk::DisplayControllerInterfaceClient(intf);
    added_display_args_t args;
    PopulateAddedDisplayArgs(&args);
    dc_intf_.OnDisplaysChanged(&args, 1, NULL, 0, NULL, 0, NULL);
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t Mt8167sDisplay::DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo,
                                                                size_t offset) {
    ImageInfo* import_info = new(ImageInfo);
    if (import_info == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }
    auto cleanup = fbl::MakeAutoCall([&]() {
        if (import_info->pmt) {
            import_info->pmt.unpin();
        }
        delete(import_info);
     });

    fbl::AutoLock lock(&image_lock_);
    if (image->type != IMAGE_TYPE_SIMPLE || image->pixel_format != kSupportedPixelFormats[0]) {
        return ZX_ERR_INVALID_ARGS;
    }

    uint32_t stride = DisplayControllerImplComputeLinearStride(image->width, image->pixel_format);
    unsigned pixel_size = ZX_PIXEL_FORMAT_BYTES(image->pixel_format);
    size_t size = ROUNDUP((stride * image->height * pixel_size) +
                          (offset & (PAGE_SIZE - 1)), PAGE_SIZE);
    zx_paddr_t paddr;
    zx_status_t status = bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS,
                                  vmo, offset & ~(PAGE_SIZE - 1), size, &paddr, 1,
                                  &import_info->pmt);
    if (status != ZX_OK) {
        DISP_ERROR("Could not pin bit\n");
        return status;
    }

    import_info->paddr = paddr;
    list_add_head(&imported_images_, &import_info->node);
    image->handle = paddr;
    cleanup.cancel();
    return status;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void Mt8167sDisplay::DisplayControllerImplReleaseImage(image_t* image) {
    fbl::AutoLock lock(&image_lock_);
    zx_paddr_t image_paddr = reinterpret_cast<zx_paddr_t>(image->handle);
    ImageInfo* info;
    list_for_every_entry(&imported_images_, info, ImageInfo, node) {
        if (info->paddr == image_paddr) {
            list_delete(&info->node);
            break;
        }
    }
    if (info) {
        info->pmt.unpin();
        delete(info);
    }
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
uint32_t Mt8167sDisplay::DisplayControllerImplCheckConfiguration(
    const display_config_t** display_configs, size_t display_count,
    uint32_t** layer_cfg_results, size_t* layer_cfg_result_count) {
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
        success = display_configs[0]->layer_list[0]->type == LAYER_TYPE_PRIMARY &&
                  layer.transform_mode == FRAME_TRANSFORM_IDENTITY &&
                  layer.image.width == width_ && layer.image.height == height_ &&
                  memcmp(&layer.dest_frame, &frame, sizeof(frame_t)) == 0 &&
                  memcmp(&layer.src_frame, &frame, sizeof(frame_t)) == 0 &&
                  display_configs[0]->cc_flags == 0 && layer.alpha_mode == ALPHA_DISABLE;
    }
    if (!success) {
        layer_cfg_results[0][0] = CLIENT_MERGE_BASE;
        for (unsigned i = 1; i < display_configs[0]->layer_count; i++) {
            layer_cfg_results[0][i] = CLIENT_MERGE_SRC;
        }
    }
    return CONFIG_DISPLAY_OK;
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
void Mt8167sDisplay::DisplayControllerImplApplyConfiguration(
    const display_config_t** display_configs, size_t display_count) {
    ZX_DEBUG_ASSERT(display_configs);

    fbl::AutoLock lock(&display_lock_);
    if (display_count == 1 && display_configs[0]->layer_count) {
        //TODO(payamm): if HDMI support is added + plug n play, we need to validate configuration
        zx_paddr_t addr =
            reinterpret_cast<zx_paddr_t>(display_configs[0]->layer_list[0]->cfg.primary.image.handle);
        current_image_valid_ = true;
        // write to register and hope for the best
        ovl_mmio_->Write32(static_cast<uint32_t>(addr), OVL_LX_ADDR(0));
    } else {
        //TODO(payamm): Properly disable ovl in the next round of the driver
        current_image_valid_ = false;
    }
}

// part of ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL ops
zx_status_t Mt8167sDisplay::DisplayControllerImplAllocateVmo(uint64_t size, zx::vmo* vmo_out) {
    return zx::vmo::create_contiguous(bti_, size, 0, vmo_out);
}

int Mt8167sDisplay::VSyncThread() {
    zx_status_t status;
    while (1) {
        // clear interrupt source
        // TODO(payamm): There are several sources of interrupt. Might be a good idea to make
        // sure the correct interrupt is being fired in the next phase of this driver
        ovl_mmio_->Write32(0x0, 0x8);
        zx::time timestamp;
        status = vsync_irq_.wait(&timestamp);
        if (status != ZX_OK) {
            DISP_ERROR("VSync Interrupt wait failed\n");
            break;
        }
        fbl::AutoLock lock(&display_lock_);
        uint64_t live = current_image_;
        bool current_image_valid = current_image_valid_;
        if (dc_intf_.is_valid()) {
            dc_intf_.OnDisplayVsync(kDisplayId, timestamp.get(), &live, current_image_valid);
        }
    }
    return ZX_OK;
}

void Mt8167sDisplay::Shutdown() {
    vsync_irq_.destroy();
    thrd_join(vsync_thread_, nullptr);
}

void Mt8167sDisplay::DdkUnbind() {
    Shutdown();
    DdkRemove();
}
void Mt8167sDisplay::DdkRelease() {
    delete this;
}

zx_status_t Mt8167sDisplay::Bind() {

    zx_status_t status = device_get_protocol(parent_, ZX_PROTOCOL_PDEV, &pdev_);
    if (status != ZX_OK) {
        DISP_ERROR("Could not get parent protocol\n");
        return status;
    }

    status = pdev_get_bti(&pdev_, 0, bti_.reset_and_get_address());
    if (status != ZX_OK) {
        DISP_ERROR("Could not get BTI handle\n");
        return status;
    }

    // Map VSync Interrupt
    status = pdev_map_interrupt(&pdev_, 0, vsync_irq_.reset_and_get_address());
    if (status != ZX_OK) {
        DISP_ERROR("Could not map vsync Interruptn");
        return status;
    }

    mmio_buffer_t mmio;
    status = pdev_map_mmio_buffer2(&pdev_, MMIO_DISP_OVL, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                   &mmio);
    if (status != ZX_OK) {
        DISP_ERROR("Could not map OVL mmio\n");
        return status;
    }
    fbl::AllocChecker ac;
    ovl_mmio_ = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mmio);
    if (!ac.check()) {
        DISP_ERROR("Could not mapp Overlay MMIO\n");
        return ZX_ERR_NO_MEMORY;
    }

    // Clear all OVL layers
    ovl_mmio_->Write32(0, OVL_LX_ADDR(0));
    ovl_mmio_->Write32(0, OVL_LX_ADDR(1));
    ovl_mmio_->Write32(0, OVL_LX_ADDR(2));
    ovl_mmio_->Write32(0, OVL_LX_ADDR(3));

    auto start_thread = [](void* arg) { return static_cast<Mt8167sDisplay*>(arg)->VSyncThread(); };
    status = thrd_create_with_name(&vsync_thread_, start_thread, this, "vsync_thread");
    if (status != ZX_OK) {
        DISP_ERROR("Could not create vsync_thread\n");
        return status;
    }

    list_initialize(&imported_images_);

    status = DdkAdd("mt8167s-display");
    if (status != ZX_OK) {
        DISP_ERROR("coud not add device\n");
        Shutdown();
        return status;
    }

    return ZX_OK;
}

// main bind function called from dev manager
zx_status_t display_bind(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<mt8167s_display::Mt8167sDisplay>(&ac, parent, DISPLAY_WIDTH,
                                                                         DISPLAY_HEIGHT);
    if (!ac.check()) {
        DISP_ERROR("no bind\n");
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t status = dev->Bind();
    if (status == ZX_OK) {
        __UNUSED auto ptr = dev.release();
    }
    return status;
}

static zx_driver_ops_t display_ops = [](){
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = display_bind;
    return ops;
}();

} // namespace mt8167s_display

// clang-format off
ZIRCON_DRIVER_BEGIN(mt8167s_display, mt8167s_display::display_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_DISPLAY),
ZIRCON_DRIVER_END(mt8167s_display)
