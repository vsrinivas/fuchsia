// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/hw/inout.h>
#include <lib/pci/hw.h>
#include <zircon/syscalls.h>

#include "src/graphics/display/drivers/simple/simple-display.h"
#include "src/graphics/display/drivers/simple/simple-gga-bind.h"

// GGA device only supports RGB888 format.
#define GGA_DISPLAY_FORMAT ZX_PIXEL_FORMAT_RGB_888
#define GGA_DISPLAY_BPP 24

#define GGA_VBE_DISPI_ID 0x0
#define GGA_VBE_DISPI_XRES 0x1
#define GGA_VBE_DISPI_YRES 0x2
#define GGA_VBE_DISPI_BPP 0x3
#define GGA_VBE_DISPI_ENABLE 0x4
#define GGA_VBE_DISPI_BANK 0x5
#define GGA_VBE_DISPI_VIRT_WIDTH 0x6
#define GGA_VBE_DISPI_VIRT_HEIGHT 0x7
#define GGA_VBE_DISPI_X_OFFSET 0x8
#define GGA_VBE_DISPI_Y_OFFSET 0x9
#define GGA_VBE_DISPI_VIDEO_MEMORY_64K 0xa
#define GGA_VBE_DISPI_NUM_REGS 0xb

#define GGA_VBE_DISPI_ENABLE_FLAG_ENABLED 0x01
#define GGA_VBE_DISPI_ENABLE_FLAG_GET_CAPS 0x02
#define GGA_VBE_DISPI_ENABLE_FLAG_LFB_ENABLED 0x40

#define GGA_VBE_INDEX_REG 0x1ce
#define GGA_VBE_DATA_REG 0x1cf
#define GGA_VBE_DATA2_REG 0x1d0

static const char* GGA_VBE_REG_NAMES[] = {"ID",       "XRES",     "YRES",       "BPP",
                                          "ENABLE",   "BANK",     "VIRT_WIDTH", "VIRT_HEIGHT",
                                          "X_OFFSET", "Y_OFFSET", "MEMORY_64K"};

static uint16_t gga_read_reg(uint16_t idx) {
  outpw(GGA_VBE_INDEX_REG, idx);
  return inpw(GGA_VBE_DATA_REG);
}

static void gga_write_reg(uint16_t idx, uint16_t value) {
  outpw(GGA_VBE_INDEX_REG, idx);
  outpw(GGA_VBE_DATA_REG, value);
}

__attribute__((unused)) static void gga_dump_regs() {
  zxlogf(INFO, "GGA VBE Registers:");
  uint16_t reg_value[GGA_VBE_DISPI_NUM_REGS];
  for (size_t i = 0; i < GGA_VBE_DISPI_NUM_REGS; i++) {
    reg_value[i] = gga_read_reg(i);
  }
  for (size_t i = 0; i < GGA_VBE_DISPI_NUM_REGS; i++) {
    zxlogf(INFO, "  [%12s] = 0x%x", GGA_VBE_REG_NAMES[i], reg_value[i]);
  }
}

static zx_status_t gga_disp_setup(uint16_t width, uint16_t height) {
  // TODO(fxbug.dev/84561): Drivers shouldn't request root resource to get IO
  // ports. Instead the board driver should provide the port access over PCI
  // root protocol and PCI bus driver should pass them to corresponding devices.
  zx_handle_t root = get_root_resource();

  zx_status_t status = zx_ioports_request(root, GGA_VBE_INDEX_REG, 1u);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Cannot request VBE index register: %d", __func__, status);
    return status;
  }
  status = zx_ioports_request(root, GGA_VBE_DATA_REG, 1u);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Cannot request VBE data register: %d", __func__, status);
    return status;
  }
  status = zx_ioports_request(root, GGA_VBE_DATA2_REG, 1u);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Cannot request VBE data2 register: %d", __func__, status);
    return status;
  }

  gga_write_reg(GGA_VBE_DISPI_XRES, width);
  gga_write_reg(GGA_VBE_DISPI_YRES, height);
  gga_write_reg(GGA_VBE_DISPI_BPP, GGA_DISPLAY_BPP);
  gga_write_reg(GGA_VBE_DISPI_ENABLE,
                GGA_VBE_DISPI_ENABLE_FLAG_ENABLED | GGA_VBE_DISPI_ENABLE_FLAG_LFB_ENABLED);

  status = zx_ioports_release(root, GGA_VBE_INDEX_REG, 1u);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Cannot release VBE index register: %d", __func__, status);
    return status;
  }
  status = zx_ioports_release(root, GGA_VBE_DATA_REG, 1u);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Cannot release VBE data register: %d", __func__, status);
    return status;
  }
  status = zx_ioports_request(root, GGA_VBE_DATA2_REG, 1u);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Cannot release VBE data2 register: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

static zx_status_t gga_disp_bind(void* ctx, zx_device_t* dev) {
  uint32_t format, width, height, stride;
  zx_status_t status =
      zx_framebuffer_get_info(get_root_resource(), &format, &width, &height, &stride);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to get bootloader dimensions: %d\n", __func__, status);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Set up device VBE registers.
  status = gga_disp_setup(width, height);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Cannot set up GGA device registers: %d", __func__, status);
    return status;
  }

  // Framebuffer BAR is 0.
  // GGA devices only support RGB888 format, thus we should always override
  // the format information we got from bootloader framebuffer.
  return bind_simple_pci_display(dev, "gga", /*bar=*/0u, width, height, /*stride=*/width,
                                 GGA_DISPLAY_FORMAT);
}

static zx_driver_ops_t gga_disp_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = gga_disp_bind,
};

ZIRCON_DRIVER(gga_disp, gga_disp_driver_ops, "zircon", "0.1");
