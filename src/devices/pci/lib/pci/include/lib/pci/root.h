// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_PCI_LIB_PCI_INCLUDE_LIB_PCI_ROOT_H_
#define SRC_DEVICES_PCI_LIB_PCI_INCLUDE_LIB_PCI_ROOT_H_
#include <string.h>
#include <zircon/compiler.h>

#include <ddk/protocol/pciroot.h>

__BEGIN_CDECLS
pciroot_protocol_ops_t* get_pciroot_ops(void);
__END_CDECLS

// Userspace ACPI/PCI support is entirely in C++, but the legacy kernel pci support
// still has kpci.c. In lieu of needlessly porting that, it's simpler to ifdef the
// DDKTL usage away from it until we can remove it entirely.
#ifdef __cplusplus
#include <lib/pci/root_host.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/port.h>
#include <lib/zx/thread.h>
#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>

#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include <ddktl/device.h>
#include <ddktl/protocol/pciroot.h>

// Pciroot is the interface between a platform's PCI RootHost, and the PCI Bus Driver instances.
// It is templated on |PlatformContextType| so that platform specific context can be provided to
// each root as necessary. For instance, in ACPI systems this contains the ACPI object for the PCI
// root to work with ACPICA.

template <class PlatformContextType>
class Pciroot : public ddk::Device<Pciroot<PlatformContextType>>,
                public ddk::PcirootProtocol<Pciroot<PlatformContextType>, ddk::base_protocol> {
 public:
  static zx_status_t Create(PciRootHost* root_host, std::unique_ptr<PlatformContextType> ctx,
                            zx_device_t* parent, zx_device_t* platform_bus, const char* name);
  zx_status_t PcirootGetAuxdata(const char* args, void* out_data, size_t data_size,
                                size_t* out_data_actual);
  zx_status_t PcirootGetBti(uint32_t bdf, uint32_t index, zx::bti* bti);
  zx_status_t PcirootConnectSysmem(zx::handle handle);
  zx_status_t PcirootGetPciPlatformInfo(pci_platform_info_t* info);
  zx_status_t PcirootGetPciIrqInfo(pci_irq_info_t* info);

  // If |true| is returned by the Pciroot implementation then the bus driver
  // will send all config space reads and writes over the Pciroot protocol
  // rather than in the bus driver using MMIO/IO access. This exists to work
  // with non-standard PCI implementations that require controller configuration
  // before accessing a given device.
  bool PcirootDriverShouldProxyConfig();

  // Config space read/write accessors for PCI systems that require platform
  // bus to configure something before config space is accessible. For ACPI
  // systems we only intend to use PIO access if MMIO config is unavailable.
  // In the event we do use them though, we're restricted to the base 256 byte
  // PCI config header.
  zx_status_t PcirootConfigRead8(const pci_bdf_t* address, uint16_t offset, uint8_t* value);
  zx_status_t PcirootConfigRead16(const pci_bdf_t* address, uint16_t offset, uint16_t* value);
  zx_status_t PcirootConfigRead32(const pci_bdf_t* address, uint16_t offset, uint32_t* value);
  zx_status_t PcirootConfigWrite8(const pci_bdf_t* address, uint16_t offset, uint8_t value);
  zx_status_t PcirootConfigWrite16(const pci_bdf_t* address, uint16_t offset, uint16_t value);
  zx_status_t PcirootConfigWrite32(const pci_bdf_t* address, uint16_t offset, uint32_t value);

  // These methods may not exist in usable implementations and are a
  // prototyping side effect. It likely will not make sense for MSI blocks to
  // be dealt with in the PCI driver itself if we can help it.
  zx_status_t PcirootAllocMsiBlock(uint64_t requested_irqs, bool can_target_64bit,
                                   msi_block_t* out_block);
  zx_status_t PcirootFreeMsiBlock(const msi_block_t* block);
  zx_status_t PcirootMaskUnmaskMsi(uint64_t msi_id, bool mask);

  // These methods correspond to address space reservations needed by the bus
  // driver for providing a place to map bridges and bars.
  zx_status_t PcirootGetAddressSpace(zx_paddr_t in_base, size_t len, pci_address_space_t type,
                                     bool low, uint64_t* out_base, zx::resource* resource);
  zx_status_t PcirootFreeAddressSpace(uint64_t base, size_t len, pci_address_space_t type);

  // DDK mix-in impls
  void DdkRelease() { delete this; }

  // Accessors
  // TODO(cja): Remove this when we no longer share get_auxdata/get_bti with
  // the kernel pci bus driver's C interface.
  void* c_context() { return ctx_.get(); }
  char name_[8];

 private:
  Pciroot(PciRootHost* host, std::unique_ptr<PlatformContextType> ctx, zx_device_t* parent,
          zx_device_t* platform_bus, const char* name)
      : ddk::Device<Pciroot<PlatformContextType>>(parent),
        root_host_(host),
        ctx_(std::move(ctx)),
        platform_bus_(platform_bus) {}
  // TODO(32978): presently, pciroot instances will always outlive the root host
  // it references here because it exists within the same devhost process as a
  // singleton. This will be updated when the pciroot implementation changes to
  // move away from a standalone banjo protocol.
  PciRootHost* root_host_;
  std::unique_ptr<PlatformContextType> ctx_;
  zx_device_t* platform_bus_;
};

#endif  // ifndef __cplusplus
#endif  // SRC_DEVICES_PCI_LIB_PCI_INCLUDE_LIB_PCI_ROOT_H_
