// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_UI_INPUT_DRIVERS_VIRTIO_INPUT_KBD_H_
#define SRC_UI_INPUT_DRIVERS_VIRTIO_INPUT_KBD_H_

#include "input_device.h"

namespace virtio {

class HidKeyboard : public HidDevice {
 public:
  zx_status_t GetDescriptor(uint8_t desc_type, void* out_data_buffer, size_t data_size,
                            size_t* out_data_actual) override;
  void ReceiveEvent(virtio_input_event_t* event) override;
  const uint8_t* GetReport(size_t* size) override;

 private:
  void AddKeypressToReport(uint16_t event_code);
  void RemoveKeypressFromReport(uint16_t event_code);

  hid_boot_kbd_report_t report_ = {};
};

}  // namespace virtio

#endif  // SRC_UI_INPUT_DRIVERS_VIRTIO_INPUT_KBD_H_
