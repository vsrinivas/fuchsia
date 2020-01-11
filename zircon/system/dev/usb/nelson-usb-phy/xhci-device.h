// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/usb/phy.h>

namespace nelson_usb_phy {

class XhciDevice;
using XhciDeviceType = ddk::Device<XhciDevice>;

// Device for binding the XHCI driver.
class XhciDevice : public XhciDeviceType,
                   public ddk::UsbPhyProtocol<XhciDevice, ddk::base_protocol> {
 public:
  explicit XhciDevice(zx_device_t* parent) : XhciDeviceType(parent), phy_(parent) {}
  // Device protocol implementation.
  void DdkRelease() { delete this; }
  void UsbPhyConnectStatusChanged(bool connected) {}

 private:
  ddk::UsbPhyProtocolClient phy_;
  DISALLOW_COPY_ASSIGN_AND_MOVE(XhciDevice);
};

}  // namespace nelson_usb_phy
