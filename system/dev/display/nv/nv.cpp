// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/pci.h>
#include <fbl/unique_ptr.h>
#include <hw/pci.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

// simple framebuffer device to match against an Nvidia display controller already
// initialized from EFI
struct nv_disp_device {
    void* regs;
    uint64_t regs_size;
    zx_handle_t regs_handle;

    void* framebuffer;
    uint64_t framebuffer_size;
    zx_handle_t framebuffer_handle;

    zx_display_info_t info;
};

// implement display protocol
static zx_status_t nv_disp_set_mode(void* ctx, zx_display_info_t* info) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t nv_disp_get_mode(void* ctx, zx_display_info_t* info) {
    nv_disp_device* device = static_cast<nv_disp_device*>(ctx);

    assert(info);

    memcpy(info, &device->info, sizeof(zx_display_info_t));
    return ZX_OK;
}

static zx_status_t nv_disp_get_framebuffer(void* ctx, void** framebuffer) {
    nv_disp_device* device = static_cast<nv_disp_device*>(ctx);

    assert(framebuffer);

    (*framebuffer) = device->framebuffer;
    return ZX_OK;
}

// implement device protocol

static void nv_disp_release(void* ctx) {
    nv_disp_device* device = static_cast<nv_disp_device*>(ctx);

    if (device->regs) {
        zx_handle_close(device->regs_handle);
        device->regs_handle = -1;
    }

    if (device->framebuffer) {
        zx_handle_close(device->framebuffer_handle);
        device->framebuffer_handle = -1;
    }

    delete device;
}

// implement driver object:

extern "C" zx_status_t nv_disp_bind(void* ctx, zx_device_t* dev) {
    pci_protocol_t pci;
    zx_status_t status;

    status = device_get_protocol(dev, ZX_PROTOCOL_PCI, &pci);
    if (status != ZX_OK) {
        return status;
    }

    // map resources and initialize the device
    fbl::unique_ptr<nv_disp_device> device(new nv_disp_device());
    if (!device) {
        return ZX_ERR_NO_MEMORY;
    }

    // map register window
    // seems to be bar 0
    status = pci_map_bar(&pci, 0u, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                         &device->regs, &device->regs_size, &device->regs_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "nv: failed to map pci bar 5: %d\n", status);
        return status;
    }

    // map framebuffer window
    // seems to be bar 1
    status = pci_map_bar(&pci, 1u, ZX_CACHE_POLICY_WRITE_COMBINING,
                         &device->framebuffer,
                         &device->framebuffer_size,
                         &device->framebuffer_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "nv: failed to map pci bar 0: %d\n", status);
        return status;
    }

    zx_display_info_t* di = &device->info;
    uint32_t format, width, height, stride;
    status = zx_bootloader_fb_get_info(&format, &width, &height, &stride);
    if (status == ZX_OK) {
        di->format = format;
        di->width = width;
        di->height = height;
        di->stride = stride;
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }
    di->flags = ZX_DISPLAY_FLAG_HW_FRAMEBUFFER;

    zx_set_framebuffer(get_root_resource(), device->framebuffer, static_cast<uint32_t>(device->framebuffer_size),
                       format, width, height, stride);

    static display_protocol_ops_t nv_disp_display_proto = {};
    nv_disp_display_proto.set_mode = nv_disp_set_mode;
    nv_disp_display_proto.get_mode = nv_disp_get_mode;
    nv_disp_display_proto.get_framebuffer = nv_disp_get_framebuffer;

    static zx_protocol_device_t nv_disp_device_proto = {};
    nv_disp_device_proto.version = DEVICE_OPS_VERSION;
    nv_disp_device_proto.release = nv_disp_release;

    // create and add the display (char) device
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "nv_disp";
    args.ctx = reinterpret_cast<void*>(device.get());
    args.ops = &nv_disp_device_proto;
    args.proto_id = ZX_PROTOCOL_DISPLAY;
    args.proto_ops = &nv_disp_display_proto;

    status = device_add(dev, &args, NULL);
    if (status != ZX_OK) {
        return status;
    }

    zxlogf(INFO, "nv: initialized nv display driver, reg=%p regsize=0x%" PRIx64 " fb=%p fbsize=0x%" PRIx64 "\n",
           device->regs, device->regs_size, device->framebuffer, device->framebuffer_size);
    zxlogf(INFO, "nv:   width %u height %u stride %u format %u\n",
           device->info.width, device->info.height, device->info.stride, device->info.format);

    // drop the reference to this pointer, device_add owns it now
    __UNUSED nv_disp_device* d = device.release();

    return ZX_OK;
}
