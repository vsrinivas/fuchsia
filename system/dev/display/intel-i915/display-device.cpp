// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

#include <cpuid.h>
#include <string.h>

#include <zx/vmar.h>
#include <zx/vmo.h>

#include "display-device.h"
#include "intel-i915.h"
#include "registers.h"

namespace i915 {

DisplayDevice::DisplayDevice(Controller* controller)
        : DisplayDeviceType(controller->zxdev()), controller_(controller) {}

DisplayDevice::~DisplayDevice() {
    if (framebuffer_) {
        zx::vmar::root_self().unmap(framebuffer_, framebuffer_size_);
    }
}

// implement device protocol

void DisplayDevice::DdkRelease() {
    delete this;
}

// implement display protocol

zx_status_t DisplayDevice::SetMode(zx_display_info_t* info) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t DisplayDevice::GetMode(zx_display_info_t* info) {
    assert(info);
    memcpy(info, &info_, sizeof(zx_display_info_t));
    return ZX_OK;
}

zx_status_t DisplayDevice::GetFramebuffer(void** framebuffer) {
    assert(framebuffer);
    *framebuffer = reinterpret_cast<void*>(framebuffer_);
    return ZX_OK;
}

void DisplayDevice::Flush() {
    // TODO(ZX-1413): Use uncacheable memory for fb or use some zx cache primitive when available
    unsigned int a, b, c, d;
    if (!__get_cpuid(1, &a, &b, &c, &d)) {
        return;
    }
    uint64_t cacheline_size = 8 * ((b >> 8) & 0xff);

    uint8_t* p = reinterpret_cast<uint8_t*>(framebuffer_ & ~(cacheline_size - 1));
    uint8_t* end = reinterpret_cast<uint8_t*>(framebuffer_ + framebuffer_size_);

    while (p < end) {
        __builtin_ia32_clflush(p);
        p += cacheline_size;
    }
}

bool DisplayDevice::Init() {
    if (!Init(&info_)) {
        return false;
    }

    framebuffer_size_ = info_.stride * info_.height * info_.pixelsize;
    zx_status_t status = zx::vmo::create(framebuffer_size_, 0, &framebuffer_vmo_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: Failed to allocate framebuffer (%d)\n", status);
        return false;
    }

    status = framebuffer_vmo_.op_range(ZX_VMO_OP_COMMIT, 0, framebuffer_size_, nullptr, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: Failed to commit VMO (%d)\n", status);
        return false;
    }

    status = zx::vmar::root_self().map(0, framebuffer_vmo_, 0, framebuffer_size_,
                                       ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &framebuffer_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: Failed to map framebuffer (%d)\n", status);
        return false;
    }

    uint32_t fb_gfx_addr;
    if (!controller_->gtt()->Insert(controller_->mmio_space(),
                                    &framebuffer_vmo_, framebuffer_size_,
                                    registers::PlaneSurface::kTrailingPtePadding, &fb_gfx_addr)) {
        zxlogf(ERROR, "i915: Failed to allocate gfx address for framebuffer\n");
        return false;
    }

    // TODO(ZX-1413): set the stride and format
    auto plane_surface = registers::PlaneSurface::Get().ReadFrom(controller_->mmio_space());
    plane_surface.surface_base_addr().set(fb_gfx_addr >> plane_surface.kRShiftCount);
    plane_surface.WriteTo(controller_->mmio_space());

    return true;
}

} // namespace i915
