// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_PCIROOT_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_PCIROOT_H_

#include <assert.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/fake-bti/bti.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <zircon/errors.h>

#include "fake_ecam.h"

// This FakePciroot class for the moment is a stub and test files
// will specialize the methods they need. Eventually when more tests
// are sorted out it may make sense to have pciroot tests be similar
// to the mock-i2c style fakes.
class FakePciroot : public ddk::PcirootProtocol<FakePciroot> {
 public:
  // By default, pciroot won't populate an ecam unless it's called with Create().
  FakePciroot(uint8_t bus_start = 0, uint8_t bus_end = 0)
      : proto_({&pciroot_protocol_ops_, this}),
        ecam_(bus_start, bus_end),
        info_{
            .name = "fakroot",
            .start_bus_num = bus_start,
            .end_bus_num = bus_end,
            .ecam_vmo = ecam_.vmo()->get(),
        } {
    ZX_ASSERT(fake_bti_create(bti_.reset_and_get_address()) == ZX_OK);
  }

  // Allow move.
  FakePciroot(FakePciroot&&) = default;
  FakePciroot& operator=(FakePciroot&&) = default;
  // Disallow copy.
  FakePciroot(const FakePciroot&) = delete;
  FakePciroot& operator=(const FakePciroot&) = delete;

  pciroot_protocol_t* proto() { return &proto_; }
  pci_platform_info_t info() { return info_; }
  FakeEcam& ecam() { return ecam_; }
  uint8_t bus_start() { return info_.start_bus_num; }
  uint8_t bus_end() { return info_.end_bus_num; }
  int32_t allocation_cnt() { return allocation_cnt_; }
  zx::bti& bti() { return bti_; }

  // Protocol methods.
  zx_status_t PcirootGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) {
    return bti_.duplicate(ZX_RIGHT_SAME_RIGHTS, bti);
  }

  zx_status_t PcirootConnectSysmem(zx::channel connection) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PcirootGetPciPlatformInfo(pci_platform_info_t* info) {
    *info = info_;
    return ZX_OK;
  }

  bool PcirootDriverShouldProxyConfig(void) { return false; }
  zx_status_t PcirootConfigRead8(const pci_bdf_t* address, uint16_t offset, uint8_t* value) {
    if (address->bus_id < info_.start_bus_num || address->bus_id > info_.end_bus_num) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    memcpy(value, &ecam_.get(*address).ext_config[offset], sizeof(*value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigRead16(const pci_bdf_t* address, uint16_t offset, uint16_t* value) {
    if (address->bus_id < info_.start_bus_num || address->bus_id > info_.end_bus_num) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    memcpy(value, &ecam_.get(*address).ext_config[offset], sizeof(*value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigRead32(const pci_bdf_t* address, uint16_t offset, uint32_t* value) {
    if (address->bus_id < info_.start_bus_num || address->bus_id > info_.end_bus_num) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    memcpy(value, &ecam_.get(*address).ext_config[offset], sizeof(*value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigWrite8(const pci_bdf_t* address, uint16_t offset, uint8_t value) {
    if (address->bus_id < info_.start_bus_num || address->bus_id > info_.end_bus_num) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    memcpy(&ecam_.get(*address).ext_config[offset], &value, sizeof(value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigWrite16(const pci_bdf_t* address, uint16_t offset, uint16_t value) {
    if (address->bus_id < info_.start_bus_num || address->bus_id > info_.end_bus_num) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    memcpy(&ecam_.get(*address).ext_config[offset], &value, sizeof(value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigWrite32(const pci_bdf_t* address, uint16_t offset, uint32_t value) {
    if (address->bus_id < info_.start_bus_num || address->bus_id > info_.end_bus_num) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    memcpy(&ecam_.get(*address).ext_config[offset], &value, sizeof(value));
    return ZX_OK;
  }

  zx_status_t PcirootAllocateMsi(uint32_t requested_irqs, bool can_target_64bit,
                                 zx::msi* out_allocation) {
    return zx_msi_allocate(ZX_HANDLE_INVALID, requested_irqs,
                           out_allocation->reset_and_get_address());
  }

  zx_status_t PcirootGetAddressSpace(zx_paddr_t in_base, size_t size, pci_address_space_t type,
                                     bool low, uint64_t* out_base, zx::resource* resource,
                                     zx::eventpair* eventpair) {
    allocation_cnt_++;
    return ZX_OK;
  }

 private:
  pciroot_protocol_t proto_;
  FakeEcam ecam_;
  pci_platform_info_t info_;
  zx::bti bti_;
  int32_t allocation_cnt_ = 0;
};

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_PCIROOT_H_
