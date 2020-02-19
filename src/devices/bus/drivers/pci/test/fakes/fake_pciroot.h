// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_PCIROOT_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_PCIROOT_H_

#include <ddktl/protocol/pciroot.h>

#include "fake_ecam.h"

// This FakePciroot class for the moment is a stub and test files
// will specialize the methods they need. Eventually when more tests
// are sorted out it may make sense to have pciroot tests be similar
// to the mock-i2c style fakes.
class FakePciroot : public ddk::PcirootProtocol<FakePciroot> {
 public:
  // By default, pciroot won't populate an ecam unless it's called with Create().
  static zx_status_t Create(uint8_t bus_start, uint8_t bus_end, std::unique_ptr<FakePciroot>* out) {
    std::optional<FakeEcam> ecam;
    zx_status_t st = FakeEcam::Create(bus_start, bus_end, &ecam);
    if (st != ZX_OK) {
      return st;
    }

    *out = std::unique_ptr<FakePciroot>(new FakePciroot(bus_start, bus_end, std::move(*ecam)));
    return ZX_OK;
  }

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
  zx_status_t PcirootGetAuxdata(const char* args, void* out_data, size_t data_size,
                                size_t* out_data_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PcirootGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PcirootConnectSysmem(zx::handle handle) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PcirootGetPciPlatformInfo(pci_platform_info_t* info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PcirootGetPciIrqInfo(pci_irq_info_t* info) { return ZX_ERR_NOT_SUPPORTED; }
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
  zx_status_t PcirootAllocMsiBlock(uint64_t requested_irqs, bool can_target_64bit,
                                   msi_block_t* out_block) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PcirootFreeMsiBlock(const msi_block_t* block) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PcirootMaskUnmaskMsi(uint64_t msi_id, bool mask) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PcirootGetAddressSpace(zx_paddr_t in_base, size_t len, pci_address_space_t type,
                                     bool low, uint64_t* out_base, zx::resource* resource) {
    allocation_cnt_++;
    return ZX_OK;
  }
  zx_status_t PcirootFreeAddressSpace(uint64_t base, size_t len, pci_address_space_t type) {
    allocation_cnt_--;
    return ZX_OK;
  }

 private:
  FakePciroot(uint8_t bus_start, uint8_t bus_end, FakeEcam&& ecam)
      : bus_start_(bus_start),
        bus_end_(bus_end),
        proto_({&pciroot_protocol_ops_, this}),
        ecam_(std::move(ecam)) {}
  uint8_t bus_start_ = 0;
  uint8_t bus_end_ = 0;
  pciroot_protocol_t proto_;
  FakeEcam ecam_;
  int32_t allocation_cnt_ = 0;
};

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_PCIROOT_H_
