// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/pci.h>
#include <hw/pci.h>

#include <assert.h>
#include <cpuid.h>
#include <fbl/unique_ptr.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

#include "intel-i915.h"
#include "registers.h"

#define INTEL_I915_BROADWELL_DID (0x1616)

#define INTEL_I915_REG_WINDOW_SIZE (0x1000000u)
#define INTEL_I915_FB_WINDOW_SIZE (0x10000000u)

#define BACKLIGHT_CTRL_OFFSET (0xc8250)
#define BACKLIGHT_CTRL_BIT ((uint32_t)(1u << 31))

#define FLAGS_BACKLIGHT 1

namespace i915 {

void Device::EnableBacklight(bool enable) {
    if (flags_ & FLAGS_BACKLIGHT) {
        uint32_t tmp = mmio_space_->Read32(BACKLIGHT_CTRL_OFFSET);

        if (enable) {
            tmp |= BACKLIGHT_CTRL_BIT;
        } else {
            tmp &= ~BACKLIGHT_CTRL_BIT;
        }

        mmio_space_->Write32(BACKLIGHT_CTRL_OFFSET, tmp);
    }
}

// implement display protocol

zx_status_t Device::SetMode(zx_display_info_t* info) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::GetMode(zx_display_info_t* info) {
    assert(info);
    memcpy(info, &info_, sizeof(zx_display_info_t));
    return ZX_OK;
}

zx_status_t Device::GetFramebuffer(void** framebuffer) {
    assert(framebuffer);
    *framebuffer = reinterpret_cast<void*>(framebuffer_);
    return ZX_OK;
}

void Device::Flush() {
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

// implement device protocol

zx_status_t Device::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
    EnableBacklight(true);
    return ZX_OK;
}

zx_status_t Device::DdkClose(uint32_t flags) {
    return ZX_OK;
}

void Device::DdkRelease() {
    delete this;
}

zx_status_t Device::Bind() {
    zxlogf(SPEW, "i915: binding to display controller\n");

    pci_protocol_t pci;
    if (device_get_protocol(parent_, ZX_PROTOCOL_PCI, &pci))
        return ZX_ERR_NOT_SUPPORTED;

    void* cfg_space;
    size_t config_size;
    zx_handle_t cfg_handle = ZX_HANDLE_INVALID;
    zx_status_t status = pci_map_resource(&pci, PCI_RESOURCE_CONFIG,
                                          ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                          &cfg_space, &config_size, &cfg_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: Failed to map PCI resource config\n");
        return status;
    }
    const pci_config_t* pci_config = reinterpret_cast<const pci_config_t*>(cfg_space);
    uint32_t flags = 0;
    if (pci_config->device_id == INTEL_I915_BROADWELL_DID) {
        // TODO: this should be based on the specific target
        flags |= FLAGS_BACKLIGHT;
    }

    uintptr_t gmchGfxControl =
            reinterpret_cast<uintptr_t>(cfg_space) + registers::GmchGfxControl::kAddr;
    uint16_t gmch_ctrl = *reinterpret_cast<volatile uint16_t*>(gmchGfxControl);
    uint32_t gtt_size = registers::GmchGfxControl::mem_size_to_mb(gmch_ctrl);

    zx_handle_close(cfg_handle);

    // map register window
    uintptr_t regs;
    uint64_t regs_size;
    status = pci_map_resource(&pci, PCI_RESOURCE_BAR_0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                              reinterpret_cast<void**>(&regs), &regs_size, &regs_handle_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: failed to map bar 0: %d\n", status);
        return status;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<MmioSpace> mmio_space(new (&ac) MmioSpace(regs));
    if (!ac.check()) {
        zxlogf(ERROR, "i915: failed to alloc MmioSpace\n");
        return ZX_ERR_NO_MEMORY;
    }
    mmio_space_ = fbl::move(mmio_space);

    zx_display_info_t* di = &info_;
    uint32_t format, width, height, stride;
    status = zx_bootloader_fb_get_info(&format, &width, &height, &stride);
    if (status == ZX_OK) {
        di->format = format;
        di->width = width;
        di->height = height;
        di->stride = stride;
    } else {
        di->format = ZX_PIXEL_FORMAT_RGB_565;
        di->width = 2560 / 2;
        di->height = 1700 / 2;
        di->stride = 2560 / 2;
    }
    di->flags = ZX_DISPLAY_FLAG_HW_FRAMEBUFFER;

    switch (di->format) {
    case ZX_PIXEL_FORMAT_RGB_565:
        di->pixelsize = 2;
        break;
    case ZX_PIXEL_FORMAT_RGB_x888:
    case ZX_PIXEL_FORMAT_ARGB_8888:
        di->pixelsize = 4;
        break;
    case ZX_PIXEL_FORMAT_RGB_332:
    case ZX_PIXEL_FORMAT_RGB_2220:
    case ZX_PIXEL_FORMAT_MONO_1:
    case ZX_PIXEL_FORMAT_MONO_8:
        di->pixelsize = 1;
        break;
    default:
        zxlogf(ERROR, "i915: unknown format %u\n", di->format);
        return ZX_ERR_NOT_SUPPORTED;
    }

    framebuffer_size_ = stride * height * di->pixelsize;
    status = zx::vmo::create(framebuffer_size_, 0, &framebuffer_vmo_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: Failed to allocate framebuffer (%d)\n", status);
        return status;
    }

    status = framebuffer_vmo_.op_range(ZX_VMO_OP_COMMIT, 0, framebuffer_size_, nullptr, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: Failed to commit VMO (%d)\n", status);
        return status;
    }

    status = zx::vmar::root_self().map(0, framebuffer_vmo_, 0, framebuffer_size_,
                                       ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &framebuffer_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: Failed to map framebuffer (%d)\n", status);
        return status;
    }

    gtt_.Init(mmio_space_.get(), gtt_size);

    uint32_t fb_gfx_addr;
    if (!gtt_.Insert(mmio_space_.get(), &framebuffer_vmo_, framebuffer_size_,
                     registers::PlaneSurface::kTrailingPtePadding, &fb_gfx_addr)) {
        zxlogf(ERROR, "i915: Failed to allocate gfx address for framebuffer\n");
        return ZX_ERR_INTERNAL;
    }

    // TODO(ZX-1413): set the stride and format
    auto plane_surface = registers::PlaneSurface::Get().ReadFrom(mmio_space_.get());
    plane_surface.surface_base_addr().set(fb_gfx_addr >> plane_surface.kRShiftCount);
    plane_surface.WriteTo(mmio_space_.get());

    // TODO remove when the gfxconsole moves to user space
    EnableBacklight(true);
    zx_set_framebuffer(get_root_resource(), reinterpret_cast<void*>(framebuffer_),
                       static_cast<uint32_t>(framebuffer_size_),
                       format, width, height, stride);

    status = DdkAdd("intel_i915_disp", flags);
    if (status != ZX_OK) {
        return status;
    }

    zxlogf(SPEW, "i915: reg=%08lx regsize=0x%" PRIx64 " fb=0x%" PRIx64 "fbsize=0x%d\n",
            regs, regs_size, framebuffer_, framebuffer_size_);

    return ZX_OK;
}

Device::Device(zx_device_t* parent) : DeviceType(parent) { }

Device::~Device() {
    if (mmio_space_) {
        EnableBacklight(false);

        zx_handle_close(regs_handle_);
        regs_handle_ = ZX_HANDLE_INVALID;
    }
    if (framebuffer_) {
        zx::vmar::root_self().unmap(framebuffer_, framebuffer_size_);
    }
}

} // namespace i915

zx_status_t intel_i915_bind(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<i915::Device> device(new (&ac) i915::Device(parent));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device->Bind();
    if (status == ZX_OK) {
        device.release();
    }
    return status;
}
