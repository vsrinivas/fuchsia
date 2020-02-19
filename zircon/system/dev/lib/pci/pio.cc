// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <assert.h>
#include <lib/pci/pio.h>
#include <zircon/hw/pci.h>
#include <zircon/types.h>

#include <fbl/auto_lock.h>
#include <hw/inout.h>
#include <hwreg/bitfields.h>

#ifdef __x86_64__

static constexpr uint16_t kPciConfigAddrPort = 0xCF8;
static constexpr uint16_t kPciConfigDataPort = 0xCFC;

fbl::Mutex pio_port_lock;

typedef struct {
  uint32_t value;
  DEF_SUBBIT(value, 31, enable);
  DEF_SUBFIELD(value, 23, 16, bus);
  DEF_SUBFIELD(value, 15, 11, device);
  DEF_SUBFIELD(value, 10, 8, function);
  DEF_SUBFIELD(value, 7, 0, reg_num);
} config_address_t;

// This library assumes the calling process already has the io bitmap permissions
// set to access cf8/cfc. Any processes with that permission will be synchronizing
// with each other by means of the PCI Root protocol.

static zx_status_t pci_pio_read(pci_bdf_t bdf, uint8_t offset, uint32_t* val) {
  fbl::AutoLock lock(&pio_port_lock);

  config_address_t addr = {};
  addr.set_enable(true);
  addr.set_bus(bdf.bus_id);
  addr.set_device(bdf.device_id);
  addr.set_function(bdf.function_id);
  addr.set_reg_num(offset & ~0x3);  // Lowest 2 bits must be zero, all reads are 32 bit

  outpd(kPciConfigAddrPort, addr.value);
  *val = inpd(kPciConfigDataPort);
  return ZX_OK;
}

zx_status_t pci_pio_read32(pci_bdf_t bdf, uint8_t offset, uint32_t* val) {
  // Only 32 bit alignment allowed for 32 bit reads.
  if (offset & 0x3) {
    printf("invalid args read32\n");
    return ZX_ERR_INVALID_ARGS;
  }
  uint32_t _val = 0;
  zx_status_t status = pci_pio_read(bdf, offset, val);
  if (status == ZX_OK) {
    *val = _val;
  }
  return status;
}

zx_status_t pci_pio_read16(pci_bdf_t bdf, uint8_t offset, uint16_t* val) {
  // Only 16 bit alignment allowed for 16 bit reads
  if (offset & 0x1) {
    printf("invalid args read16\n");
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t _val = 0;
  zx_status_t status = pci_pio_read(bdf, offset, &_val);
  if (status == ZX_OK) {
    // Shift the top 16 over if requested
    *val = static_cast<uint16_t>(_val >> (8u * (offset & 0x2)));
  }
  return status;
}

zx_status_t pci_pio_read8(pci_bdf_t bdf, uint8_t offset, uint8_t* val) {
  uint32_t _val = 0;
  zx_status_t status = pci_pio_read(bdf, offset, &_val);
  if (status == ZX_OK) {
    *val = static_cast<uint8_t>(_val >> (8u * (offset & 0x3)));
  }
  return status;
}

// Generates an unshifted mask to match the width of the write we're making.
static constexpr uint32_t rmw_mask(size_t width) {
  return (width == 32) ? 0xffffffff : (1u << width) - 1u;
}

// Figure out the shift to align the bytes in the right. Valid offsets are already
// checked by the pci_pio_write calls themselves.
static constexpr int calculate_shift(uint8_t offset) { return (offset & 0x3) * 8u; }

static zx_status_t pci_pio_write(pci_bdf_t bdf, uint8_t offset, uint32_t mask, uint32_t val) {
  fbl::AutoLock lock(&pio_port_lock);

  config_address_t addr = {};
  addr.set_enable(true);
  addr.set_bus(bdf.bus_id);
  addr.set_device(bdf.device_id);
  addr.set_function(bdf.function_id);
  addr.set_reg_num(offset & ~0x3);  // Lowest 3 bits must be zero, all reads are 32 bit

  outpd(kPciConfigAddrPort, addr.value);
  // Zero out the bytes we're going to write and then OR them in.
  uint32_t old_val = inpd(kPciConfigDataPort);
  old_val &= ~mask;
  old_val |= val;
  outpd(kPciConfigDataPort, old_val);

  return ZX_OK;
}

zx_status_t pci_pio_write32(pci_bdf_t bdf, uint8_t offset, uint32_t val) {
  // Only 32 bit alignment allowed for 32 bit reads
  if (offset & 0x3) {
    return ZX_ERR_INVALID_ARGS;
  }
  return pci_pio_write(bdf, offset, rmw_mask(32), val);
}

// These functions both create a shifted mask and shifted value to call the main write
// function so that its body can be as simple as possible.
zx_status_t pci_pio_write16(pci_bdf_t bdf, uint8_t offset, uint16_t val) {
  // Only 16 bit alignment allowed for 16 bit reads
  if (offset & 0x1) {
    return ZX_ERR_INVALID_ARGS;
  }
  int shift = calculate_shift(offset);
  return pci_pio_write(bdf, offset, rmw_mask(16) << shift, val << shift);
}

zx_status_t pci_pio_write8(pci_bdf_t bdf, uint8_t offset, uint8_t val) {
  int shift = calculate_shift(offset);
  return pci_pio_write(bdf, offset, rmw_mask(8) << shift, val << shift);
}

#endif  // __x86_64__
