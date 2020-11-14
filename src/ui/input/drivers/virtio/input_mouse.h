// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_UI_INPUT_DRIVERS_VIRTIO_INPUT_MOUSE_H_
#define SRC_UI_INPUT_DRIVERS_VIRTIO_INPUT_MOUSE_H_

#include <hid/virtio-mouse.h>

#include "src/ui/input/drivers/virtio/input_device.h"

namespace virtio {

class HidMouse : public HidDevice {
 public:
  zx_status_t GetDescriptor(uint8_t desc_type, void* out_data_buffer, size_t data_size,
                            size_t* out_data_actual) override;
  void ReceiveEvent(virtio_input_event_t* event) override;
  const uint8_t* GetReport(size_t* size) override;

 private:
  void ReceiveRelEvent(virtio_input_event_t* event);
  void ReceiveKeyEvent(virtio_input_event_t* event);

  hid_scroll_mouse_report report_ = {};
};

}  // namespace virtio

#endif  // SRC_UI_INPUT_DRIVERS_VIRTIO_INPUT_MOUSE_H_
