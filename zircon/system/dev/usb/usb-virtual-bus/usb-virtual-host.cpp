// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-virtual-host.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/usb/virtualbus/c/fidl.h>
#include <usb/usb-request.h>

#include "usb-virtual-bus.h"

namespace usb_virtual_bus {

void UsbVirtualHost::DdkRelease() {
    delete this;
}

void UsbVirtualHost::UsbHciRequestQueue(usb_request_t* req,
                                       const usb_request_complete_t* complete_cb) {
    return bus_->UsbHciRequestQueue(req, complete_cb);
}

void UsbVirtualHost::UsbHciSetBusInterface(const usb_bus_interface_t* bus_intf) {
    return bus_->UsbHciSetBusInterface(bus_intf);
}

size_t UsbVirtualHost::UsbHciGetMaxDeviceCount() {
    return bus_->UsbHciGetMaxDeviceCount();
}

zx_status_t UsbVirtualHost::UsbHciEnableEndpoint(uint32_t device_id,
                                                const usb_endpoint_descriptor_t* ep_desc,
                                                const usb_ss_ep_comp_descriptor_t* ss_com_desc,
                                                bool enable) {
    return bus_->UsbHciEnableEndpoint(device_id, ep_desc, ss_com_desc, enable);
}

uint64_t UsbVirtualHost::UsbHciGetCurrentFrame() {
    return bus_->UsbHciGetCurrentFrame();
}

zx_status_t UsbVirtualHost::UsbHciConfigureHub(uint32_t device_id, usb_speed_t speed,
                                              const usb_hub_descriptor_t* desc) {
    return bus_->UsbHciConfigureHub(device_id, speed, desc);
}

zx_status_t UsbVirtualHost::UsbHciHubDeviceAdded(uint32_t device_id, uint32_t port,
                                                usb_speed_t speed) {
    return bus_->UsbHciHubDeviceAdded(device_id, port, speed);
}

zx_status_t UsbVirtualHost::UsbHciHubDeviceRemoved(uint32_t device_id, uint32_t port) {
    return bus_->UsbHciHubDeviceRemoved(device_id, port);
}

zx_status_t UsbVirtualHost::UsbHciHubDeviceReset(uint32_t device_id, uint32_t port) {
    return bus_->UsbHciHubDeviceReset(device_id, port);
}

zx_status_t UsbVirtualHost::UsbHciResetEndpoint(uint32_t device_id, uint8_t ep_address) {
    return bus_->UsbHciResetEndpoint(device_id, ep_address);
}

zx_status_t UsbVirtualHost::UsbHciResetDevice(uint32_t hub_address, uint32_t device_id) {
    return bus_->UsbHciResetDevice(hub_address, device_id);
}

size_t UsbVirtualHost::UsbHciGetMaxTransferSize(uint32_t device_id, uint8_t ep_address) {
    return bus_->UsbHciGetMaxTransferSize(device_id, ep_address);
}

zx_status_t UsbVirtualHost::UsbHciCancelAll(uint32_t device_id, uint8_t ep_address) {
    return bus_->UsbHciCancelAll(device_id, ep_address);
}

size_t UsbVirtualHost::UsbHciGetRequestSize() {
    return bus_->UsbHciGetRequestSize();
}

} // namespace usb_virtual_bus
