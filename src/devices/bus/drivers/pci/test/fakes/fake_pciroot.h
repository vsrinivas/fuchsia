// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_PCIROOT_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_PCIROOT_H_

#include <assert.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake-resource/resource.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <zircon/errors.h>
#include <zircon/syscalls/resource.h>

#include <vector>

#include <fbl/array.h>

#include "src/devices/bus/drivers/pci/test/fakes/fake_ecam.h"

// This FakePciroot class for the moment is a stub and test files
// will specialize the methods they need. Eventually when more tests
// are sorted out it may make sense to have pciroot tests be similar
// to the mock-i2c style fakes.
class FakePciroot : public ddk::PcirootProtocol<FakePciroot> {
 public:
  static constexpr uint64_t kDefaultHighMemoryAddress = (1llu << 32);
  static constexpr uint32_t kDefaultLowMemoryAddress = (1u << 10);
  static constexpr uint16_t kDefaultIoAddress = 0x10;
  // By default, pciroot won't populate an ecam unless it's called with Create().
  explicit FakePciroot(uint8_t bus_start = 0, uint8_t bus_end = 0)
      : proto_({&pciroot_protocol_ops_, this}),
        ecam_(bus_start, bus_end),
        info_{
            .name = "fakroot",
            .start_bus_num = bus_start,
            .end_bus_num = bus_end,
            .ecam_vmo = ecam_.vmo()->get(),
        } {
    ZX_ASSERT(fake_root_resource_create(resource_.reset_and_get_address()) == ZX_OK);
    ZX_ASSERT(fake_bti_create(bti_.reset_and_get_address()) == ZX_OK);
    sysmem_.reset(0x5359534D);  // SYSM
  }

  // Allow move.
  FakePciroot(FakePciroot&&) = default;
  FakePciroot& operator=(FakePciroot&&) = default;
  // Disallow copy.
  FakePciroot(const FakePciroot&) = delete;
  FakePciroot& operator=(const FakePciroot&) = delete;

  pciroot_protocol_t* proto() { return &proto_; }
  pci_platform_info_t info() {
    info_.legacy_irqs_list = legacy_irqs_.data();
    info_.legacy_irqs_count = legacy_irqs_.size();
    info_.irq_routing_list = routing_entries_.data();
    info_.irq_routing_count = routing_entries_.size();
    return info_;
  }
  FakeEcam& ecam() { return ecam_; }
  uint8_t bus_start() const { return info_.start_bus_num; }
  uint8_t bus_end() const { return info_.end_bus_num; }
  zx::bti& bti() { return bti_; }
  zx::resource& resource() { return resource_; }
  auto& legacy_irqs() { return legacy_irqs_; }
  auto& routing_entries() { return routing_entries_; }
  auto& allocation_eps() { return allocation_eps_; }

