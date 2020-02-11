// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xhci-context.h"

// Helper functions to assist with setup and teardown of USB devices.

namespace usb_xhci {

class UsbXhci;

// Enumerates a device as specified in xHCI section 4.3 starting from step 4
// This method should be called once the physical port of a device has been
// initialized.
TRBPromise EnumerateDevice(UsbXhci* hci, uint8_t port, std::optional<HubInfo> hub_info);

// Retrieves the bMaxPacketSize0 field in the USB device descriptor
fit::promise<uint8_t, zx_status_t> GetMaxPacketSize(UsbXhci* hci, uint8_t slot_id);
}  // namespace usb_xhci
