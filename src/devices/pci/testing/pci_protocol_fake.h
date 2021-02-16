// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_PCI_TESTING_PCI_PROTOCOL_FAKE_H_
#define SRC_DEVICES_PCI_TESTING_PCI_PROTOCOL_FAKE_H_
#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/fake-bti/bti.h>
#include <lib/zx/interrupt.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <array>
#include <optional>
#include <vector>

#include <ddk/device.h>
#include <fbl/algorithm.h>

// These are here to prevent a large dependency chain from including the userspace
// PCI driver's headers.
#define PCI_DEVICE_BAR_COUNT 6
#define MSI_MAX_VECTORS 32
#define PCI_CFG_HEADER_SIZE 64
#define kFakePciInternalError "Internal FakePciProtocol Error"

namespace pci {
class FakePciProtocol : public ddk::PciProtocol<FakePciProtocol> {
 public:
  FakePciProtocol() { reset(); }

  struct FakeBar {
    size_t size;
    zx::vmo vmo;
  };

  zx_status_t PciGetBar(uint32_t bar_id, zx_pci_bar_t* out_res) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_handle) {
    if (!out_handle) {
      return ZX_ERR_INVALID_ARGS;
    }

    switch (irq_mode_) {
      case PCI_IRQ_MODE_LEGACY:
        if (which_irq > 0) {
          return ZX_ERR_INVALID_ARGS;
        }
        return legacy_interrupt_->duplicate(ZX_RIGHT_SAME_RIGHTS, out_handle);
      case PCI_IRQ_MODE_MSI:
        if (which_irq >= msi_interrupts_.size()) {
          return ZX_ERR_INVALID_ARGS;
        }
        return msi_interrupts_[which_irq].duplicate(ZX_RIGHT_SAME_RIGHTS, out_handle);
      case PCI_IRQ_MODE_MSI_X:
        if (which_irq >= msix_interrupts_.size()) {
          return ZX_ERR_INVALID_ARGS;
        }
        return msix_interrupts_[which_irq].duplicate(ZX_RIGHT_SAME_RIGHTS, out_handle);
    }

    return ZX_ERR_BAD_STATE;
  }

