// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
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

static mx_status_t intel_i915_set_mode(void* ctx, mx_display_info_t* info) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t intel_i915_get_mode(void* ctx, mx_display_info_t* info) {
    assert(info);
    intel_i915_device_t* device = ctx;
    memcpy(info, &device->info, sizeof(mx_display_info_t));
    return MX_OK;
}

static mx_status_t intel_i915_get_framebuffer(void* ctx, void** framebuffer) {
    assert(framebuffer);
    intel_i915_device_t* device = ctx;
    (*framebuffer) = device->framebuffer;
    return MX_OK;
}

static display_protocol_ops_t intel_i915_display_proto = {
    .set_mode = intel_i915_set_mode,
    .get_mode = intel_i915_get_mode,
    .get_framebuffer = intel_i915_get_framebuffer,
};

// implement device protocol

static mx_status_t intel_i915_open(void* ctx, mx_device_t** out, uint32_t flags) {
    intel_i915_device_t* device = ctx;
    intel_i915_enable_backlight(device, true);
    return MX_OK;
}

static mx_status_t intel_i915_close(void* ctx, uint32_t flags) {
    return MX_OK;
}

static void intel_i915_release(void* ctx) {
    intel_i915_device_t* device = ctx;
    intel_i915_enable_backlight(device, false);

    if (device->regs) {
        mx_handle_close(device->regs_handle);
        device->regs_handle = -1;
    }

    if (device->framebuffer) {
        mx_handle_close(device->framebuffer_handle);
        device->framebuffer_handle = -1;
    }

    free(device);
}

static mx_protocol_device_t intel_i915_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .open = intel_i915_open,
    .close = intel_i915_close,
    .release = intel_i915_release,
};

// implement driver object:

static mx_status_t intel_i915_bind(void* ctx, mx_device_t* dev, void** cookie) {
    pci_protocol_t pci;
    if (device_get_protocol(dev, MX_PROTOCOL_PCI, &pci))
        return MX_ERR_NOT_SUPPORTED;

    // map resources and initialize the device
    intel_i915_device_t* device = calloc(1, sizeof(intel_i915_device_t));
    if (!device)
        return MX_ERR_NO_MEMORY;

    const pci_config_t* pci_config;
    size_t config_size;
    mx_handle_t cfg_handle = MX_HANDLE_INVALID;
    mx_status_t status = pci_map_resource(&pci, PCI_RESOURCE_CONFIG,
                                          MX_CACHE_POLICY_UNCACHED_DEVICE,
                                          (void**)&pci_config,
                                          &config_size, &cfg_handle);
    if (status == MX_OK) {
        if (pci_config->device_id == INTEL_I915_BROADWELL_DID) {
            // TODO: this should be based on the specific target
            device->flags |= FLAGS_BACKLIGHT;
        }
        mx_handle_close(cfg_handle);
    }

    // map register window
    status = pci_map_resource(&pci, PCI_RESOURCE_BAR_0, MX_CACHE_POLICY_UNCACHED_DEVICE,
                              &device->regs, &device->regs_size, &device->regs_handle);
    if (status != MX_OK) {
        printf("i915: failed to map bar 0: %d\n", status);
        goto fail;
    }

    // map framebuffer window
    status = pci_map_resource(&pci, PCI_RESOURCE_BAR_2, MX_CACHE_POLICY_WRITE_COMBINING,
                              &device->framebuffer,
                              &device->framebuffer_size,
                              &device->framebuffer_handle);
    if (status != MX_OK) {
        printf("i915: failed to map bar 2: %d\n", status);
        goto fail;
    }

    mx_display_info_t* di = &device->info;
    uint32_t format, width, height, stride;
    status = mx_bootloader_fb_get_info(&format, &width, &height, &stride);
    if (status == MX_OK) {
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

    // create and add the display (char) device
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "intel_i915_disp",
        .ctx = device,
        .ops = &intel_i915_device_proto,
        .proto_id = MX_PROTOCOL_DISPLAY,
        .proto_ops = &intel_i915_display_proto,
    };

    status = device_add(dev, &args, NULL);
    if (status != MX_OK) {
        goto fail;
    }

    xprintf("initialized intel i915 display driver, reg=%p regsize=0x%llx fb=%p fbsize=0x%llx\n",
            device->regs, device->regs_size, device->framebuffer, device->framebuffer_size);

    return MX_OK;

fail:
    free(device);
    return status;
}

static mx_driver_ops_t intel_i915_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = intel_i915_bind,
};

// clang-format off
MAGENTA_DRIVER_BEGIN(intel_i915, intel_i915_driver_ops, "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, INTEL_I915_VID),
    BI_MATCH_IF(EQ, BIND_PCI_CLASS, 0x3), // Display class
MAGENTA_DRIVER_END(intel_i915)
