// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_USB_VIRTUAL_BUS_USB_VIRTUAL_DEVICE_H_
#define SRC_DEVICES_USB_DRIVERS_USB_VIRTUAL_BUS_USB_VIRTUAL_DEVICE_H_

#include <fuchsia/hardware/usb/dci/cpp/banjo.h>
#include <lib/ddk/device.h>

#include <ddktl/device.h>
#include <fbl/macros.h>

namespace usb_virtual_bus {

class UsbVirtualBus;
class UsbVirtualDevice;
using UsbVirtualDeviceType = ddk::Device<UsbVirtualDevice>;

// This class implements the virtual USB device controller protocol.
class UsbVirtualDevice : public UsbVirtualDeviceType,
                         public ddk::UsbDciProtocol<UsbVirtualDevice, ddk::base_protocol> {
 public:
  explicit UsbVirtualDevice(zx_device_t* parent, UsbVirtualBus* bus)
      : UsbVirtualDeviceType(parent), bus_(bus) {}

  // Device protocol implementation.
  void DdkRelease();

  // USB device controller protocol implementation.
  void UsbDciRequestQueue(usb_request_t* usb_request, const usb_request_complete_t* complete_cb);
  zx_status_t UsbDciSetInterface(const usb_dci_interface_protocol_t* interface);
  zx_status_t UsbDciConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                             const usb_ss_ep_comp_descriptor_t* ss_comp_desc);
  zx_status_t UsbDciDisableEp(uint8_t ep_address);
  zx_status_t UsbDciEpSetStall(uint8_t ep_address);
  zx_status_t UsbDciEpClearStall(uint8_t ep_address);
  zx_status_t UsbDciCancelAll(uint8_t endpoint);
  size_t UsbDciGetRequestSize();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(UsbVirtualDevice);

  UsbVirtualBus* bus_;
};

}  // namespace usb_virtual_bus

#endif  // SRC_DEVICES_USB_DRIVERS_USB_VIRTUAL_BUS_USB_VIRTUAL_DEVICE_H_
