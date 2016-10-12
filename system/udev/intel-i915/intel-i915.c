// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/pci.h>
#include <hw/pci.h>

#include <assert.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INTEL_I915_VID (0x8086)
#define INTEL_I915_BROADWELL_DID (0x1616)

#define INTEL_I915_REG_WINDOW_SIZE (0x1000000u)
#define INTEL_I915_FB_WINDOW_SIZE (0x10000000u)

#define BACKLIGHT_CTRL_OFFSET (0xc8250)
#define BACKLIGHT_CTRL_BIT ((uint32_t)(1u << 31))

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

typedef struct intel_i915_device {
    mx_device_t device;
    void* regs;
    uint64_t regs_size;
    mx_handle_t regs_handle;

    void* framebuffer;
    uint64_t framebuffer_size;
    mx_handle_t framebuffer_handle;

    mx_display_info_t info;
    uint32_t flags;
} intel_i915_device_t;

#define FLAGS_BACKLIGHT 1

#define get_i915_device(dev) containerof(dev, intel_i915_device_t, device)

static void intel_i915_enable_backlight(intel_i915_device_t* dev, bool enable) {
    if (dev->flags & FLAGS_BACKLIGHT) {
        void* backlight_ctrl = (uint8_t*)dev->regs + BACKLIGHT_CTRL_OFFSET;
        uint32_t tmp = pcie_read32(backlight_ctrl);

        if (enable)
            tmp |= BACKLIGHT_CTRL_BIT;
        else
            tmp &= ~BACKLIGHT_CTRL_BIT;

        pcie_write32(backlight_ctrl, tmp);
    }
}

// implement display protocol

static mx_status_t intel_i915_set_mode(mx_device_t* dev, mx_display_info_t* info) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t intel_i915_get_mode(mx_device_t* dev, mx_display_info_t* info) {
    assert(info);
    intel_i915_device_t* device = get_i915_device(dev);
    memcpy(info, &device->info, sizeof(mx_display_info_t));
    return NO_ERROR;
}

static mx_status_t intel_i915_get_framebuffer(mx_device_t* dev, void** framebuffer) {
    assert(framebuffer);
    intel_i915_device_t* device = get_i915_device(dev);
    (*framebuffer) = device->framebuffer;
    return NO_ERROR;
}

static mx_display_protocol_t intel_i915_display_proto = {
    .set_mode = intel_i915_set_mode,
    .get_mode = intel_i915_get_mode,
    .get_framebuffer = intel_i915_get_framebuffer,
};

// implement device protocol

static mx_status_t intel_i915_open(mx_device_t* dev, mx_device_t** out, uint32_t flags) {
    intel_i915_device_t* device = get_i915_device(dev);
    intel_i915_enable_backlight(device, true);
    return NO_ERROR;
}

static mx_status_t intel_i915_close(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_status_t intel_i915_release(mx_device_t* dev) {
    intel_i915_device_t* device = get_i915_device(dev);
    intel_i915_enable_backlight(device, false);

    if (device->regs) {
        mx_handle_close(device->regs_handle);
        device->regs_handle = -1;
    }

    if (device->framebuffer) {
        mx_handle_close(device->framebuffer_handle);
        device->framebuffer_handle = -1;
    }

    return NO_ERROR;
}

static mx_protocol_device_t intel_i915_device_proto = {
    .open = intel_i915_open,
    .close = intel_i915_close,
    .release = intel_i915_release,
};

// implement driver object:

static mx_status_t intel_i915_bind(mx_driver_t* drv, mx_device_t* dev) {
    pci_protocol_t* pci;
    if (device_get_protocol(dev, MX_PROTOCOL_PCI, (void**)&pci))
        return ERR_NOT_SUPPORTED;

    mx_status_t status = pci->claim_device(dev);
    if (status < 0)
        return status;

    // map resources and initialize the device
    intel_i915_device_t* device = calloc(1, sizeof(intel_i915_device_t));
    if (!device)
        return ERR_NO_MEMORY;

    const pci_config_t* pci_config;
    mx_handle_t cfg_handle = pci->get_config(dev, &pci_config);
    if (cfg_handle >= 0) {
        if (pci_config->device_id == INTEL_I915_BROADWELL_DID) {
            // TODO: this should be based on the specific target
            device->flags |= FLAGS_BACKLIGHT;
        }
        mx_handle_close(cfg_handle);
    }

    // map register window
    device->regs_handle = pci->map_mmio(dev, 0, MX_CACHE_POLICY_UNCACHED_DEVICE,
                                        &device->regs, &device->regs_size);
    if (device->regs_handle < 0) {
        status = device->regs_handle;
        goto fail;
    }

    // map framebuffer window
    device->framebuffer_handle = pci->map_mmio(dev, 2, MX_CACHE_POLICY_WRITE_COMBINING,
                                               &device->framebuffer,
                                               &device->framebuffer_size);
    if (device->framebuffer_handle < 0) {
        status = device->framebuffer_handle;
        goto fail;
    }

    // create and add the display (char) device
    device_init(&device->device, drv, "intel_i915_disp", &intel_i915_device_proto);

    mx_display_info_t* di = &device->info;
    uint32_t format, width, height, stride;
    status = mx_bootloader_fb_get_info(&format, &width, &height, &stride);
    if (status == NO_ERROR) {
        di->format = format;
        di->width = width;
        di->height = height;
        di->stride = stride;
    } else {
        di->format = MX_PIXEL_FORMAT_RGB_565;
        di->width = 2560 / 2;
        di->height = 1700 / 2;
        di->stride = 2560 / 2;
    }
    di->flags = MX_DISPLAY_FLAG_HW_FRAMEBUFFER;

    // TODO remove when the gfxconsole moves to user space
    intel_i915_enable_backlight(device, true);
    mx_set_framebuffer(get_root_resource(), device->framebuffer, device->framebuffer_size,
                       format, width, height, stride);

    device->device.protocol_id = MX_PROTOCOL_DISPLAY;
    device->device.protocol_ops = &intel_i915_display_proto;
    device_add(&device->device, dev);

    xprintf("initialized intel i915 display driver, reg=%p regsize=0x%llx fb=%p fbsize=0x%llx\n",
            device->regs, device->regs_size, device->framebuffer, device->framebuffer_size);

    return NO_ERROR;
fail:
    free(device);
    return status;
}

mx_driver_t _driver_intel_i915 = {
    .ops = {
        .bind = intel_i915_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_intel_i915, "intel-i915-display", "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, INTEL_I915_VID),
    BI_MATCH_IF(EQ, BIND_PCI_CLASS, 0x3), // Display class
MAGENTA_DRIVER_END(_driver_intel_i915)
