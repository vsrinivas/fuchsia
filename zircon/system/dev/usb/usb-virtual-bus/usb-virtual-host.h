// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/usb/hci.h>

namespace usb_virtual_bus {

class UsbVirtualBus;
class UsbVirtualHost;
using UsbVirtualHostType = ddk::Device<UsbVirtualHost>;

// This class implements the virtual USB host controller protocol.
class UsbVirtualHost : public UsbVirtualHostType,
                       public ddk::UsbHciProtocol<UsbVirtualHost, ddk::base_protocol> {
 public:
  explicit UsbVirtualHost(zx_device_t* parent, UsbVirtualBus* bus)
      : UsbVirtualHostType(parent), bus_(bus) {}

  // Device protocol implementation.
  void DdkRelease();

  // USB host controller protocol implementation.
  void UsbHciRequestQueue(usb_request_t* usb_request, const usb_request_complete_t* complete_cb);
  void UsbHciSetBusInterface(const usb_bus_interface_protocol_t* bus_intf);
  size_t UsbHciGetMaxDeviceCount();
  zx_status_t UsbHciEnableEndpoint(uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                                   const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable);
  uint64_t UsbHciGetCurrentFrame();
  zx_status_t UsbHciConfigureHub(uint32_t device_id, usb_speed_t speed,
                                 const usb_hub_descriptor_t* desc, bool multi_tt);
  zx_status_t UsbHciHubDeviceAdded(uint32_t device_id, uint32_t port, usb_speed_t speed);
  zx_status_t UsbHciHubDeviceRemoved(uint32_t device_id, uint32_t port);
  zx_status_t UsbHciHubDeviceReset(uint32_t device_id, uint32_t port);
  zx_status_t UsbHciResetEndpoint(uint32_t device_id, uint8_t ep_address);
  zx_status_t UsbHciResetDevice(uint32_t hub_address, uint32_t device_id);
  size_t UsbHciGetMaxTransferSize(uint32_t device_id, uint8_t ep_address);
  zx_status_t UsbHciCancelAll(uint32_t device_id, uint8_t ep_address);
  size_t UsbHciGetRequestSize();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(UsbVirtualHost);

  UsbVirtualBus* bus_;
};

}  // namespace usb_virtual_bus
