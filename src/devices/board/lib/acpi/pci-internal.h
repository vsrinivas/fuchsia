// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_PCI_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_PCI_H_

#include <fuchsia/hardware/pciroot/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/pci/pciroot.h>
#include <lib/zx/resource.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/pci.h>

#include <unordered_map>

#include <acpica/acpi.h>
#include <acpica/actypes.h>

#include "src/devices/board/lib/acpi/acpi.h"
#include "src/devices/board/lib/acpi/pci.h"

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

// A structure derived from ACPI _PRTs that represents a zx::interrupt to create and
// provide to the PCI bus driver.
struct acpi_legacy_irq {
  uint32_t vector;   // Hardware vector
  uint32_t options;  // Configuration for zx_interrupt_create
};

zx_status_t get_pci_init_arg(acpi::Acpi* acpi, zx_pci_init_arg_t** arg, uint32_t* size);
zx_status_t pci_report_current_resources(acpi::Acpi* acpi, zx_handle_t root_resource_handle);

class x64Pciroot : public PcirootBase {
 public:
  struct Context {
    char name[ACPI_NAMESEG_SIZE];
    ACPI_HANDLE acpi_object;
    acpi::UniquePtr<ACPI_DEVICE_INFO> acpi_device_info;
    zx_device_t* platform_bus;
    std::unordered_map<uint32_t, acpi_legacy_irq> irqs;
    std::vector<zx::resource> irq_resources;
    std::vector<pci_irq_routing_entry_t> routing;
    struct pci_platform_info info;
  };

  static zx_status_t Create(PciRootHost* root_host, x64Pciroot::Context ctx, zx_device_t* parent,
                            const char* name, std::vector<pci_bdf_t> acpi_bdfs);
  zx_status_t PcirootGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) final;
  zx_status_t PcirootGetPciPlatformInfo(pci_platform_info_t* info) final;
  zx_status_t PcirootConfigRead8(const pci_bdf_t* address, uint16_t offset, uint8_t* value) final;
  zx_status_t PcirootConfigRead16(const pci_bdf_t* address, uint16_t offset, uint16_t* value) final;
  zx_status_t PcirootConfigRead32(const pci_bdf_t* address, uint16_t offset, uint32_t* value) final;
  zx_status_t PcirootConfigWrite8(const pci_bdf_t* address, uint16_t offset, uint8_t value) final;
  zx_status_t PcirootConfigWrite16(const pci_bdf_t* address, uint16_t offset, uint16_t value) final;
  zx_status_t PcirootConfigWrite32(const pci_bdf_t* address, uint16_t offset, uint32_t value) final;

 private:
  Context context_;
  std::vector<pci_bdf_t> acpi_bdfs_;
  x64Pciroot(PciRootHost* root_host, x64Pciroot::Context ctx, zx_device_t* parent, const char* name,
             std::vector<pci_bdf_t> acpi_bdfs)
      : PcirootBase(root_host, parent, name),
        context_(std::move(ctx)),
        acpi_bdfs_(std::move(acpi_bdfs)) {}
};

namespace acpi {

ACPI_STATUS GetPciRootIrqRouting(acpi::Acpi* acpi, ACPI_HANDLE root_obj,
                                 x64Pciroot::Context* context);

}  // namespace acpi

__END_CDECLS

#endif
