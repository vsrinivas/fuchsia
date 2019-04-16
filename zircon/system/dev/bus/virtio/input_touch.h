// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "input_device.h"

#include <hid/paradise.h>


namespace virtio {

// The HidTouch class translates virtio touchscreen events into HID touchscreen
// events. It does this by making the virtio touchscreen appear exactly like
// a paradise touchscreen. There is no good reason to use the paradise
// touchscreen, other than it is a valid, tested report descriptor and it
// was easier to reuse it than building a new report descriptor from scratch.
class HidTouch : public HidDevice {
public:
    HidTouch(virtio_input_absinfo_t x_info, virtio_input_absinfo_t y_info)
        : x_info_(x_info), y_info_(y_info) {
        report_.rpt_id = PARADISE_RPT_ID_TOUCH;
    }

    zx_status_t GetDescriptor(uint8_t desc_type, void** data, size_t* len);
    void ReceiveEvent(virtio_input_event_t* event);
    const uint8_t* GetReport(size_t* size);

private:
    static constexpr int MAX_TOUCH_POINTS = 5;
    virtio_input_absinfo_t x_info_;
    virtio_input_absinfo_t y_info_;
    int mt_slot_ = -1;
    paradise_touch_t report_ = {};
};

} // namespace virtio
