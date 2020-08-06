// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_AML_USB_PHY_V2_AML_USB_PHY_H_
#define SRC_DEVICES_USB_DRIVERS_AML_USB_PHY_V2_AML_USB_PHY_H_

#include <fuchsia/hardware/registers/llcpp/fidl.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>
#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/usb/phy.h>
#include <fbl/mutex.h>

#include "dwc2-device.h"
#include "xhci-device.h"

namespace aml_usb_phy {

class AmlUsbPhy;
using AmlUsbPhyType = ddk::Device<AmlUsbPhy, ddk::UnbindableDeprecated, ddk::Messageable>;

// This is the main class for the platform bus driver.
class AmlUsbPhy : public AmlUsbPhyType,
                  public ddk::UsbPhyProtocol<AmlUsbPhy, ddk::base_protocol>,
                  public llcpp::fuchsia::hardware::registers::Device::Interface {
 public:
  explicit AmlUsbPhy(zx_device_t* parent) : AmlUsbPhyType(parent), pdev_(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // USB PHY protocol implementation.
  void UsbPhyConnectStatusChanged(bool connected);

  // Device protocol implementation.
  void DdkUnbindDeprecated();
  void DdkRelease();
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

  void WriteRegister(uint64_t address, uint32_t value,
                     WriteRegisterCompleter::Sync completer) override;

 private:
  enum class UsbMode {
    UNKNOWN,
    HOST,
    PERIPHERAL,
  };

  DISALLOW_COPY_ASSIGN_AND_MOVE(AmlUsbPhy);

  void InitPll(ddk::MmioBuffer* mmio);
  zx_status_t InitPhy();
  zx_status_t InitOtg();
  void SetMode(UsbMode mode) __TA_REQUIRES(lock_);

  zx_status_t AddXhciDevice();
  void RemoveXhciDevice();
  zx_status_t AddDwc2Device();
  void RemoveDwc2Device();

  zx_status_t Init();
  int IrqThread();

  ddk::PDev pdev_;
  std::optional<ddk::MmioBuffer> reset_mmio_;
  std::optional<ddk::MmioBuffer> usbctrl_mmio_;
  std::optional<ddk::MmioBuffer> usbphy20_mmio_;
  std::optional<ddk::MmioBuffer> usbphy21_mmio_;
  std::optional<ddk::MmioBuffer> factory_mmio_;

  zx::interrupt irq_;
  thrd_t irq_thread_;

  fbl::Mutex lock_;

  // Magic numbers for PLL from metadata
  uint32_t pll_settings_[8];

  // Device node for binding XHCI driver.
  std::unique_ptr<XhciDevice> xhci_device_;
  std::unique_ptr<Dwc2Device> dwc2_device_;

  UsbMode mode_ = UsbMode::UNKNOWN;
  bool dwc2_connected_ = false;
};

}  // namespace aml_usb_phy

#endif  // SRC_DEVICES_USB_DRIVERS_AML_USB_PHY_V2_AML_USB_PHY_H_
