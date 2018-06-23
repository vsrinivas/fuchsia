// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <ddktl/protocol/display-controller.h>
#include <hw/pci.h>

#include <assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "simple-display.h"

// implement display controller protocol
static constexpr uint64_t kDisplayId = 1;

static void* const kImageHandle = reinterpret_cast<void*>(0xdecafc0ffee);

void SimpleDisplay::SetDisplayControllerCb(void* cb_ctx, display_controller_cb_t* cb) {
    cb_ctx_ = cb_ctx;
    cb_ = cb;

    uint64_t display_id = kDisplayId;
    cb->on_displays_changed(cb_ctx, &display_id, 1, nullptr, 0);
}

zx_status_t SimpleDisplay::GetDisplayInfo(uint64_t display_id, display_info_t* info) {
    if (display_id != kDisplayId) {
        return ZX_ERR_INVALID_ARGS;
    }

    info->edid_present = false;
    info->panel.params.height = height_;
    info->panel.params.width = width_;
    info->panel.params.refresh_rate_e2 = 3000; // Just guess that it's 30fps
    info->pixel_formats = &format_;
    info->pixel_format_count = 1;

    return ZX_OK;
}

zx_status_t SimpleDisplay::ImportVmoImage(image_t* image, const zx::vmo& vmo, size_t offset) {
    zx_info_handle_basic_t import_info;
    size_t actual, avail;
    zx_status_t status = vmo.get_info(ZX_INFO_HANDLE_BASIC,
                                      &import_info, sizeof(import_info), &actual, &avail);
    if (status != ZX_OK) {
        return status;
    }
    if (import_info.koid != framebuffer_koid_) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (image->width != width_ || image->height != height_
            || image->pixel_format != format_ || offset != 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    image->handle = kImageHandle;
    return ZX_OK;
}

void SimpleDisplay::ReleaseImage(image_t* image) {
    // noop
}

void SimpleDisplay::CheckConfiguration(const display_config_t** display_configs,
                                       uint32_t** layer_cfg_results,
                                       uint32_t display_count) {
    if (display_count != 1) {
        ZX_DEBUG_ASSERT(display_count == 0);
        return;
    }
    ZX_DEBUG_ASSERT(display_configs[0]->display_id == kDisplayId);
    bool success;
    if (display_configs[0]->layer_count != 1) {
        success = false;
    } else {
        primary_layer_t* layer = &display_configs[0]->layers[0]->cfg.primary;
        frame_t frame = {
                .x_pos = 0, .y_pos = 0, .width = width_, .height = height_,
        };
        success = display_configs[0]->layers[0]->type == LAYER_PRIMARY
                && layer->transform_mode == FRAME_TRANSFORM_IDENTITY
                && layer->image.width == width_
                && layer->image.height == height_
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
}

void SimpleDisplay::ApplyConfiguration(const display_config_t** display_config,
                                       uint32_t display_count) {
    bool has_image = display_count != 0 && display_config[0]->layer_count != 0;
    void* handles[] = { kImageHandle };
    if (cb_) {
        cb_->on_display_vsync(cb_ctx_, kDisplayId, handles, has_image);
    }
}

uint32_t SimpleDisplay::ComputeLinearStride(uint32_t width, zx_pixel_format_t format) {
    return (width == width_ && format == format_) ? stride_ : 0;
}

zx_status_t SimpleDisplay::AllocateVmo(uint64_t size, zx_handle_t* vmo_out) {
    zx_info_handle_count handle_count;
    size_t actual, avail;
    zx_status_t status = framebuffer_handle_.get_info(ZX_INFO_HANDLE_COUNT, &handle_count,
                                                      sizeof(handle_count), &actual, &avail);
    if (status != ZX_OK) {
        return status;
    }
    if (handle_count.handle_count != 1) {
        return ZX_ERR_NO_RESOURCES;
    }
    if (size > height_ * stride_ * ZX_PIXEL_FORMAT_BYTES(format_)) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return zx_handle_duplicate(framebuffer_handle_.get(), ZX_RIGHT_SAME_RIGHTS, vmo_out);
}

// implement device protocol

void SimpleDisplay::DdkUnbind() {
    DdkRemove();
}

void SimpleDisplay::DdkRelease() {
    delete this;
}

// implement driver object:

zx_status_t SimpleDisplay::Bind(const char* name, fbl::unique_ptr<SimpleDisplay>* vbe_ptr) {
    zx_info_handle_basic_t framebuffer_info;
    size_t actual, avail;
    zx_status_t status = framebuffer_handle_.get_info(
            ZX_INFO_HANDLE_BASIC, &framebuffer_info, sizeof(framebuffer_info), &actual, &avail);
    if (status != ZX_OK) {
        printf("%s: failed to id framebuffer: %d\n", name, status);
        return status;
    }
    framebuffer_koid_ = framebuffer_info.koid;

    status = DdkAdd(name);
    if (status != ZX_OK) {
        return status;
    }
    // DevMgr now owns this pointer, release it to avoid destroying the object
    // when device goes out of scope.
    __UNUSED auto ptr = vbe_ptr->release();

    zxlogf(INFO, "%s: initialized display, %u x %u (stride=%u format=%08x)\n",
           name, width_, height_, stride_, format_);

    return ZX_OK;
}

SimpleDisplay::SimpleDisplay(zx_device_t* parent, zx_handle_t vmo,
                             uint32_t width, uint32_t height,
                             uint32_t stride, zx_pixel_format_t format)
        : DeviceType(parent), framebuffer_handle_(vmo),
          width_(width), height_(height), stride_(stride), format_(format) { }

zx_status_t bind_simple_pci_display_bootloader(zx_device_t* dev, const char* name, uint32_t bar) {
    uint32_t format, width, height, stride;
    zx_status_t status = zx_framebuffer_get_info(get_root_resource(), &format,
                                                 &width, &height, &stride);
    if (status != ZX_OK) {
        printf("%s: failed to get bootloader dimensions: %d\n", name, status);
        return ZX_ERR_NOT_SUPPORTED;
    }

    return bind_simple_pci_display(dev, name, bar, width, height, stride, format);
}

zx_status_t bind_simple_pci_display(zx_device_t* dev, const char* name, uint32_t bar,
                                    uint32_t width, uint32_t height,
                                    uint32_t stride, zx_pixel_format_t format) {
    pci_protocol_t pci;
    zx_status_t status;
    if (device_get_protocol(dev, ZX_PROTOCOL_PCI, &pci)) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    void* framebuffer;
    uint64_t framebuffer_size;
    zx_handle_t framebuffer_handle;
    // map framebuffer window
    status = pci_map_bar(&pci, bar, ZX_CACHE_POLICY_WRITE_COMBINING,
                         &framebuffer, &framebuffer_size, &framebuffer_handle);
    if (status != ZX_OK) {
        printf("%s: failed to map pci bar %d: %d\n", name, bar, status);
        return status;
    }
    zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t) framebuffer, framebuffer_size);

    fbl::AllocChecker ac;
    fbl::unique_ptr<SimpleDisplay> display(
            new (&ac) SimpleDisplay(dev, framebuffer_handle, width, height, stride, format));
    if (!ac.check()) {
        zx_handle_close(framebuffer_handle);
        return ZX_ERR_NO_MEMORY;
    }

    return display->Bind(name, &display);
}
