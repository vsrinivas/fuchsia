// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_AS370_USB_PHY_DWC2_DEVICE_H_
#define SRC_DEVICES_USB_DRIVERS_AS370_USB_PHY_DWC2_DEVICE_H_

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace as370_usb_phy {

class Dwc2Device;
using Dwc2DeviceType = ddk::Device<Dwc2Device>;

// Device for binding the DWC2 driver.
class Dwc2Device : public Dwc2DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_USB_PHY> {
 public:
  explicit Dwc2Device(zx_device_t* parent) : Dwc2DeviceType(parent) {}

  // Device protocol implementation.
  void DdkRelease() { delete this; }

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Dwc2Device);
};

}  // namespace as370_usb_phy

#endif  // SRC_DEVICES_USB_DRIVERS_AS370_USB_PHY_DWC2_DEVICE_H_
