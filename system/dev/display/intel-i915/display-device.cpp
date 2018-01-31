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
#include "macros.h"
#include "registers.h"
#include "registers-dpll.h"
#include "registers-transcoder.h"

#define USE_FB_TEST_PATTERN 0

namespace i915 {

DisplayDevice::DisplayDevice(Controller* controller, registers::Ddi ddi, registers::Pipe pipe)
        : DisplayDeviceType(controller->zxdev()), controller_(controller)
        , ddi_(ddi), pipe_(pipe) {}

DisplayDevice::~DisplayDevice() {
    if (inited_) {
        ResetPipe();
        ResetDdi();
    }
    if (framebuffer_) {
        zx::vmar::root_self().unmap(framebuffer_, framebuffer_size_);
    }
}

hwreg::RegisterIo* DisplayDevice::mmio_space() const {
    return controller_->mmio_space();
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

bool DisplayDevice::EnablePowerWell2() {
    // Enable Power Wells
    auto power_well = registers::PowerWellControl2::Get().ReadFrom(mmio_space());
    power_well.set_power_well_2_request(1);
    power_well.WriteTo(mmio_space());

    // Wait for PWR_WELL_CTL Power Well 2 state and distribution status
    power_well.ReadFrom(mmio_space());
    if (!WAIT_ON_US(registers::PowerWellControl2
            ::Get().ReadFrom(mmio_space()).power_well_2_state(), 20)) {
        zxlogf(ERROR, "i915: failed to enable Power Well 2\n");
        return false;
    }
    if (!WAIT_ON_US(registers::FuseStatus
            ::Get().ReadFrom(mmio_space()).pg2_dist_status(), 1)) {
        zxlogf(ERROR, "i915: Power Well 2 distribution failed\n");
        return false;
    }
    return true;
}

bool DisplayDevice::ResetPipe() {
    return controller_->ResetPipe(pipe_);
}

bool DisplayDevice::ResetDdi() {
    return controller_->ResetDdi(ddi_);
}

bool DisplayDevice::Init() {
    if (!Init(&info_)) {
        return false;
    }
    inited_ = true;

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

    fb_gfx_addr_ = controller_->gtt()->Insert(&framebuffer_vmo_, framebuffer_size_,
                                              registers::PlaneSurface::kLinearAlignment,
                                              registers::PlaneSurface::kTrailingPtePadding);
    if (!fb_gfx_addr_) {
        zxlogf(ERROR, "i915: Failed to allocate gfx address for framebuffer\n");
        return false;
    }

    registers::PipeRegs pipe_regs(pipe());

#if USE_FB_TEST_PATTERN
    // Fill the framebuffer with a r/g/b/white checkered pattern. Note that the pattern
    // will be overwritten as soon as any client draws to the framebuffer.
    uint32_t* fb = reinterpret_cast<uint32_t*>(framebuffer_);
    for (unsigned y = 0; y < info_.height; y++) {
        for (unsigned x = 0; x < info_.width; x++) {
            uint32_t colors[4] = { 0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff };
            uint32_t y_offset = (y / 12) % fbl::count_of(colors);
            uint32_t color = colors[(y_offset + (x / 24)) % fbl::count_of(colors)];
            *(fb + (y * info_.stride) + x) = color;
        }
    }
#else
    memset(reinterpret_cast<void*>(framebuffer_), 0xff, framebuffer_size_);
#endif // USE_FB_TEST_PATTERN
    Flush();

    auto plane_stride = pipe_regs.PlaneSurfaceStride().ReadFrom(controller_->mmio_space());
    plane_stride.set_stride(info_.stride / registers::PlaneSurfaceStride::kLinearStrideChunkSize);
    plane_stride.WriteTo(controller_->mmio_space());

    auto plane_surface = pipe_regs.PlaneSurface().ReadFrom(controller_->mmio_space());
    plane_surface.set_surface_base_addr(
            static_cast<uint32_t>(fb_gfx_addr_->base() >> plane_surface.kRShiftCount));
    plane_surface.WriteTo(controller_->mmio_space());

    return true;
}

} // namespace i915
