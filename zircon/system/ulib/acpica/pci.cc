// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <acpica/acpi.h>

#include <fuchsia/hardware/pciroot/c/banjo.h>
#include <lib/pci/pio.h>

#include "zircon/system/ulib/acpica/osfuchsia.h"

namespace {
const size_t PCIE_MAX_DEVICES_PER_BUS = 32;
const size_t PCIE_MAX_FUNCTIONS_PER_DEVICE = 8;
}  // namespace

/**
 * @brief Read/Write a value from a PCI configuration register.
 *
 * @param PciId The full PCI configuration space address, consisting of a
 *        segment number, bus number, device number, and function number.
 * @param Register The PCI register address to be read from.
 * @param Value A pointer to a location where the data is to be returned.
 * @param Width The register width in bits, either 8, 16, 32, or 64.
 * @param Write Write or Read.
 *
 * @return Exception code that indicates success or reason for failure.
 */
static ACPI_STATUS AcpiOsReadWritePciConfiguration(ACPI_PCI_ID* PciId, UINT32 Register,
                                                   UINT64* Value, UINT32 Width, bool Write) {
  if (LOCAL_TRACE) {
    printf("ACPIOS: %s PCI Config %x:%x:%x:%x register %#x width %u\n", Write ? "write" : "read",
           PciId->Segment, PciId->Bus, PciId->Device, PciId->Function, Register, Width);
  }

  // Only segment 0 is supported for now
  if (PciId->Segment != 0) {
    printf("ACPIOS: read/write config, segment != 0 not supported.\n");
    return AE_ERROR;
  }

  // Check bounds of device and function offsets
  if (PciId->Device >= PCIE_MAX_DEVICES_PER_BUS ||
      PciId->Function >= PCIE_MAX_FUNCTIONS_PER_DEVICE) {
    printf("ACPIOS: device out of reasonable bounds.\n");
    return AE_ERROR;
  }

  // PCI config only supports up to 32 bit values
  if (Write && (*Value > UINT_MAX)) {
    printf("ACPIOS: read/write config, Value passed does not fit confg registers.\n");
  }

  // Clear higher bits before a read
  if (!Write) {
    *Value = 0;
  }

#if __x86_64__
  uint8_t bus = static_cast<uint8_t>(PciId->Bus);
  uint8_t dev = static_cast<uint8_t>(PciId->Device);
  uint8_t func = static_cast<uint8_t>(PciId->Function);
  uint8_t offset = static_cast<uint8_t>(Register);
  zx_status_t st;
#ifdef ENABLE_USER_PCI
  pci_bdf_t addr = {bus, dev, func};
  switch (Width) {
    case 8u:
      (Write) ? st = pci_pio_write8(addr, offset, static_cast<uint8_t>(*Value))
              : st = pci_pio_read8(addr, offset, reinterpret_cast<uint8_t*>(Value));
      break;
    case 16u:
      (Write) ? st = pci_pio_write16(addr, offset, static_cast<uint16_t>(*Value))
              : st = pci_pio_read16(addr, offset, reinterpret_cast<uint16_t*>(Value));
      break;
    // assume 32bit by default since 64 bit reads on IO ports are not a thing supported by the
    // spec
    default:
      (Write) ? st = pci_pio_write32(addr, offset, static_cast<uint32_t>(*Value))
              : st = pci_pio_read32(addr, offset, reinterpret_cast<uint32_t*>(Value));
  }
#else
  st = zx_pci_cfg_pio_rw(root_resource_handle, bus, dev, func, offset,
                         reinterpret_cast<uint32_t*>(Value), static_cast<uint8_t>(Width), Write);

#endif  // ENABLE_USER_PCI
#ifdef ACPI_DEBUG_OUTPUT
  if (st != ZX_OK) {
    printf("ACPIOS: pci rw error: %d\n", st);
  }
#endif  // ACPI_DEBUG_OUTPUT
  return (st == ZX_OK) ? AE_OK : AE_ERROR;
#endif  // __x86_64__

  return AE_NOT_IMPLEMENTED;
}
/**
 * @brief Read a value from a PCI configuration register.
 *
 * @param PciId The full PCI configuration space address, consisting of a
 *        segment number, bus number, device number, and function number.
 * @param Register The PCI register address to be read from.
 * @param Value A pointer to a location where the data is to be returned.
 * @param Width The register width in bits, either 8, 16, 32, or 64.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsReadPciConfiguration(ACPI_PCI_ID* PciId, UINT32 Register, UINT64* Value,
                                       UINT32 Width) {
  return AcpiOsReadWritePciConfiguration(PciId, Register, Value, Width, false);
}

/**
 * @brief Write a value to a PCI configuration register.
 *
 * @param PciId The full PCI configuration space address, consisting of a
 *        segment number, bus number, device number, and function number.
 * @param Register The PCI register address to be written to.
 * @param Value Data to be written.
 * @param Width The register width in bits, either 8, 16, or 32.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsWritePciConfiguration(ACPI_PCI_ID* PciId, UINT32 Register, UINT64 Value,
                                        UINT32 Width) {
  return AcpiOsReadWritePciConfiguration(PciId, Register, &Value, Width, true);
}