  // Protocol methods.
  zx_status_t PcirootGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) {
    if (!enable_get_bti_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    return bti_.duplicate(ZX_RIGHT_SAME_RIGHTS, bti);
  }

  zx_status_t PcirootConnectSysmem(zx::channel connection) {
    if (!enable_connect_sysmem_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PcirootGetPciPlatformInfo(pci_platform_info_t* out_info) {
    if (!enable_get_pci_platform_info_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    *out_info = info();
    return ZX_OK;
  }

  bool PcirootDriverShouldProxyConfig() { return enable_driver_should_proxy_config_; }
  zx_status_t PcirootConfigRead8(const pci_bdf_t* address, uint16_t offset, uint8_t* value) {
    if (!enable_config_read_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    if (address->bus_id < info_.start_bus_num || address->bus_id > info_.end_bus_num) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    memcpy(value, &ecam_.get(*address).ext_config[offset], sizeof(*value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigRead16(const pci_bdf_t* address, uint16_t offset, uint16_t* value) {
    if (!enable_config_read_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    if (address->bus_id < info_.start_bus_num || address->bus_id > info_.end_bus_num) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    memcpy(value, &ecam_.get(*address).ext_config[offset], sizeof(*value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigRead32(const pci_bdf_t* address, uint16_t offset, uint32_t* value) {
    if (!enable_config_read_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    if (address->bus_id < info_.start_bus_num || address->bus_id > info_.end_bus_num) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    memcpy(value, &ecam_.get(*address).ext_config[offset], sizeof(*value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigWrite8(const pci_bdf_t* address, uint16_t offset, uint8_t value) {
    if (!enable_config_write_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    if (address->bus_id < info_.start_bus_num || address->bus_id > info_.end_bus_num) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    memcpy(&ecam_.get(*address).ext_config[offset], &value, sizeof(value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigWrite16(const pci_bdf_t* address, uint16_t offset, uint16_t value) {
    if (!enable_config_write_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    if (address->bus_id < info_.start_bus_num || address->bus_id > info_.end_bus_num) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    memcpy(&ecam_.get(*address).ext_config[offset], &value, sizeof(value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigWrite32(const pci_bdf_t* address, uint16_t offset, uint32_t value) {
    if (!enable_config_write_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    if (address->bus_id < info_.start_bus_num || address->bus_id > info_.end_bus_num) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    memcpy(&ecam_.get(*address).ext_config[offset], &value, sizeof(value));
    return ZX_OK;
  }

  zx_status_t PcirootAllocateMsi(uint32_t requested_irqs, bool can_target_64bit,
                                 zx::msi* out_allocation) {
    if (!enable_allocate_msi_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    return zx_msi_allocate(ZX_HANDLE_INVALID, requested_irqs,
                           out_allocation->reset_and_get_address());
  }

  zx_status_t PcirootGetAddressSpace(zx_paddr_t in_base, size_t size, pci_address_space_t type,
                                     bool low, uint64_t* out_base, zx::resource* resource,
                                     zx::eventpair* eventpair) {
    if (!enable_get_address_space_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    zx_rsrc_kind_t kind =
        (type == PCI_ADDRESS_SPACE_MEMORY) ? ZX_RSRC_KIND_MMIO : ZX_RSRC_KIND_IOPORT;
    if (in_base) {
      *out_base = in_base;
    } else {
      if (type == PCI_ADDRESS_SPACE_MEMORY) {
      }
      if (type == PCI_ADDRESS_SPACE_MEMORY) {
        if (low) {
          *out_base = kDefaultLowMemoryAddress;
        } else {
          *out_base = kDefaultHighMemoryAddress;
        }
      } else {
        *out_base = kDefaultIoAddress;
      }
    }

    ZX_ASSERT(zx::resource::create(resource_, kind, *out_base, size, "fake", 5, resource) == ZX_OK);
    zx::eventpair local_ep;
    zx::eventpair::create(0, &local_ep, eventpair);
    allocation_eps_.push_back(std::move(local_ep));
    return ZX_OK;
  }

  void enable_get_bti(bool enable) { enable_get_bti_ = enable; }
  void enable_connect_sysmem(bool enable) { enable_connect_sysmem_ = enable; }
  void enable_get_pci_platform_info(bool enable) { enable_get_pci_platform_info_ = enable; }
  void enable_driver_should_proxy_config(bool enable) {
    enable_driver_should_proxy_config_ = enable;
  }
  void enable_config_read(bool enable) { enable_config_read_ = enable; }
  void enable_config_write(bool enable) { enable_config_write_ = enable; }
  void enable_allocate_msi(bool enable) { enable_allocate_msi_ = enable; }
  void enable_get_address_space(bool enable) { enable_get_address_space_ = enable; }

 private:
  pciroot_protocol_t proto_;
  FakeEcam ecam_;
  pci_platform_info_t info_;
  std::vector<zx::eventpair> allocation_eps_;
  zx::bti bti_;
  zx::resource resource_;
  zx::channel sysmem_;
  std::vector<pci_legacy_irq_t> legacy_irqs_;
  std::vector<pci_irq_routing_entry_t> routing_entries_;

  // Switches so tests can test error paths of PCIRoot usage.
  bool enable_get_bti_ = true;
  bool enable_connect_sysmem_ = true;
  bool enable_get_pci_platform_info_ = true;
  bool enable_driver_should_proxy_config_ = false;
  bool enable_config_read_ = true;
  bool enable_config_write_ = true;
  bool enable_allocate_msi_ = true;
  bool enable_get_address_space_ = true;
};

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_PCIROOT_H_
