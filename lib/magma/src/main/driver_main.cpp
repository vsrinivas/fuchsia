// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/pci.h>
#include <hw/pci.h>

int devhost_init(void);
int devhost_cmdline(int argc, char** argv);
int devhost_start(void);
}

#include <magenta/types.h>
#include <thread>

#include "magma_util/sleep.h"
#include "sys_driver/magma_driver.h"

#define MAGMA_START 1

#ifndef MAGMA_CREATE_DEVICE
#define MAGMA_CREATE_DEVICE 1
#endif

#if MAGMA_INDRIVER_TEST
void magma_indriver_test(mx_device_t* device);
#endif

#define INTEL_I915_VID (0x8086)
#define INTEL_I915_BROADWELL_DID (0x1616)
#define INTEL_I915_SKYLAKE_DID (0x1916)

#define INTEL_I915_REG_WINDOW_SIZE (0x1000000u)
#define INTEL_I915_FB_WINDOW_SIZE (0x10000000u)

#define TRACE 1

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...)                                                                            \
    do {                                                                                           \
    } while (0)
#endif

static int magma_hook(void* dev);

typedef struct intel_i915_device {
    mx_device_t device;
    mx_device_t* parent_device;

    void* framebuffer;
    uint64_t framebuffer_size;
    mx_handle_t framebuffer_handle;

    mx_display_info_t info;
    uint32_t flags;

    MagmaDriver* magma_driver;
    MagmaSystemDevice* magma_system_device;

} intel_i915_device_t;

#define get_i915_device(dev) containerof(dev, intel_i915_device_t, device)

static void intel_i915_enable_backlight(intel_i915_device_t* dev, bool enable)
{
    // Take action on backlight here for certain platforms as necessary.
}

// implement display protocol

static mx_status_t intel_i915_set_mode(mx_device_t* dev, mx_display_info_t* info)
{
    return ERR_NOT_SUPPORTED;
}

static mx_status_t intel_i915_get_mode(mx_device_t* dev, mx_display_info_t* info)
{
    assert(info);
    intel_i915_device_t* device = get_i915_device(dev);
    memcpy(info, &device->info, sizeof(mx_display_info_t));
    return NO_ERROR;
}

