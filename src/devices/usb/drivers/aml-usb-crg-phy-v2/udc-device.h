// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_AML_USB_CRG_PHY_V2_UDC_DEVICE_H_
#define SRC_DEVICES_USB_DRIVERS_AML_USB_CRG_PHY_V2_UDC_DEVICE_H_

#include <fuchsia/hardware/usb/phy/cpp/banjo.h>

#include <ddktl/device.h>

namespace aml_usb_crg_phy {

class UdcDevice;
using UdcDeviceType = ddk::Device<UdcDevice>;

// Device for binding the UDC driver.
class UdcDevice : public UdcDeviceType, public ddk::UsbPhyProtocol<UdcDevice, ddk::base_protocol> {
 public:
  explicit UdcDevice(zx_device_t* parent) : UdcDeviceType(parent), parent_protocol_(parent) {}

  // USB PHY protocol implementation.
  void UsbPhyConnectStatusChanged(bool connected) {
    parent_protocol_.ConnectStatusChanged(connected);
  }

  // Device protocol implementation.
  void DdkRelease() { delete this; }

 private:
  UdcDevice(const UdcDevice&) = delete;
  UdcDevice& operator=(const UdcDevice&) = delete;
  UdcDevice(UdcDevice&&) = delete;
  UdcDevice& operator=(UdcDevice&&) = delete;

  ddk::UsbPhyProtocolClient parent_protocol_;
};

}  // namespace aml_usb_crg_phy

#endif  // SRC_DEVICES_USB_DRIVERS_AML_USB_CRG_PHY_V2_UDC_DEVICE_H_