  zx_status_t PciConfigureIrqMode(uint32_t requested_irq_count) {
    ZX_ASSERT(requested_irq_count);
    if (msix_interrupts_.size() >= requested_irq_count) {
      irq_mode_ = PCI_IRQ_MODE_MSI_X;
      irq_cnt_ = requested_irq_count;
      return ZX_OK;
    }

    if (msi_interrupts_.size() >= requested_irq_count) {
      irq_mode_ = PCI_IRQ_MODE_MSI;
      irq_cnt_ = requested_irq_count;
      return ZX_OK;
    }

    if (legacy_interrupt_ && requested_irq_count == 1) {
      irq_mode_ = PCI_IRQ_MODE_LEGACY;
      irq_cnt_ = requested_irq_count;
      return ZX_OK;
    }

    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PciQueryIrqMode(zx_pci_irq_mode_t mode, uint32_t* out_max_irqs) {
    ZX_ASSERT(out_max_irqs);
    ZX_ASSERT(mode < PCI_IRQ_MODE_COUNT);

    switch (mode) {
      case PCI_IRQ_MODE_LEGACY:
        if (legacy_interrupt_) {
          *out_max_irqs = 1;
          return ZX_OK;
        }
        break;
      case PCI_IRQ_MODE_MSI:
        if (!msi_interrupts_.empty()) {
          // MSI interrupts are only supported in powers of 2.
          *out_max_irqs = (msi_interrupts_.size() <= 1)
                              ? msi_interrupts_.size()
                              : fbl::round_down(msi_interrupts_.size(), 2u);
          return ZX_OK;
        }
        break;
      case PCI_IRQ_MODE_MSI_X:
        if (!msix_interrupts_.empty()) {
          *out_max_irqs = msix_interrupts_.size();
          return ZX_OK;
          break;
        }
    }
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PciSetIrqMode(zx_pci_irq_mode_t mode, uint32_t requested_irq_count) {
    switch (mode) {
      case PCI_IRQ_MODE_LEGACY:
        if (requested_irq_count > 1) {
          return ZX_ERR_INVALID_ARGS;
        }

        if (legacy_interrupt_) {
          irq_mode_ = PCI_IRQ_MODE_LEGACY;
          irq_cnt_ = 1;
        }
        return ZX_OK;
      case PCI_IRQ_MODE_MSI:
        if (msi_interrupts_.empty()) {
          break;
        }
        if (!fbl::is_pow2(requested_irq_count) || requested_irq_count > MSI_MAX_VECTORS) {
          return ZX_ERR_INVALID_ARGS;
        }
        if (msi_interrupts_.size() < requested_irq_count) {
          return ZX_ERR_INVALID_ARGS;
        }
        irq_mode_ = PCI_IRQ_MODE_MSI;
        irq_cnt_ = requested_irq_count;
        return ZX_OK;
      case PCI_IRQ_MODE_MSI_X:
        if (msix_interrupts_.empty()) {
          break;
        }

        if (msix_interrupts_.size() < requested_irq_count) {
          return ZX_ERR_INVALID_ARGS;
        }
        irq_mode_ = PCI_IRQ_MODE_MSI_X;
        irq_cnt_ = requested_irq_count;
        return ZX_OK;
    }

    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PciEnableBusMaster(bool enable) {
    bus_master_en_ = enable;
    return ZX_OK;
  }

  zx_status_t PciResetDevice() {
    reset_cnt_++;
    return ZX_OK;
  }

  zx_status_t PciGetDeviceInfo(zx_pcie_device_info_t* out_info) {
    ZX_ASSERT(out_info);
    *out_info = info_;
    return ZX_OK;
  }

  template <typename T>
  zx_status_t ConfigRead(uint16_t offset, T* out_value) {
    ZX_ASSERT_MSG(offset + sizeof(T) <= PCI_BASE_CONFIG_SIZE,
                  "FakePciProtocol: PciConfigRead reads must fit in the range [%#x, %#x] (offset "
                  "= %#x, io width = %#lx).",
                  0, PCI_BASE_CONFIG_SIZE - 1, offset, sizeof(T));
    return config_.read(out_value, offset, sizeof(T));
  }

  constexpr zx_status_t PciConfigRead8(uint16_t offset, uint8_t* out_value) {
    return ConfigRead(offset, out_value);
  }

  constexpr zx_status_t PciConfigRead16(uint16_t offset, uint16_t* out_value) {
    return ConfigRead(offset, out_value);
  }

  constexpr zx_status_t PciConfigRead32(uint16_t offset, uint32_t* out_value) {
    return ConfigRead(offset, out_value);
  }

  template <typename T>
  zx_status_t ConfigWrite(uint16_t offset, T value) {
    ZX_ASSERT_MSG(offset >= PCI_CFG_HEADER_SIZE && offset + sizeof(T) <= PCI_BASE_CONFIG_SIZE,
                  "FakePciProtocol: PciConfigWrite writes must fit in the range [%#x, %#x] (offset "
                  "= %#x, io width = %#lx).",
                  PCI_CFG_HEADER_SIZE, PCI_BASE_CONFIG_SIZE - 1, offset, sizeof(T));
    return config_.write(&value, offset, sizeof(T));
  }

  zx_status_t PciConfigWrite8(uint16_t offset, uint8_t value) { return ConfigWrite(offset, value); }

  zx_status_t PciConfigWrite16(uint16_t offset, uint16_t value) {
    return ConfigWrite(offset, value);
  }

  zx_status_t PciConfigWrite32(uint16_t offset, uint32_t value) {
    return ConfigWrite(offset, value);
  }

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

  zx_status_t PciGetBti(uint32_t index, zx::bti* out_bti) {
    return bti_.duplicate(ZX_RIGHT_SAME_RIGHTS, out_bti);
  }

  // Returns a |pci_protocol_t| suitable for use with the C api, or passing to a
  // ddk::PciProtocolClient.
  const pci_protocol_t& get_protocol() { return protocol_; }

  // Support methods for tests

  // Add an interrupt for the specified PCI interrupt mode. A borrowed copy of the zx::interrupt
  // is returned for use in tests.
  zx::unowned_interrupt AddLegacyInterrupt() { return AddInterrupt(PCI_IRQ_MODE_LEGACY); }
  zx::unowned_interrupt AddMsiInterrupt() { return AddInterrupt(PCI_IRQ_MODE_MSI); }
  zx::unowned_interrupt AddMsixInterrupt() { return AddInterrupt(PCI_IRQ_MODE_MSI_X); }
  zx::unowned_interrupt AddInterrupt(pci_irq_mode_t mode) {
    ZX_ASSERT_MSG(!(mode == PCI_IRQ_MODE_LEGACY && legacy_interrupt_),
                  "FakePciProtocol Error: Legacy interrupt mode only supports 1 interrupt.");
    ZX_ASSERT_MSG(!(mode == PCI_IRQ_MODE_MSI && msi_interrupts_.size() == MSI_MAX_VECTORS),
                  "FakePciProtocol Error: MSI interrupt mode only supports up to %u interrupts.",
                  MSI_MAX_VECTORS);

    zx::interrupt interrupt{};
    zx_status_t status = zx::interrupt::create(*zx::unowned_resource(ZX_HANDLE_INVALID), 0,
                                               ZX_INTERRUPT_VIRTUAL, &interrupt);
    ZX_ASSERT_MSG(status == ZX_OK, kFakePciInternalError);
    zx::unowned_interrupt borrow = interrupt.borrow();

    switch (mode) {
      case PCI_IRQ_MODE_LEGACY:
        legacy_interrupt_ = std::move(interrupt);
        break;
      case PCI_IRQ_MODE_MSI:
        msi_interrupts_.push_back(std::move(interrupt));
        break;
      case PCI_IRQ_MODE_MSI_X:
        msix_interrupts_.push_back(std::move(interrupt));
        break;
    }

    return borrow;
  }

  zx_pcie_device_info_t SetDeviceInfo(zx_pcie_device_info_t info) {
    info_ = info;
    config_.write(&info_.vendor_id, PCI_CFG_VENDOR_ID, sizeof(info_.vendor_id));
    config_.write(&info_.device_id, PCI_CFG_DEVICE_ID, sizeof(info_.device_id));
    config_.write(&info_.revision_id, PCI_CFG_REVISION_ID, sizeof(info_.revision_id));
    config_.write(&info_.base_class, PCI_CFG_CLASS_CODE_BASE, sizeof(info_.base_class));
    config_.write(&info_.sub_class, PCI_CFG_CLASS_CODE_SUB, sizeof(info_.sub_class));
    config_.write(&info_.program_interface, PCI_CFG_CLASS_CODE_INTR,
                  sizeof(info_.program_interface));

    return info;
  }

  zx::unowned_vmo GetConfigVmo() { return config_.borrow(); }
  pci_irq_mode_t GetIrqMode() const { return irq_mode_; }
  uint32_t GetIrqCount() const { return irq_cnt_; }
  uint32_t GetResetCount() const { return reset_cnt_; }

  // Returns the state of the device's Bus Mastering setting. Returned as an optional
  // so that the caller can differentiate between off and "never set" states in driver
  // testing.
  std::optional<bool> GetBusMasterEnabled() const { return bus_master_en_; }

  void reset() {
    legacy_interrupt_ = std::nullopt;
    msi_interrupts_.clear();
    msix_interrupts_.clear();
    irq_mode_ = PCI_IRQ_MODE_DISABLED;
    irq_cnt_ = 0;

    bus_master_en_ = std::nullopt;
    reset_cnt_ = 0;
    info_ = {};

    zx_status_t status = zx::vmo::create(PCI_BASE_CONFIG_SIZE, /*options=*/0, &config_);
    ZX_ASSERT(status == ZX_OK);
    status = fake_bti_create(bti_.reset_and_get_address());
    ZX_ASSERT(status == ZX_OK);
  }

 private:
  // Interrupts
  std::optional<zx::interrupt> legacy_interrupt_;
  std::vector<zx::interrupt> msi_interrupts_;
  std::vector<zx::interrupt> msix_interrupts_;
  pci_irq_mode_t irq_mode_;
  uint32_t irq_cnt_;

  zx::bti bti_;
  uint32_t reset_cnt_;
  std::optional<bool> bus_master_en_;
  zx_pcie_device_info_t info_ = {};
  zx::vmo config_;
  pci_protocol_t protocol_ = {.ops = &pci_protocol_ops_, .ctx = this};
};

}  // namespace pci

#endif  // SRC_DEVICES_PCI_TESTING_PCI_PROTOCOL_FAKE_H_
