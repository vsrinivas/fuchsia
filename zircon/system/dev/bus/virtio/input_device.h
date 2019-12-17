// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_DEV_BUS_VIRTIO_INPUT_DEVICE_H_
#define ZIRCON_SYSTEM_DEV_BUS_VIRTIO_INPUT_DEVICE_H_

#include <zircon/types.h>

#include <virtio/input.h>

namespace virtio {

// Each HidDevice is responsible for taking virtio events and translating them
// into HID events. This class should be inherited once for each type of input
// device that should be supported (e.g: mice, keyboards, touchscreens).
class HidDevice {
 public:
  virtual ~HidDevice() = default;

  // Gets the HID Report Descriptor for this device. The memory for the descriptor
  // is dynamically allocated and placed in |data| with length |len|.
  virtual zx_status_t GetDescriptor(uint8_t desc_type, void* out_data_buffer, size_t data_size,
                                    size_t* out_data_actual) = 0;

  // Process a virtio event for this device and update the private HID
  // report accordingly.
  virtual void ReceiveEvent(virtio_input_event_t* event) = 0;

  // Return a constant pointer to the private HID report that represents
  // this device.
  virtual const uint8_t* GetReport(size_t* size) = 0;
};

}  // namespace virtio

#endif  // ZIRCON_SYSTEM_DEV_BUS_VIRTIO_INPUT_DEVICE_H_
