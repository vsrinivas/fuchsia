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
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QEMU_VGA_VID (0x1234)
#define QEMU_VGA_DID (0x1111)

typedef struct bochs_vbe_device {
    void* regs;
    uint64_t regs_size;
    zx_handle_t regs_handle;

    void* framebuffer;
    uint64_t framebuffer_size;
    zx_handle_t framebuffer_handle;

    zx_display_info_t info;
} bochs_vbe_device_t;

#define bochs_vbe_dispi_read(base, reg) pcie_read16(base + (0x500 + (reg << 1)))
#define bochs_vbe_dispi_write(base, reg, val) pcie_write16(base + (0x500 + (reg << 1)), val)

#define BOCHS_VBE_DISPI_ID 0x0
#define BOCHS_VBE_DISPI_XRES 0x1
#define BOCHS_VBE_DISPI_YRES 0x2
#define BOCHS_VBE_DISPI_BPP 0x3
#define BOCHS_VBE_DISPI_ENABLE 0x4
#define BOCHS_VBE_DISPI_BANK 0x5
#define BOCHS_VBE_DISPI_VIRT_WIDTH 0x6
#define BOCHS_VBE_DISPI_VIRT_HEIGHT 0x7
#define BOCHS_VBE_DISPI_X_OFFSET 0x8
#define BOCHS_VBE_DISPI_Y_OFFSET 0x9
#define BOCHS_VBE_DISPI_VIDEO_MEMORY_64K 0xa

static int zx_display_format_to_bpp(unsigned format) {
    unsigned bpp = ZX_PIXEL_FORMAT_BYTES(format) * 8;
    if (bpp == 0) {
        // unknown
        return -1;
    } else {
        return bpp;
    }
}

static void set_hw_mode(bochs_vbe_device_t* dev) {
    zxlogf(SPEW, "id: 0x%x\n", bochs_vbe_dispi_read(dev->regs, BOCHS_VBE_DISPI_ID));

    int bpp = zx_display_format_to_bpp(dev->info.format);
    assert(bpp >= 0);

    bochs_vbe_dispi_write(dev->regs, BOCHS_VBE_DISPI_ENABLE, 0);
    bochs_vbe_dispi_write(dev->regs, BOCHS_VBE_DISPI_BPP, bpp);
    bochs_vbe_dispi_write(dev->regs, BOCHS_VBE_DISPI_XRES, dev->info.width);
    bochs_vbe_dispi_write(dev->regs, BOCHS_VBE_DISPI_YRES, dev->info.height);
    bochs_vbe_dispi_write(dev->regs, BOCHS_VBE_DISPI_BANK, 0);
    bochs_vbe_dispi_write(dev->regs, BOCHS_VBE_DISPI_VIRT_WIDTH, dev->info.stride);
    bochs_vbe_dispi_write(dev->regs, BOCHS_VBE_DISPI_VIRT_HEIGHT, dev->framebuffer_size / dev->info.stride);
    bochs_vbe_dispi_write(dev->regs, BOCHS_VBE_DISPI_X_OFFSET, 0);
    bochs_vbe_dispi_write(dev->regs, BOCHS_VBE_DISPI_Y_OFFSET, 0);
    bochs_vbe_dispi_write(dev->regs, BOCHS_VBE_DISPI_ENABLE, 0x41);

    zx_set_framebuffer(get_root_resource(), dev->framebuffer,
                       dev->framebuffer_size, dev->info.format,
                       dev->info.width, dev->info.height, dev->info.stride);

    zxlogf(SPEW, "bochs_vbe_set_hw_mode:\n");
    zxlogf(SPEW, "     ID: 0x%x\n", bochs_vbe_dispi_read(dev->regs, BOCHS_VBE_DISPI_ID));
    zxlogf(SPEW, "   XRES: 0x%x\n", bochs_vbe_dispi_read(dev->regs, BOCHS_VBE_DISPI_XRES));
    zxlogf(SPEW, "   YRES: 0x%x\n", bochs_vbe_dispi_read(dev->regs, BOCHS_VBE_DISPI_YRES));
    zxlogf(SPEW, "    BPP: 0x%x\n", bochs_vbe_dispi_read(dev->regs, BOCHS_VBE_DISPI_BPP));
    zxlogf(SPEW, " ENABLE: 0x%x\n", bochs_vbe_dispi_read(dev->regs, BOCHS_VBE_DISPI_ENABLE));
    zxlogf(SPEW, "   BANK: 0x%x\n", bochs_vbe_dispi_read(dev->regs, BOCHS_VBE_DISPI_BANK));
    zxlogf(SPEW, "VWIDTH: 0x%x\n", bochs_vbe_dispi_read(dev->regs, BOCHS_VBE_DISPI_VIRT_WIDTH));
    zxlogf(SPEW, "VHEIGHT: 0x%x\n", bochs_vbe_dispi_read(dev->regs, BOCHS_VBE_DISPI_VIRT_HEIGHT));
    zxlogf(SPEW, "   XOFF: 0x%x\n", bochs_vbe_dispi_read(dev->regs, BOCHS_VBE_DISPI_X_OFFSET));
    zxlogf(SPEW, "   YOFF: 0x%x\n", bochs_vbe_dispi_read(dev->regs, BOCHS_VBE_DISPI_Y_OFFSET));
    zxlogf(SPEW, "    64K: 0x%x\n", bochs_vbe_dispi_read(dev->regs, BOCHS_VBE_DISPI_VIDEO_MEMORY_64K));
}

