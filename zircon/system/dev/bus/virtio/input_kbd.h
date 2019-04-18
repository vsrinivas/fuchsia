// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "input_device.h"

namespace virtio {

class HidKeyboard : public HidDevice {
public:

    zx_status_t GetDescriptor(uint8_t desc_type, void** data, size_t* len);
    void ReceiveEvent(virtio_input_event_t* event);
    const uint8_t* GetReport(size_t* size);

private:
    void AddKeypressToReport(uint16_t event_code);
    void RemoveKeypressFromReport(uint16_t event_code);

    hid_boot_kbd_report_t report_ = {};
};

} // namespace virtio
