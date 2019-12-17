// Copyright 2018 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_PCIE_INCLUDE_DEV_ADDRESS_PROVIDER_ADDRESS_PROVIDER_H_
#define ZIRCON_KERNEL_DEV_PCIE_INCLUDE_DEV_ADDRESS_PROVIDER_ADDRESS_PROVIDER_H_

#include <sys/types.h>
#include <zircon/types.h>

#include <dev/address_provider/ecam_region.h>
#include <dev/pci_config.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>

// PcieAddressProvider is an interface that implements translation from a BDF to
// a PCI ECAM address.
class PcieAddressProvider {
 public:
  virtual ~PcieAddressProvider() {}

  // Accepts a PCI BDF triple and returns ZX_OK if it is able to translate it
  // into an ECAM address.
  // Upon success, virt will contain the ECAM address provided by the
  // translation. phys will optionally contain the corresponding physical
  // address.
  // On failure, result must not be touched by the implementation.
  virtual zx_status_t Translate(uint8_t bus_id, uint8_t device_id, uint8_t function_id,
                                vaddr_t* virt, paddr_t* phys) = 0;

  // Creates a config that corresponds to the type of the PcieAddressProvider.
  // For example, a PioAddressProvider will return a PioConfig whereas an
  // MmioAddressProvider will return an MmioConfig.
  virtual fbl::RefPtr<PciConfig> CreateConfig(const uintptr_t addr) = 0;

 protected:
  PcieAddressProvider() {}
};

// Concrete implementations.

// Systems that have memory mapped Config Spaces
class MmioPcieAddressProvider : public PcieAddressProvider {
 public:
  MmioPcieAddressProvider() {}
  ~MmioPcieAddressProvider();

  zx_status_t Translate(uint8_t bus_id, uint8_t device_id, uint8_t function_id, vaddr_t* virt,
                        paddr_t* phys) override;
  fbl::RefPtr<PciConfig> CreateConfig(const uintptr_t addr) override;

  zx_status_t AddEcamRegion(const PciEcamRegion& ecam);

 private:
  mutable DECLARE_MUTEX(PcieAddressProvider) ecam_region_lock_;
  fbl::WAVLTree<uint8_t, ktl::unique_ptr<MappedEcamRegion>> ecam_regions_;
};

// Systems that have PIO mapped Config Spaces
class PioPcieAddressProvider : public PcieAddressProvider {
 public:
  PioPcieAddressProvider() {}

  zx_status_t Translate(uint8_t bus_id, uint8_t device_id, uint8_t function_id, vaddr_t* virt,
                        paddr_t* phys) override;
  fbl::RefPtr<PciConfig> CreateConfig(const uintptr_t addr) override;
};

class DesignWarePcieAddressProvider : public PcieAddressProvider {
 public:
  DesignWarePcieAddressProvider() {}
  ~DesignWarePcieAddressProvider() {}

  zx_status_t Init(const PciEcamRegion& root_bridge, const PciEcamRegion& downstream_device);

  zx_status_t Translate(uint8_t bus_id, uint8_t device_id, uint8_t function_id, vaddr_t* virt,
                        paddr_t* phys) override;
  fbl::RefPtr<PciConfig> CreateConfig(const uintptr_t addr) override;

 private:
  ktl::unique_ptr<MappedEcamRegion> root_bridge_region_;
  ktl::unique_ptr<MappedEcamRegion> downstream_region_;
};

#endif  // ZIRCON_KERNEL_DEV_PCIE_INCLUDE_DEV_ADDRESS_PROVIDER_ADDRESS_PROVIDER_H_
