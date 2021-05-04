// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/device-protocol/pci.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>

#include <zxtest/zxtest.h>

namespace {

class FakePci : public ddk::PciProtocol<FakePci> {
 public:
  pci_protocol_t get_proto() {
    return pci_protocol_t{
        .ops = &pci_protocol_ops_,
        .ctx = this,
    };
  }

  zx_status_t PciGetBar(uint32_t bar_id, pci_bar_t* out_res) {
    zx::vmo vmo;
    if (auto status = zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo); status != ZX_OK) {
      return status;
    }
    out_res->handle = vmo.release();
    out_res->type = ZX_PCI_BAR_TYPE_MMIO;
    return ZX_OK;
  }
  zx_status_t PciEnableBusMaster(bool enable) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciResetDevice() { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_handle) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PciConfigureIrqMode(uint32_t requested_irq_count, pci_irq_mode_t* mode) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PciQueryIrqMode(pci_irq_mode_t mode, uint32_t* out_max_irqs) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PciSetIrqMode(pci_irq_mode_t mode, uint32_t requested_irq_count) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PciAckInterrupt() { return ZX_OK; }
  zx_status_t PciGetDeviceInfo(pcie_device_info_t* out_into) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciConfigRead8(uint16_t offset, uint8_t* out_value) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciConfigRead16(uint16_t offset, uint16_t* out_value) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciConfigRead32(uint16_t offset, uint32_t* out_value) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciConfigWrite8(uint16_t offset, uint8_t value) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciConfigWrite16(uint16_t offset, uint16_t value) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciConfigWrite32(uint16_t offset, uint32_t value) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PciGetFirstCapability(uint8_t cap_id, uint8_t* out_offset) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PciGetNextCapability(uint8_t cap_id, uint8_t offset, uint8_t* out_offset) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PciGetFirstExtendedCapability(uint16_t cap_id, uint16_t* out_offset) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PciGetNextExtendedCapability(uint16_t cap_id, uint16_t offset, uint16_t* out_offset) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PciGetBti(uint32_t index, zx::bti* out_bti) { return ZX_ERR_NOT_SUPPORTED; }
};

TEST(PciTest, MapMmio) {
  FakePci fake_pci;
  auto proto = fake_pci.get_proto();
  ddk::Pci pci(proto);
  std::optional<ddk::MmioBuffer> mmio;
  EXPECT_OK(pci.MapMmio(0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio));
}

}  // namespace
