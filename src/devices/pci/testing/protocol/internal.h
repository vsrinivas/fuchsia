// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/fake-bti/bti.h>
#include <zircon/hw/pci.h>

#include <ddktl/device.h>
#include <fbl/algorithm.h>

#ifndef SRC_DEVICES_PCI_TESTING_PROTOCOL_INTERNAL_H_
#define SRC_DEVICES_PCI_TESTING_PROTOCOL_INTERNAL_H_

// These are here to prevent a large dependency chain from including the userspace
// PCI driver's headers.
#define PCI_DEVICE_BAR_COUNT 6
#define MSI_MAX_VECTORS 32
#define MSIX_MAX_VECTORS 8
#define PCI_CFG_HEADER_SIZE 64
#define kFakePciInternalError "Internal FakePciProtocol Error"

namespace pci {

class FakePciProtocolInternal
    : public ddk::PciProtocol<FakePciProtocolInternal, ddk::base_protocol> {
 public:
  static constexpr uint8_t kPciExpressCapabilitySize = 0x3B;

  FakePciProtocolInternal();
  ~FakePciProtocolInternal() = default;
  zx_status_t PciGetBar(uint32_t bar_id, pci_bar_t* out_res);
  zx_status_t PciAckInterrupt();
  zx_status_t PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_handle);
  zx_status_t PciConfigureIrqMode(uint32_t requested_irq_count, pci_irq_mode_t* out_irq_mode);
  zx_status_t PciQueryIrqMode(pci_irq_mode_t mode, uint32_t* out_max_irqs);
  zx_status_t PciSetIrqMode(pci_irq_mode_t mode, uint32_t requested_irq_count);
  zx_status_t PciEnableBusMaster(bool enable);
  zx_status_t PciResetDevice();
  zx_status_t PciGetDeviceInfo(pcie_device_info_t* out_info);

  constexpr zx_status_t PciConfigRead8(uint16_t offset, uint8_t* out_value) {
    return ConfigRead(offset, out_value);
  }

  constexpr zx_status_t PciConfigRead16(uint16_t offset, uint16_t* out_value) {
    return ConfigRead(offset, out_value);
  }

  constexpr zx_status_t PciConfigRead32(uint16_t offset, uint32_t* out_value) {
    return ConfigRead(offset, out_value);
  }

  zx_status_t PciConfigWrite8(uint16_t offset, uint8_t value) { return ConfigWrite(offset, value); }

  zx_status_t PciConfigWrite16(uint16_t offset, uint16_t value) {
    return ConfigWrite(offset, value);
  }

  zx_status_t PciConfigWrite32(uint16_t offset, uint32_t value) {
    return ConfigWrite(offset, value);
  }

  zx_status_t PciGetFirstCapability(uint8_t id, uint8_t* out_offset);
  zx_status_t PciGetNextCapability(uint8_t id, uint8_t offset, uint8_t* out_offset);
  zx_status_t PciGetFirstExtendedCapability(uint16_t id, uint16_t* out_offset);
  zx_status_t PciGetNextExtendedCapability(uint16_t id, uint16_t offset, uint16_t* out_offset);
  zx_status_t PciGetBti(uint32_t index, zx::bti* out_bti);

  // Returns a |pci_protocol_t| suitable for use with the C api, or passing to a
  // ddk::PciProtocolClient.
  const pci_protocol_t& get_protocol() { return protocol_; }

 protected:
  struct FakeBar {
    size_t size;
    uint32_t type;
    std::optional<zx::vmo> vmo;
  };

  struct FakeCapability {
    bool operator<(const FakeCapability& r) const { return this->position < r.position; }

    uint8_t id;
    uint8_t position;
    uint8_t size;
  };

  // Capabilities are the hardest part to implement because if a device expects a capability
  // at a given address in configuration space then it's possible they will want to write to it.
  // Additionally, vendor capabilities are of a variable size which is read from the capability
  // at runtime. To further complicate things, particular devices will have registers in their
  // configuration space that the device may be expected to use but which are not exposed
  // through any PCI base address register mechanism. This makes it risky to lay out a
  // capability wherever we wish for fear it may overlap with one of these spaces. For this
  // reason we do no validation of the capability's setup in configuration space besides writing
  // the capability id and next pointer. The test author is responsible for setting up the
  // layout of the capabilities as necessary to match their device, but we can provide helper
  // methods to ensure they're doing it properly.
  void AddCapabilityInternal(uint8_t capability_id, uint8_t position, uint8_t size);
  zx::interrupt& AddInterrupt(pci_irq_mode_t mode);
  pcie_device_info_t SetDeviceInfoInternal(pcie_device_info_t new_info);

  pci_irq_mode_t irq_mode() const { return irq_mode_; }
  uint32_t irq_cnt() const { return irq_cnt_; }
  std::array<FakeBar, PCI_DEVICE_BAR_COUNT>& bars() { return bars_; }
  std::vector<FakeCapability>& capabilities() { return capabilities_; }

  uint32_t reset_cnt() const { return reset_cnt_; }
  std::optional<bool> bus_master_en() const { return bus_master_en_; }
  pcie_device_info_t& info() { return info_; }
  zx::vmo& config() { return config_; }

  void reset();

 private:
  template <typename T>
  zx_status_t ConfigRead(uint16_t offset, T* out_value) {
    ZX_ASSERT_MSG(offset + sizeof(T) <= PCI_BASE_CONFIG_SIZE,
                  "FakePciProtocol: PciConfigRead reads must fit in the range [%#x, %#x] (offset "
                  "= %#x, io width = %#lx).",
                  0, PCI_BASE_CONFIG_SIZE - 1, offset, sizeof(T));
    return config_.read(out_value, offset, sizeof(T));
  }

  template <typename T>
  zx_status_t ConfigWrite(uint16_t offset, T value) {
    ZX_ASSERT_MSG(offset >= PCI_CFG_HEADER_SIZE && offset + sizeof(T) <= PCI_BASE_CONFIG_SIZE,
                  "FakePciProtocol: PciConfigWrite writes must fit in the range [%#x, %#x] (offset "
                  "= %#x, io width = %#lx).",
                  PCI_CFG_HEADER_SIZE, PCI_BASE_CONFIG_SIZE - 1, offset, sizeof(T));
    return config_.write(&value, offset, sizeof(T));
  }

  // This allows us to mimic the kernel's handling of outstanding MsiDispatchers per MsiAllocation
  // objects. A device's legacy interrupt is still a valid object if the interrupt mode is
  // switched, albeit not a useful one.
  bool AllMappedInterruptsFreed();
  zx_status_t CommonCapabilitySearch(uint8_t id, std::optional<uint8_t> offset,
                                     uint8_t* out_offset);

  // Interrupts
  std::optional<zx::interrupt> legacy_interrupt_;
  std::vector<zx::interrupt> msi_interrupts_;
  std::vector<zx::interrupt> msix_interrupts_;
  pci_irq_mode_t irq_mode_;
  uint32_t irq_cnt_ = 0;
  uint32_t msi_count_ = 0;
  uint32_t msix_count_ = 0;

  std::array<FakeBar, PCI_DEVICE_BAR_COUNT> bars_{};
  std::vector<FakeCapability> capabilities_;

  zx::bti bti_;
  uint32_t reset_cnt_;
  std::optional<bool> bus_master_en_;
  pcie_device_info_t info_ = {};
  zx::vmo config_;
  pci_protocol_t protocol_ = {.ops = &pci_protocol_ops_, .ctx = this};
};

}  // namespace pci

#endif  // SRC_DEVICES_PCI_TESTING_PROTOCOL_INTERNAL_H_
