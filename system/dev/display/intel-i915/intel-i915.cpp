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
#include <fbl/unique_ptr.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

#include "bootloader-display.h"
#include "intel-i915.h"
#include "registers.h"

#define INTEL_I915_BROADWELL_DID (0x1616)

#define INTEL_I915_REG_WINDOW_SIZE (0x1000000u)
#define INTEL_I915_FB_WINDOW_SIZE (0x10000000u)

#define BACKLIGHT_CTRL_OFFSET (0xc8250)
#define BACKLIGHT_CTRL_BIT ((uint32_t)(1u << 31))

#define FLAGS_BACKLIGHT 1

namespace i915 {

void Controller::EnableBacklight(bool enable) {
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

zx_status_t Controller::InitDisplays() {
    fbl::AllocChecker ac;
    fbl::unique_ptr<DisplayDevice> disp_device(new (&ac) BootloaderDisplay(this));
    if (!ac.check()) {
        zxlogf(ERROR, "i915: failed to alloc disp_device\n");
        return ZX_ERR_NO_MEMORY;
    }

    if (!disp_device->Init()) {
        zxlogf(ERROR, "i915: failed to init display\n");
        return ZX_ERR_INTERNAL;
    }

    zx_status_t status = disp_device->DdkAdd("intel_i915_disp");
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: failed to add display device\n");
        return status;
    }

    display_device_ = disp_device.release();
    return ZX_OK;
}

void Controller::DdkUnbind() {
    if (display_device_) {
        device_remove(display_device_->zxdev());
        display_device_ = nullptr;
    }
    device_remove(zxdev());
}

void Controller::DdkRelease() {
    delete this;
}

zx_status_t Controller::Bind() {
    zxlogf(SPEW, "i915: binding to display controller\n");

    pci_protocol_t pci;
    if (device_get_protocol(parent_, ZX_PROTOCOL_PCI, &pci)) {
        return ZX_ERR_NOT_SUPPORTED;
    }

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
    if (pci_config->device_id == INTEL_I915_BROADWELL_DID) {
        // TODO: this should be based on the specific target
        flags_ |= FLAGS_BACKLIGHT;
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

    gtt_.Init(mmio_space_.get(), gtt_size);

    status = DdkAdd("intel_i915");
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: failed to add controller device\n");
        return status;
    }

    status = InitDisplays();
    if (status != ZX_OK) {
        device_remove(zxdev());
        return status;
    }

    // TODO remove when the gfxconsole moves to user space
    EnableBacklight(true);
    zx_set_framebuffer(get_root_resource(),
                       reinterpret_cast<void*>(display_device_->framebuffer()),
                       static_cast<uint32_t>(display_device_->framebuffer_size()),
                       display_device_->info().format, display_device_->info().width,
                       display_device_->info().height, display_device_->info().stride);

    zxlogf(SPEW, "i915: reg=%08lx regsize=0x%" PRIx64 " fb=0x%" PRIx64 "fbsize=0x%d\n",
            regs, regs_size, display_device_->framebuffer(), display_device_->framebuffer_size());

    return ZX_OK;
}

Controller::Controller(zx_device_t* parent) : DeviceType(parent) { }

Controller::~Controller() {
    if (mmio_space_) {
        EnableBacklight(false);

        zx_handle_close(regs_handle_);
        regs_handle_ = ZX_HANDLE_INVALID;
    }
}

} // namespace i915

zx_status_t intel_i915_bind(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<i915::Controller> controller(new (&ac) i915::Controller(parent));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = controller->Bind();
    if (status == ZX_OK) {
        controller.release();
    }
    return status;
}
