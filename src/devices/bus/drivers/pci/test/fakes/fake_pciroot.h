// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_PCIROOT_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_PCIROOT_H_

#include <zircon/errors.h>

#include <ddktl/protocol/pciroot.h>

#include "fake_ecam.h"

// This FakePciroot class for the moment is a stub and test files
// will specialize the methods they need. Eventually when more tests
// are sorted out it may make sense to have pciroot tests be similar
// to the mock-i2c style fakes.
class FakePciroot : public ddk::PcirootProtocol<FakePciroot> {
 public:
  // By default, pciroot won't populate an ecam unless it's called with Create().
  FakePciroot(uint8_t bus_start = 0, uint8_t bus_end = 0)
      : bus_start_(bus_start),
        bus_end_(bus_end),
        proto_({&pciroot_protocol_ops_, this}),
        ecam_(bus_start, bus_end) {}

  // Allow move.
  FakePciroot(FakePciroot&&) = default;
  FakePciroot& operator=(FakePciroot&&) = default;
  // Disallow copy.
  FakePciroot(const FakePciroot&) = delete;
  FakePciroot& operator=(const FakePciroot&) = delete;

  const pciroot_protocol_t* proto() const { return &proto_; }
  FakeEcam& ecam() { return ecam_; }
  uint8_t bus_start() { return bus_start_; }
  uint8_t bus_end() { return bus_end_; }
  int32_t allocation_cnt() { return allocation_cnt_; }

  // Protocol methods.
  zx_status_t PcirootGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PcirootConnectSysmem(zx::channel connection) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PcirootGetPciPlatformInfo(pci_platform_info_t* info) { return ZX_ERR_NOT_SUPPORTED; }
  bool PcirootDriverShouldProxyConfig(void) { return false; }
  zx_status_t PcirootConfigRead8(const pci_bdf_t* address, uint16_t offset, uint8_t* value) {
    if (address->bus_id < bus_start_ || address->bus_id > bus_end_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    memcpy(value, &ecam_.get(*address).ext_config[offset], sizeof(*value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigRead16(const pci_bdf_t* address, uint16_t offset, uint16_t* value) {
    if (address->bus_id < bus_start_ || address->bus_id > bus_end_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    memcpy(value, &ecam_.get(*address).ext_config[offset], sizeof(*value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigRead32(const pci_bdf_t* address, uint16_t offset, uint32_t* value) {
    if (address->bus_id < bus_start_ || address->bus_id > bus_end_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    memcpy(value, &ecam_.get(*address).ext_config[offset], sizeof(*value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigWrite8(const pci_bdf_t* address, uint16_t offset, uint8_t value) {
    if (address->bus_id < bus_start_ || address->bus_id > bus_end_) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    memcpy(&ecam_.get(*address).ext_config[offset], &value, sizeof(value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigWrite16(const pci_bdf_t* address, uint16_t offset, uint16_t value) {
    if (address->bus_id < bus_start_ || address->bus_id > bus_end_) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    memcpy(&ecam_.get(*address).ext_config[offset], &value, sizeof(value));
    return ZX_OK;
  }
  zx_status_t PcirootConfigWrite32(const pci_bdf_t* address, uint16_t offset, uint32_t value) {
    if (address->bus_id < bus_start_ || address->bus_id > bus_end_) {
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
  uint8_t bus_start_ = 0;
  uint8_t bus_end_ = 0;
  pciroot_protocol_t proto_;
  FakeEcam ecam_;
  int32_t allocation_cnt_ = 0;
};

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_PCIROOT_H_
