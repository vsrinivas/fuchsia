// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "input_touch.h"

#include <ddk/debug.h>
#include <ddk/protocol/hidbus.h>
#include <fbl/algorithm.h>

#include "trace.h"

#define LOCAL_TRACE 0

namespace virtio {

zx_status_t HidTouch::GetDescriptor(uint8_t desc_type, void* out_data_buffer, size_t data_size,
                                    size_t* out_data_actual) {
  if (out_data_buffer == nullptr || out_data_actual == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (desc_type != HID_DESCRIPTION_TYPE_REPORT) {
    return ZX_ERR_NOT_FOUND;
  }

  size_t buflen = 0;
  const uint8_t* buf = get_paradise_touch_report_desc(&buflen);

  if (data_size < buflen) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  memcpy(out_data_buffer, buf, buflen);
  *out_data_actual = buflen;
  return ZX_OK;
}

void HidTouch::ReceiveEvent(virtio_input_event_t* event) {
  if (event->type == VIRTIO_INPUT_EV_ABS) {
    if (event->code == VIRTIO_INPUT_EV_MT_SLOT) {
      if (event->value > MAX_TOUCH_POINTS) {
        LTRACEF("ERROR: Slot is too large for touchscreen\n");
        mt_slot_ = -1;
        return;
      }
      mt_slot_ = event->value;
    }

    if (mt_slot_ < 0 || mt_slot_ > MAX_TOUCH_POINTS) {
      return;
    }

    if (event->code == VIRTIO_INPUT_EV_MT_TRACKING_ID) {
      paradise_finger_t& finger = report_.fingers[mt_slot_];
      // If tracking id is -1 we have to remove the finger from being tracked.
      if (static_cast<int>(event->value) == -1) {
        if (finger.flags & PARADISE_FINGER_FLAGS_TSWITCH_MASK) {
          finger.flags &= static_cast<uint8_t>(~PARADISE_FINGER_FLAGS_TSWITCH_MASK);
          report_.contact_count--;
        }
      } else {
        if (!(finger.flags & PARADISE_FINGER_FLAGS_TSWITCH_MASK)) {
          finger.flags |= static_cast<uint8_t>(PARADISE_FINGER_FLAGS_TSWITCH_MASK);
          report_.contact_count++;
        }
      }
      finger.finger_id = static_cast<uint16_t>(event->value);
    } else if (event->code == VIRTIO_INPUT_EV_MT_POSITION_X) {
      report_.fingers[mt_slot_].x =
          static_cast<uint16_t>(event->value * PARADISE_X_MAX / x_info_.max);
    } else if (event->code == VIRTIO_INPUT_EV_MT_POSITION_Y) {
      report_.fingers[mt_slot_].y =
          static_cast<uint16_t>(event->value * PARADISE_Y_MAX / y_info_.max);
    }
  }
}

const uint8_t* HidTouch::GetReport(size_t* size) {
  *size = sizeof(report_);
  return reinterpret_cast<const uint8_t*>(&report_);
}

}  // namespace virtio
