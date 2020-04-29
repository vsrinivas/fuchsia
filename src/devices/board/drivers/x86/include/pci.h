// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_PCI_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_PCI_H_

#include <zircon/compiler.h>
#include <zircon/syscalls/pci.h>

#include <acpica/acpi.h>
#include <acpica/actypes.h>
#include <ddk/device.h>
#include <ddk/protocol/pciroot.h>

#include "acpi-private.h"

__BEGIN_CDECLS
// It would be nice to use the hwreg library here, but these structs should be kept
// simple so that it can be passed across process boundaries.
#define MB(n) (1024UL * 1024UL * (n))
#define PCI_BUS_MAX 255

// Base Address Allocation Structure, defined in PCI firmware spec v3.2 chapter 4.1.2
struct pci_ecam_baas {
  uint64_t base_address;
  uint16_t segment_group;
  uint8_t start_bus_num;
  uint8_t end_bus_num;
  uint32_t reserved0;
};

struct pciroot_ctx {
  char name[ACPI_NAMESEG_SIZE];
  ACPI_HANDLE acpi_object;
  ACPI_DEVICE_INFO acpi_device_info;
  struct pci_platform_info info;
};

zx_status_t pci_init(zx_device_t* parent, ACPI_HANDLE object, ACPI_DEVICE_INFO* info,
                     AcpiWalker* ctx);

zx_status_t get_pci_init_arg(zx_pci_init_arg_t** arg, uint32_t* size);
zx_status_t pci_report_current_resources(zx_handle_t root_resource_handle);

__END_CDECLS
#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_PCI_H_
