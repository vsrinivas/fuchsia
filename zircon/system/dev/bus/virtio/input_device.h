// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <virtio/input.h>
#include <zircon/types.h>

namespace virtio {

// Each HidDevice is responsible for taking virtio events and translating them
// into HID events. This class should be inherited once for each type of input
// device that should be supported (e.g: mice, keyboards, touchscreens).
class HidDevice {
public:
    virtual ~HidDevice() = default;

    // Gets the HID Report Descriptor for this device. The memory for the descriptor
    // is dynamically allocated and placed in |data| with length |len|.
    virtual zx_status_t GetDescriptor(uint8_t desc_type, void** data, size_t* len) = 0;

    // Process a virtio event for this device and update the private HID
    // report accordingly.
    virtual void ReceiveEvent(virtio_input_event_t* event) = 0;

    // Return a constant pointer to the private HID report that represents
    // this device.
    virtual const uint8_t* GetReport(size_t* size) = 0;
};

} // namespace virtio
