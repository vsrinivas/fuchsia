// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/pci.h>
#include <hw/pci.h>

#include <assert.h>
#include <fbl/unique_ptr.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "intel-i915.h"

#define INTEL_I915_BROADWELL_DID (0x1616)

#define INTEL_I915_REG_WINDOW_SIZE (0x1000000u)
#define INTEL_I915_FB_WINDOW_SIZE (0x10000000u)

#define BACKLIGHT_CTRL_OFFSET (0xc8250)
#define BACKLIGHT_CTRL_BIT ((uint32_t)(1u << 31))

#define FLAGS_BACKLIGHT 1

namespace i915 {

void Device::EnableBacklight(bool enable) {
    if (flags_ & FLAGS_BACKLIGHT) {
        auto* backlight_ctrl = reinterpret_cast<volatile uint32_t*>(regs_ + BACKLIGHT_CTRL_OFFSET);
        uint32_t tmp = pcie_read32(backlight_ctrl);

        if (enable) {
            tmp |= BACKLIGHT_CTRL_BIT;
        } else {
            tmp &= ~BACKLIGHT_CTRL_BIT;
        }

        pcie_write32(backlight_ctrl, tmp);
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
    *framebuffer = framebuffer_;
    return ZX_OK;
}

void Device::Flush() {
    // no-op
}

void Device::AcquireOrReleaseDisplay(bool acquire) {
    // no-op
}

void Device::SetOwnershipChangeCallback(zx_display_cb_t callback, void* cookie) {
    // no-op
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
    pci_protocol_t pci;
    if (device_get_protocol(parent_, ZX_PROTOCOL_PCI, &pci))
        return ZX_ERR_NOT_SUPPORTED;

    const pci_config_t* pci_config;
    size_t config_size;
    zx_handle_t cfg_handle = ZX_HANDLE_INVALID;
    zx_status_t status = pci_map_resource(&pci, PCI_RESOURCE_CONFIG,
                                          ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                          (void**)&pci_config,
                                          &config_size, &cfg_handle);
    uint32_t flags = 0;
    if (status == ZX_OK) {
        if (pci_config->device_id == INTEL_I915_BROADWELL_DID) {
            // TODO: this should be based on the specific target
            flags |= FLAGS_BACKLIGHT;
        }
        zx_handle_close(cfg_handle);
    }

    // map register window
    void** addr = reinterpret_cast<void**>(&regs_);
    status = pci_map_resource(&pci, PCI_RESOURCE_BAR_0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                              addr, &regs_size_, &regs_handle_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: failed to map bar 0: %d\n", status);
        return status;
    }

    // map framebuffer window
    status = pci_map_resource(&pci, PCI_RESOURCE_BAR_2, ZX_CACHE_POLICY_WRITE_COMBINING,
                              &framebuffer_,
                              &framebuffer_size_,
                              &framebuffer_handle_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: failed to map bar 2: %d\n", status);
        return status;
    }

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

    // TODO remove when the gfxconsole moves to user space
    EnableBacklight(true);
    zx_set_framebuffer(get_root_resource(), framebuffer_,
                       (uint32_t) framebuffer_size_,
                       format, width, height, stride);

    status = DdkAdd("intel_i915_disp", flags);
    if (status != ZX_OK) {
        return status;
    }

    zxlogf(SPEW, "i915: reg=%08lx regsize=0x%" PRIx64 " fb=%p fbsize=0x%" PRIx64 "\n",
            regs_, regs_size_, framebuffer_, framebuffer_size_);

    return ZX_OK;
}

Device::Device(zx_device_t* parent) : DeviceType(parent) { }

Device::~Device() {
    if (regs_) {
        EnableBacklight(false);

        zx_handle_close(regs_handle_);
        regs_handle_ = ZX_HANDLE_INVALID;
    }

    if (framebuffer_) {
        zx_handle_close(framebuffer_handle_);
        framebuffer_handle_ = ZX_HANDLE_INVALID;
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
