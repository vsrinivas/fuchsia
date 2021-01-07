// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_USB_SPEW_H_
#define SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_USB_SPEW_H_

#include <fuchsia/hardware/usb/request/c/banjo.h>
#include <zircon/hw/usb.h>

namespace mt_usb_hci {

// These are debugging routines that just log a formatted version of the given type to SPEW.
void SpewUsbDeviceDescriptor(const usb_device_descriptor_t& d);
void SpewUsbEndpointDescriptor(const usb_endpoint_descriptor_t& d);
void SpewUsbRequest(const usb_request_t& req);

}  // namespace mt_usb_hci

#endif  // SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_USB_SPEW_H_
