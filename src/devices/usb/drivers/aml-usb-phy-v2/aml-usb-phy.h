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
#include <ddktl/protocol/registers.h>
#include <ddktl/protocol/usb/modeswitch.h>
#include <ddktl/protocol/usb/phy.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <soc/aml-common/aml-registers.h>

#include "dwc2-device.h"
#include "xhci-device.h"

namespace aml_usb_phy {

class AmlUsbPhy;
using AmlUsbPhyType =
    ddk::Device<AmlUsbPhy, ddk::Initializable, ddk::Unbindable, ddk::ChildPreReleaseable>;

// This is the main class for the platform bus driver.
class AmlUsbPhy : public AmlUsbPhyType, public ddk::UsbPhyProtocol<AmlUsbPhy, ddk::base_protocol> {
 public:
  // Public for testing.
  enum class UsbMode {
    UNKNOWN,
    HOST,
    PERIPHERAL,
  };

  explicit AmlUsbPhy(zx_device_t* parent) : AmlUsbPhyType(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // USB PHY protocol implementation.
  void UsbPhyConnectStatusChanged(bool connected);

  // Device protocol implementation.
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkChildPreRelease(void* child_ctx);
  void DdkRelease();

  // Public for testing.
  UsbMode mode() {
    fbl::AutoLock lock(&lock_);
    return phy_mode_;
  }

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(AmlUsbPhy);

  void InitPll(ddk::MmioBuffer* mmio);
  zx_status_t InitPhy();
  zx_status_t InitOtg();

  // Called when |SetMode| completes.
  using SetModeCompletion = fit::callback<void(void)>;
  void SetMode(UsbMode mode, SetModeCompletion completion) __TA_REQUIRES(lock_);

  zx_status_t AddXhciDevice() __TA_REQUIRES(lock_);
  void RemoveXhciDevice(SetModeCompletion completion) __TA_REQUIRES(lock_);
  zx_status_t AddDwc2Device() __TA_REQUIRES(lock_);
  void RemoveDwc2Device(SetModeCompletion completion) __TA_REQUIRES(lock_);

  zx_status_t Init();
  int IrqThread();

  ddk::PDev pdev_;
  ::llcpp::fuchsia::hardware::registers::Device::SyncClient reset_register_;
  std::optional<ddk::MmioBuffer> usbctrl_mmio_;
  std::optional<ddk::MmioBuffer> usbphy20_mmio_;
  std::optional<ddk::MmioBuffer> usbphy21_mmio_;

  zx::interrupt irq_;
  thrd_t irq_thread_;
  std::atomic_bool irq_thread_started_ = false;

  fbl::Mutex lock_;

  // Magic numbers for PLL from metadata
  uint32_t pll_settings_[8];

  // Device node for binding XHCI driver.
  std::unique_ptr<XhciDevice> xhci_device_ __TA_GUARDED(lock_);
  std::unique_ptr<Dwc2Device> dwc2_device_ __TA_GUARDED(lock_);

  UsbMode phy_mode_ __TA_GUARDED(lock_) = UsbMode::UNKNOWN;  // Physical USB mode.
  usb_mode_t dr_mode_ = USB_MODE_OTG;  // USB Controller Mode. Internal to Driver.
  bool dwc2_connected_ = false;

  // If set, indicates that the device has a pending SetMode which
  // will be completed once |DdkChildPreRelease| is called.
  SetModeCompletion set_mode_completion_ __TA_GUARDED(lock_);
};

}  // namespace aml_usb_phy

#endif  // SRC_DEVICES_USB_DRIVERS_AML_USB_PHY_V2_AML_USB_PHY_H_
