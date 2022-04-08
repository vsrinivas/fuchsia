// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_PCI_LIB_PCI_INCLUDE_LIB_PCI_PIO_H_
#define SRC_DEVICES_PCI_LIB_PCI_INCLUDE_LIB_PCI_PIO_H_

#include <fuchsia/hardware/pciroot/c/banjo.h>
#include <stdint.h>
#include <zircon/hw/pci.h>
#include <zircon/types.h>

#ifdef __x86_64__

inline constexpr uint16_t kPciConfigAddrPort = 0xCF8;
inline constexpr uint16_t kPciConfigDataPort = 0xCFC;

// Reads from a PCI register at offset |offset| in the config space of the specified device by
// using the cf8/cfc port io access method.
zx_status_t pci_pio_read32(pci_bdf_t bdf, uint8_t offset, uint32_t* val);
zx_status_t pci_pio_read16(pci_bdf_t bdf, uint8_t offset, uint16_t* val);
zx_status_t pci_pio_read8(pci_bdf_t bdf, uint8_t offset, uint8_t* val);

// Writes to a PCI register at offset |offset| in the config space of the specified device by
// using the cf8/cfc port io access method. These functions will handle the nuances of
// read/modify/write semantics for the caller.
zx_status_t pci_pio_write32(pci_bdf_t bdf, uint8_t offset, uint32_t val);
zx_status_t pci_pio_write16(pci_bdf_t bdf, uint8_t offset, uint16_t val);
zx_status_t pci_pio_write8(pci_bdf_t bdf, uint8_t offset, uint8_t val);
#else
inline zx_status_t pci_pio_read32(pci_bdf_t bdf, uint8_t offset, uint32_t* val) {
  return ZX_ERR_NOT_SUPPORTED;
}
inline zx_status_t pci_pio_read16(pci_bdf_t bdf, uint8_t offset, uint16_t* val) {
  return ZX_ERR_NOT_SUPPORTED;
}
inline zx_status_t pci_pio_read8(pci_bdf_t bdf, uint8_t offset, uint8_t* val) {
  return ZX_ERR_NOT_SUPPORTED;
}
inline zx_status_t pci_pio_write32(pci_bdf_t bdf, uint8_t offset, uint32_t val) {
  return ZX_ERR_NOT_SUPPORTED;
}
inline zx_status_t pci_pio_write16(pci_bdf_t bdf, uint8_t offset, uint16_t val) {
  return ZX_ERR_NOT_SUPPORTED;
}
inline zx_status_t pci_pio_write8(pci_bdf_t bdf, uint8_t offset, uint8_t val) {
  return ZX_ERR_NOT_SUPPORTED;
}

#endif  // __x86_64__

#endif  // SRC_DEVICES_PCI_LIB_PCI_INCLUDE_LIB_PCI_PIO_H_
