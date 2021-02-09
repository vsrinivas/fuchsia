// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <ddk/driver.h>
#include <mmio-ptr/fake.h>

#include "intel-i915.h"
#include "interrupts.h"
#include "zxtest/zxtest.h"

namespace {

class FakePciProtocol : public ddk::PciProtocol<FakePciProtocol> {
 public:
  zx_status_t PciGetBar(uint32_t bar_id, zx_pci_bar_t* out_res) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_handle) {
    irq_mapped_ = which_irq;
    return ZX_OK;
  }

  zx_status_t PciConfigureIrqMode(uint32_t requested_irq_count) {
    requested_irq_count_ = requested_irq_count;
    return ZX_OK;
  }

  zx_status_t PciQueryIrqMode(zx_pci_irq_mode_t mode, uint32_t* out_max_irqs) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PciSetIrqMode(zx_pci_irq_mode_t mode, uint32_t requested_irq_count) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PciEnableBusMaster(bool enable) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciResetDevice() { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciGetDeviceInfo(zx_pcie_device_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciConfigRead8(uint16_t offset, uint8_t* out_value) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciConfigRead16(uint16_t offset, uint16_t* out_value) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciConfigRead32(uint16_t offset, uint32_t* out_value) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciConfigWrite8(uint16_t offset, uint8_t value) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciConfigWrite16(uint16_t offset, uint16_t value) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciConfigWrite32(uint16_t offset, uint32_t value) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciGetFirstCapability(uint8_t id, uint8_t* out_offset) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PciGetNextCapability(uint8_t id, uint8_t offset, uint8_t* out_offset) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PciGetFirstExtendedCapability(uint16_t id, uint16_t* out_offset) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PciGetNextExtendedCapability(uint16_t id, uint16_t offset, uint16_t* out_offset) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PciGetBti(uint32_t index, zx::bti* out_bti) { return ZX_ERR_NOT_SUPPORTED; }

  pci_protocol_t* get() { return &protocol_; }

  std::optional<bool> bus_master_enable_;
  std::optional<uint32_t> irq_mapped_;
  std::optional<uint32_t> requested_irq_count_;

 private:
  pci_protocol_t protocol_ = {.ops = &pci_protocol_ops_, .ctx = this};
};

TEST(IntelI915Display, InterruptInit) {
  i915::Controller controller(nullptr);

  constexpr uint32_t kMinimumRegCount = 0xd0000 / sizeof(uint32_t);
  std::vector<uint32_t> regs(kMinimumRegCount);
  mmio_buffer_t buffer{.vaddr = FakeMmioPtr(regs.data()),
                       .offset = 0,
                       .size = regs.size() * sizeof(uint32_t),
                       .vmo = ZX_HANDLE_INVALID};
  controller.SetMmioForTesting(ddk::MmioBuffer(buffer));

  FakePciProtocol pci{};
  controller.SetPciForTesting(*pci.get());

  constexpr bool kStartThread = false;
  EXPECT_EQ(ZX_OK, controller.interrupts()->Init(kStartThread));

  ASSERT_TRUE(pci.irq_mapped_.has_value());
  EXPECT_EQ(0, pci.irq_mapped_);

  ASSERT_TRUE(pci.requested_irq_count_.has_value());
  EXPECT_EQ(1, pci.requested_irq_count_);

  // Unset so controller teardown doesn't crash.
  controller.ResetMmioSpaceForTesting();
}

}  // namespace