static mx_status_t intel_i915_get_framebuffer(mx_device_t* dev, void** framebuffer)
{
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

static mx_status_t intel_i915_open(mx_device_t* dev, mx_device_t** out, uint32_t flags)
{
    intel_i915_device_t* device = get_i915_device(dev);
    intel_i915_enable_backlight(device, true);
    return NO_ERROR;
}

static ssize_t intel_i915_ioctl(mx_device_t* mx_device, uint32_t op, const void* in_buf,
                                size_t in_len, void* out_buf, size_t out_len)
{
    intel_i915_device_t* device = get_i915_device(mx_device);
    DASSERT(device->magma_system_device);

    ssize_t result = ERR_NOT_SUPPORTED;

    switch (op) {
    case 0: {
        auto device_id_out = reinterpret_cast<uint32_t*>(out_buf);
        if (!out_buf || out_len < sizeof(*device_id_out))
            return ERR_INVALID_ARGS;
        *device_id_out = device->magma_system_device->GetDeviceId();
        result = sizeof(*device_id_out);
        break;
    }
    case 1: {
        auto device_handle_out = reinterpret_cast<uint32_t*>(out_buf);
        if (!out_buf || out_len < sizeof(*device_handle_out))
            return ERR_INVALID_ARGS;
        *device_handle_out = 0xdeadbeef;
        result = sizeof(*device_handle_out);
        break;
    }
    }
    xprintf("intel_i915_ioctl op 0x%x returning %zd\n", op, result);

    return result;
}

static mx_status_t intel_i915_close(mx_device_t* dev, uint32_t flags) { return NO_ERROR; }

static mx_status_t intel_i915_release(mx_device_t* dev)
{
    intel_i915_device_t* device = get_i915_device(dev);
    intel_i915_enable_backlight(device, false);

    if (device->framebuffer) {
        mx_handle_close(device->framebuffer_handle);
        device->framebuffer_handle = -1;
    }

    return NO_ERROR;
}

static mx_protocol_device_t intel_i915_device_proto = {
    .open = intel_i915_open,
    .close = intel_i915_close,
    .ioctl = intel_i915_ioctl,
    .release = intel_i915_release,
};

// implement driver object:

static mx_status_t intel_i915_bind(mx_driver_t* drv, mx_device_t* dev)
{
    xprintf("intel_i915_bind start mx_device %p\n", dev);

    pci_protocol_t* pci;
    if (device_get_protocol(dev, MX_PROTOCOL_PCI, (void**)&pci))
        return ERR_NOT_SUPPORTED;

    mx_status_t status = pci->claim_device(dev);
    if (status < 0)
        return status;

    // map resources and initialize the device
    auto device = reinterpret_cast<intel_i915_device_t*>(calloc(1, sizeof(intel_i915_device_t)));
    if (!device)
        return ERR_NO_MEMORY;

    const pci_config_t* pci_config;
    mx_handle_t cfg_handle = pci->get_config(dev, &pci_config);
    if (cfg_handle >= 0) {
        mx_handle_close(cfg_handle);
    }

    // map framebuffer window
    // in vga mode we are scanning out from the same memory (pci bar 2) that's used by the gpu
    // memory aperture (gtt).
    // for now just redirect to offscreen the framebuffer used by the rest of the system.
    device->framebuffer_handle = MX_HANDLE_INVALID;
    if (!MAGMA_START) {
        device->framebuffer_handle = pci->map_mmio(dev, 2, MX_CACHE_POLICY_WRITE_COMBINING,
                                                   &device->framebuffer, &device->framebuffer_size);
        if (device->framebuffer_handle < 0) {
            free(device);
            return status;
        }
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
        di->format = MX_PIXEL_FORMAT_ARGB_8888;
        di->width = 2560 / 2;
        di->height = 1700 / 2;
        di->stride = 2560 / 2;
    }

    if (device->framebuffer_handle == MX_HANDLE_INVALID) {
        switch (di->format) {
        case MX_PIXEL_FORMAT_RGB_565:
            device->framebuffer_size = di->stride * di->height * sizeof(uint16_t);
            break;
        default:
            xprintf("unrecognized format 0x%x, defaulting to 32bpp", di->format);
        case MX_PIXEL_FORMAT_ARGB_8888:
        case MX_PIXEL_FORMAT_RGB_x888:
            device->framebuffer_size = di->stride * di->height * sizeof(uint32_t);
            break;
        }
        device->framebuffer = malloc(device->framebuffer_size);
    } else {
        di->flags = MX_DISPLAY_FLAG_HW_FRAMEBUFFER;
    }

    // TODO remove when the gfxconsole moves to user space
    intel_i915_enable_backlight(device, true);
    mx_set_framebuffer(get_root_resource(), device->framebuffer,
                       static_cast<uint32_t>(device->framebuffer_size), format, width, height,
                       stride);

    device->device.protocol_id = MX_PROTOCOL_DISPLAY;
    device->device.protocol_ops = &intel_i915_display_proto;
    device->parent_device = dev;
    device_add(&device->device, dev);

    xprintf("initialized magma intel display driver, fb=0x%p fbsize=0x%lx\n", device->framebuffer,
            device->framebuffer_size);

    if (MAGMA_START) {
        std::thread magma_thread(magma_hook, device);
        magma_thread.detach();
    }

    return NO_ERROR;
}

mx_driver_t _driver_intel_gen_gpu = {
    .ops =
        {
            .bind = intel_i915_bind,
        },
};

extern const magenta_driver_info_t __magenta_driver__;

// clang-format off
MAGENTA_DRIVER_BEGIN(_driver_intel_gen_gpu, "intel-gen-gpu", "magenta", "!0.1", 5)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, INTEL_I915_VID),
    BI_MATCH_IF(EQ, BIND_PCI_CLASS, 0x3), // Display class
MAGENTA_DRIVER_END(_driver_intel_gen_gpu)
    // clang-format on

    static int magma_hook(void* param)
{
    xprintf("magma_hook start\n");

    auto dev = reinterpret_cast<intel_i915_device_t*>(param);

#if MAGMA_CREATE_DEVICE
    // create and add the gpu device
    dev->magma_driver = MagmaDriver::Create();
    if (!dev->magma_driver)
        return ERR_INTERNAL;

    xprintf("Created driver %p\n", dev->magma_driver);

    dev->magma_system_device = dev->magma_driver->CreateDevice(dev->parent_device);
    if (!dev->magma_system_device) {
        xprintf("Failed to create device");
        return ERR_INTERNAL;
    }

    xprintf("Created device %p\n", dev->magma_system_device);
#endif

#if MAGMA_INDRIVER_TEST
    xprintf("running magma indriver test\n");
    magma_indriver_test(dev->parent_device);
#endif

    xprintf("magma_hook finish\n");

    return NO_ERROR;
}
