// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_AS370_USB_PHY_AS370_USB_PHY_H_
#define SRC_DEVICES_USB_DRIVERS_AS370_USB_PHY_AS370_USB_PHY_H_

#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>

#include <ddktl/device.h>

#include "dwc2-device.h"

namespace as370_usb_phy {

class UsbPhy;
using UsbPhyType = ddk::Device<UsbPhy, ddk::UnbindableDeprecated>;

// This is the main class for the platform bus driver.
class UsbPhy : public UsbPhyType {
 public:
  explicit UsbPhy(zx_device_t* parent) : UsbPhyType(parent), pdev_(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkUnbindDeprecated();
  void DdkRelease();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(UsbPhy);

  void ResetPhy();
  zx_status_t InitPhy();

  zx_status_t AddDwc2Device();
  zx_status_t RemoveDwc2Device();

  zx_status_t Init();

  ddk::PDev pdev_;
  std::optional<ddk::MmioBuffer> usbphy_mmio_;
  std::optional<ddk::MmioBuffer> reset_mmio_;

  // Device node for binding DWC2 driver.
  std::unique_ptr<Dwc2Device> dwc2_device_;
  uint32_t did_;
};

}  // namespace as370_usb_phy

#endif  // SRC_DEVICES_USB_DRIVERS_AS370_USB_PHY_AS370_USB_PHY_H_