// implement display protocol

static zx_status_t bochs_vbe_set_mode(void* ctx, zx_display_info_t* info) {
    assert(info);
    bochs_vbe_device_t* vdev = ctx;
    memcpy(&vdev->info, info, sizeof(zx_display_info_t));
    set_hw_mode(vdev);
    return ZX_OK;
}

static zx_status_t bochs_vbe_get_mode(void* ctx, zx_display_info_t* info) {
    assert(info);
    bochs_vbe_device_t* vdev = ctx;
    memcpy(info, &vdev->info, sizeof(zx_display_info_t));
    return ZX_OK;
}

static zx_status_t bochs_vbe_get_framebuffer(void* ctx, void** framebuffer) {
    assert(framebuffer);
    bochs_vbe_device_t* vdev = ctx;
    (*framebuffer) = vdev->framebuffer;
    return ZX_OK;
}

static display_protocol_ops_t bochs_vbe_display_proto = {
    .set_mode = bochs_vbe_set_mode,
    .get_mode = bochs_vbe_get_mode,
    .get_framebuffer = bochs_vbe_get_framebuffer,
};

// implement device protocol

static void bochs_vbe_release(void* ctx) {
    bochs_vbe_device_t* vdev = ctx;

    if (vdev->regs) {
        zx_handle_close(vdev->regs_handle);
        vdev->regs_handle = -1;
    }

    if (vdev->framebuffer) {
        zx_handle_close(vdev->framebuffer_handle);
        vdev->framebuffer_handle = -1;
    }

    free(vdev);
}

static zx_protocol_device_t bochs_vbe_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = bochs_vbe_release,
};

// implement driver object:

static zx_status_t bochs_vbe_bind(void* ctx, zx_device_t* dev) {
    pci_protocol_t pci;
    zx_status_t status;

    if (device_get_protocol(dev, ZX_PROTOCOL_PCI, &pci))
        return ZX_ERR_NOT_SUPPORTED;

    // map resources and initialize the device
    bochs_vbe_device_t* device = calloc(1, sizeof(bochs_vbe_device_t));
    if (!device)
        return ZX_ERR_NO_MEMORY;

    // map register window
    status = pci_map_bar(&pci, 2u, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                              &device->regs, &device->regs_size,
                              &device->regs_handle);
    if (status != ZX_OK) {
        printf("bochs-vbe: failed to map pci config: %d\n", status);
        goto fail;
    }

    // map framebuffer window
    status = pci_map_bar(&pci, 0u,  ZX_CACHE_POLICY_WRITE_COMBINING,
                              &device->framebuffer,
                              &device->framebuffer_size,
                              &device->framebuffer_handle);
    if (status != ZX_OK) {
        printf("bochs-vbe: failed to map pci config: %d\n", status);
        goto fail;
    }

    device->info.format = ZX_PIXEL_FORMAT_RGB_565;
    device->info.width = 1024;
    device->info.height = 768;
    device->info.stride = 1024;
    set_hw_mode(device);

    // create and add the display (char) device
   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "bochs_vbe",
        .ctx = device,
        .ops = &bochs_vbe_device_proto,
        .proto_id = ZX_PROTOCOL_DISPLAY,
        .proto_ops = &bochs_vbe_display_proto,
    };

    status = device_add(dev, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }

    zxlogf(SPEW, "initialized bochs_vbe display driver, reg=%p regsize=0x%lx fb=%p fbsize=0x%lx\n",
            device->regs, device->regs_size, device->framebuffer, device->framebuffer_size);

    return ZX_OK;

fail:
    free(device);
    return status;
}

static zx_driver_ops_t bochs_vbe_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = bochs_vbe_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(bochs_vbe, bochs_vbe_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, QEMU_VGA_VID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QEMU_VGA_DID),
ZIRCON_DRIVER_END(bochs_vbe)
