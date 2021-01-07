// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_NELSON_USB_PHY_NELSON_USB_PHY_H_
#define SRC_DEVICES_USB_DRIVERS_NELSON_USB_PHY_NELSON_USB_PHY_H_

#include <fuchsia/hardware/usb/phy/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/sync/completion.h>
#include <lib/zx/interrupt.h>
#include <threads.h>

#include <ddktl/device.h>
#include <fbl/mutex.h>

#include "dwc2-device.h"
#include "xhci-device.h"

namespace nelson_usb_phy {

class NelsonUsbPhy;
using NelsonUsbPhyType = ddk::Device<NelsonUsbPhy, ddk::Unbindable, ddk::ChildPreReleaseable>;

// This is the main class for the platform bus driver.
class NelsonUsbPhy : public NelsonUsbPhyType,
                     public ddk::UsbPhyProtocol<NelsonUsbPhy, ddk::base_protocol> {
 public:
  explicit NelsonUsbPhy(zx_device_t* parent) : NelsonUsbPhyType(parent), pdev_(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // USB PHY protocol implementation.
  void UsbPhyConnectStatusChanged(bool connected);

  zx_status_t UsbPhyNotifyDeviceRemoved();

  // Device protocol implementation.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  void DdkChildPreRelease(void* child_ctx);

 private:
  enum class UsbMode {
    UNKNOWN,
    HOST,
    PERIPHERAL,
  };

  DISALLOW_COPY_ASSIGN_AND_MOVE(NelsonUsbPhy);

  void InitPll(ddk::MmioBuffer* mmio);
  zx_status_t InitPhy();
  zx_status_t InitOtg();
  void SetMode(UsbMode mode) __TA_REQUIRES(lock_);

  zx_status_t AddXhciDevice();
  void RemoveXhciDevice(bool wait);
  zx_status_t AddDwc2Device();
  void RemoveDwc2Device(bool wait);

  zx_status_t Init();
  int IrqThread();

  ddk::PDev pdev_;
  std::optional<ddk::MmioBuffer> reset_mmio_;
  std::optional<ddk::MmioBuffer> usbctrl_mmio_;
  std::optional<ddk::MmioBuffer> usbphy20_mmio_;
  std::optional<ddk::MmioBuffer> usbphy21_mmio_;
  std::optional<ddk::MmioBuffer> power_mmio_;
  std::optional<ddk::MmioBuffer> sleep_mmio_;

  zx::interrupt irq_;
  thrd_t irq_thread_;

  // Lock used to synchronize access to this driver between the interrupt
  // thread and a child device calling SetMode through Banjo on a separate thread.
  fbl::Mutex lock_;

  // Magic numbers for PLL from metadata
  uint32_t pll_settings_[8];

  // Device node for binding XHCI driver.
  std::unique_ptr<XhciDevice> xhci_device_;
  std::unique_ptr<Dwc2Device> dwc2_device_;
  sync_completion_t remove_event_;

  UsbMode mode_ = UsbMode::UNKNOWN;
  bool dwc2_connected_ = false;
};

}  // namespace nelson_usb_phy

#endif  // SRC_DEVICES_USB_DRIVERS_NELSON_USB_PHY_NELSON_USB_PHY_H_
