// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/pci.h>
#include <zircon/pixelformat.h>
#include <zircon/process.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/mmio-buffer.h>
#include <ddk/protocol/pci.h>
#include <hw/pci.h>

#include "simple-display.h"

#define DISPLAY_WIDTH 1024
#define DISPLAY_HEIGHT 768
#define DISPLAY_FORMAT ZX_PIXEL_FORMAT_RGB_565

#define QEMU_VGA_VID (0x1234)
#define QEMU_VGA_DID (0x1111)

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

static int zx_display_format_to_bpp(zx_pixel_format_t format) {
  unsigned bpp = ZX_PIXEL_FORMAT_BYTES(format) * 8;
  if (bpp == 0) {
    // unknown
    return -1;
  } else {
    return bpp;
  }
}

static void set_hw_mode(void* regs, uint16_t width, uint16_t height, zx_pixel_format_t format) {
  zxlogf(SPEW, "id: 0x%x\n", bochs_vbe_dispi_read(regs, BOCHS_VBE_DISPI_ID));

  int bpp = zx_display_format_to_bpp(format);
  assert(bpp >= 0);

  bochs_vbe_dispi_write(regs, BOCHS_VBE_DISPI_ENABLE, 0);
  bochs_vbe_dispi_write(regs, BOCHS_VBE_DISPI_BPP, bpp);
  bochs_vbe_dispi_write(regs, BOCHS_VBE_DISPI_XRES, width);
  bochs_vbe_dispi_write(regs, BOCHS_VBE_DISPI_YRES, height);
  bochs_vbe_dispi_write(regs, BOCHS_VBE_DISPI_BANK, 0);
  bochs_vbe_dispi_write(regs, BOCHS_VBE_DISPI_VIRT_WIDTH, width);
  bochs_vbe_dispi_write(regs, BOCHS_VBE_DISPI_VIRT_HEIGHT, height);
  bochs_vbe_dispi_write(regs, BOCHS_VBE_DISPI_X_OFFSET, 0);
  bochs_vbe_dispi_write(regs, BOCHS_VBE_DISPI_Y_OFFSET, 0);
  bochs_vbe_dispi_write(regs, BOCHS_VBE_DISPI_ENABLE, 0x41);

  zxlogf(SPEW, "bochs_vbe_set_hw_mode:\n");
  zxlogf(SPEW, "     ID: 0x%x\n", bochs_vbe_dispi_read(regs, BOCHS_VBE_DISPI_ID));
  zxlogf(SPEW, "   XRES: 0x%x\n", bochs_vbe_dispi_read(regs, BOCHS_VBE_DISPI_XRES));
  zxlogf(SPEW, "   YRES: 0x%x\n", bochs_vbe_dispi_read(regs, BOCHS_VBE_DISPI_YRES));
  zxlogf(SPEW, "    BPP: 0x%x\n", bochs_vbe_dispi_read(regs, BOCHS_VBE_DISPI_BPP));
  zxlogf(SPEW, " ENABLE: 0x%x\n", bochs_vbe_dispi_read(regs, BOCHS_VBE_DISPI_ENABLE));
  zxlogf(SPEW, "   BANK: 0x%x\n", bochs_vbe_dispi_read(regs, BOCHS_VBE_DISPI_BANK));
  zxlogf(SPEW, "VWIDTH: 0x%x\n", bochs_vbe_dispi_read(regs, BOCHS_VBE_DISPI_VIRT_WIDTH));
  zxlogf(SPEW, "VHEIGHT: 0x%x\n", bochs_vbe_dispi_read(regs, BOCHS_VBE_DISPI_VIRT_HEIGHT));
  zxlogf(SPEW, "   XOFF: 0x%x\n", bochs_vbe_dispi_read(regs, BOCHS_VBE_DISPI_X_OFFSET));
  zxlogf(SPEW, "   YOFF: 0x%x\n", bochs_vbe_dispi_read(regs, BOCHS_VBE_DISPI_Y_OFFSET));
  zxlogf(SPEW, "    64K: 0x%x\n", bochs_vbe_dispi_read(regs, BOCHS_VBE_DISPI_VIDEO_MEMORY_64K));
}

static zx_status_t bochs_vbe_bind(void* ctx, zx_device_t* dev) {
  pci_protocol_t pci;
  zx_status_t status;

  if (device_get_protocol(dev, ZX_PROTOCOL_PCI, &pci))
    return ZX_ERR_NOT_SUPPORTED;

  mmio_buffer_t mmio;
  // map register window
  status = pci_map_bar_buffer(&pci, 2u, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    printf("bochs-vbe: failed to map pci config: %d\n", status);
    return status;
  }

  set_hw_mode(mmio.vaddr, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_FORMAT);

  mmio_buffer_release(&mmio);

  return bind_simple_pci_display(dev, "bochs_vbe", 0u, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_WIDTH,
                                 DISPLAY_FORMAT);
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
