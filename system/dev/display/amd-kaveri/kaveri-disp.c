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
#include <inttypes.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// simple framebuffer device to match against an AMD Kaveri R7 device already
// initialized from EFI
#define AMD_GFX_VID (0x1002)
#define AMD_KAVERI_R7_DID (0x130f)

typedef struct kaveri_disp_device {
    void* regs;
    uint64_t regs_size;
    mx_handle_t regs_handle;

    void* framebuffer;
    uint64_t framebuffer_size;
    mx_handle_t framebuffer_handle;

    mx_display_info_t info;
} kaveri_disp_device_t;

// implement display protocol
static mx_status_t kaveri_disp_set_mode(void* ctx, mx_display_info_t* info) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t kaveri_disp_get_mode(void* ctx, mx_display_info_t* info) {
    assert(info);
    kaveri_disp_device_t* device = ctx;

    memcpy(info, &device->info, sizeof(mx_display_info_t));
    return MX_OK;
}

static mx_status_t kaveri_disp_get_framebuffer(void* ctx, void** framebuffer) {
    assert(framebuffer);
    kaveri_disp_device_t* device = ctx;

    (*framebuffer) = device->framebuffer;
    return MX_OK;
}

static display_protocol_ops_t kaveri_disp_display_proto = {
    .set_mode = kaveri_disp_set_mode,
    .get_mode = kaveri_disp_get_mode,
    .get_framebuffer = kaveri_disp_get_framebuffer,
};

// implement device protocol

static void kaveri_disp_release(void* ctx) {
    kaveri_disp_device_t* device = ctx;

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

static mx_protocol_device_t kaveri_disp_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = kaveri_disp_release,
};

// implement driver object:

static mx_status_t kaveri_disp_bind(void* ctx, mx_device_t* dev, void** cookie) {
    pci_protocol_t pci;
    mx_status_t status;

    if (device_get_protocol(dev, MX_PROTOCOL_PCI, &pci))
        return MX_ERR_NOT_SUPPORTED;

    // map resources and initialize the device
    kaveri_disp_device_t* device = calloc(1, sizeof(kaveri_disp_device_t));
    if (!device)
        return MX_ERR_NO_MEMORY;

    // map register window
    // seems to be bar 5
    status = pci_map_resource(&pci, PCI_RESOURCE_BAR_5, MX_CACHE_POLICY_UNCACHED_DEVICE,
                              &device->regs, &device->regs_size, &device->regs_handle);
    if (status != MX_OK) {
        printf("kaveri: failed to map pci bar 5: %d\n", status);
        goto fail;
    }

    // map framebuffer window
    // seems to be bar 0
    status = pci_map_resource(&pci, PCI_RESOURCE_BAR_0, MX_CACHE_POLICY_WRITE_COMBINING,
                              &device->framebuffer,
                              &device->framebuffer_size,
                              &device->framebuffer_handle);
    if (status != MX_OK) {
        printf("kaveri-disp: failed to map pci bar 0: %d\n", status);
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
        status = MX_ERR_NOT_SUPPORTED;
        goto fail;
    }
    di->flags = MX_DISPLAY_FLAG_HW_FRAMEBUFFER;

    mx_set_framebuffer(get_root_resource(), device->framebuffer, device->framebuffer_size,
                       format, width, height, stride);


    // create and add the display (char) device
   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "amd_kaveri_disp",
        .ctx = device,
        .ops = &kaveri_disp_device_proto,
        .proto_id = MX_PROTOCOL_DISPLAY,
        .proto_ops = &kaveri_disp_display_proto,
    };

    status = device_add(dev, &args, NULL);
    if (status != MX_OK) {
        goto fail;
    }

    printf("initialized amd kaveri R7 display driver, reg=%p regsize=0x%" PRIx64 " fb=%p fbsize=0x%" PRIx64 "\n",
           device->regs, device->regs_size, device->framebuffer, device->framebuffer_size);
    printf("\twidth %u height %u stride %u format %u\n",
           device->info.width, device->info.height, device->info.stride, device->info.format);

    return MX_OK;

fail:
    free(device);
    return status;
}

static mx_driver_ops_t kaveri_disp_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = kaveri_disp_bind,
};

// clang-format off
MAGENTA_DRIVER_BEGIN(kaveri_disp, kaveri_disp_driver_ops, "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, AMD_GFX_VID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, AMD_KAVERI_R7_DID),
MAGENTA_DRIVER_END(kaveri_disp)
