// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_PCI_LIB_PCI_INCLUDE_LIB_PCI_ROOT_H_
#define SRC_DEVICES_PCI_LIB_PCI_INCLUDE_LIB_PCI_ROOT_H_
#include <string.h>
#include <zircon/compiler.h>

#include <ddk/protocol/pciroot.h>

pciroot_protocol_ops_t* get_pciroot_ops(void);

// Userspace ACPI/PCI support is entirely in C++, but the legacy kernel pci support
// still has kpci.c. In lieu of needlessly porting that, it's simpler to ifdef the
// DDKTL usage away from it until we can remove it entirely.
#ifdef __cplusplus
#include <lib/pci/root_host.h>
#include <lib/zx/msi.h>
#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>

#include <memory>

#include <ddktl/device.h>
#include <ddktl/protocol/pciroot.h>

// PcirootBase is the interface between a platform's PCI RootHost, and the PCI Bus Driver instances.
// It is templated on |PlatformContextType| so that platform specific context can be provided to
// each root as necessary. For instance, in ACPI systems this contains the ACPI object for the PCI
// root to work with ACPICA.
//
// Many methods may overlap between platforms, but the metadata that a given platform may need to
// track can vary. To support this a PcirootBase class templated off the context type is provided
// here and platforms are expected to derive it and override the methods they need to implement.

class PcirootBase : public ddk::Device<PcirootBase>,
                    public ddk::PcirootProtocol<PcirootBase, ddk::base_protocol> {
 public:
  PcirootBase(PciRootHost* host, zx_device_t* parent, const char* name)
      : ddk::Device<PcirootBase>(parent), root_host_(host) {}
  virtual ~PcirootBase() = default;
  zx_status_t PcirootGetAuxdata(const char* args, void* out_data, size_t data_size,
                                size_t* out_data_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  virtual zx_status_t PcirootGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  virtual zx_status_t PcirootConnectSysmem(zx::handle handle) { return ZX_ERR_NOT_SUPPORTED; }
  virtual zx_status_t PcirootGetPciPlatformInfo(pci_platform_info_t* info) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // If |true| is returned by the Pciroot implementation then the bus driver
  // will send all config space reads and writes over the Pciroot protocol
  // rather than in the bus driver using MMIO/IO access. This exists to work
  // with non-standard PCI implementations that require controller configuration
  // before accessing a given device.
  virtual bool PcirootDriverShouldProxyConfig() {
    // By default, if a platform has MMIO based ECAMs (MMCFG) then we assume it
    // is safe to have config handled in the bus driver through MMIO. This can
    // be overriden by a given derived Pciroot implementation for a specific
    // board target.
    return !root_host_->mcfgs().empty();
  }

  // Config space read/write accessors for PCI systems that require platform
  // bus to configure something before config space is accessible. For ACPI
  // systems we only intend to use PIO access if MMIO config is unavailable.
  // In the event we do use them though, we're restricted to the base 256 byte
  // PCI config header.
  virtual zx_status_t PcirootConfigRead8(const pci_bdf_t* address, uint16_t offset,
                                         uint8_t* value) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  virtual zx_status_t PcirootConfigRead16(const pci_bdf_t* address, uint16_t offset,
                                          uint16_t* value) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  virtual zx_status_t PcirootConfigRead32(const pci_bdf_t* address, uint16_t offset,
                                          uint32_t* value) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  virtual zx_status_t PcirootConfigWrite8(const pci_bdf_t* address, uint16_t offset,
                                          uint8_t value) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  virtual zx_status_t PcirootConfigWrite16(const pci_bdf_t* address, uint16_t offset,
                                           uint16_t value) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  virtual zx_status_t PcirootConfigWrite32(const pci_bdf_t* address, uint16_t offset,
                                           uint32_t value) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  virtual zx_status_t PcirootAllocateMsi(uint32_t msi_cnt, bool can_target_64bit,
                                         zx::msi* allocation) {
    // AllocateMsi already uses platform specific MSI impleemnation methods and
    // syscalls, so this likely suits most platforms.
    return root_host_->AllocateMsi(msi_cnt, allocation);
  }

  // Allocate out of the IO / MMIO32 allocators if required, otherwise try to use whichever
  // MMIO allocator can fulfill the given request of specified base and size.
  virtual zx_status_t PcirootGetAddressSpace(zx_paddr_t in_base, size_t size,
                                             pci_address_space_t type, bool low, uint64_t* out_base,
                                             zx::resource* out_resource,
                                             zx::eventpair* out_eventpair) {
    if (type == PCI_ADDRESS_SPACE_IO) {
      auto result = root_host_->AllocateIoWindow(in_base, size, out_resource, out_eventpair);
      if (result.is_ok()) {
        *out_base = result.value();
      }
      return result.status_value();
    }

    if (!low) {
      auto result = root_host_->AllocateMmio64Window(in_base, size, out_resource, out_eventpair);
      if (result.is_ok()) {
        *out_base = result.value();
      }
      return result.status_value();
    }

    auto result = root_host_->AllocateMmio32Window(in_base, size, out_resource, out_eventpair);
    if (result.is_ok()) {
      *out_base = result.value();
    }
    return result.status_value();
  }

  zx_status_t PcirootFreeAddressSpace(uint64_t base, size_t size, pci_address_space_t type) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // DDK mix-in impls
  void DdkRelease() { delete this; }

 protected:
  // TODO(32978): presently, pciroot instances will always outlive the root host
  // it references here because it exists within the same devhost process as a
  // singleton. This will be updated when the pciroot implementation changes to
  // move away from a standalone banjo protocol.
  PciRootHost* root_host_;
  zx_device_t* platform_bus_;
};

#endif  // ifndef __cplusplus
#endif  // SRC_DEVICES_PCI_LIB_PCI_INCLUDE_LIB_PCI_ROOT_H_
