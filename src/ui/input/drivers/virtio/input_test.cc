// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <ddk/protocol/hidbus.h>
#include <hid/boot.h>
#include <virtio/input.h>
#include <zxtest/zxtest.h>

#include "input_touch.h"

namespace virtio {

namespace {

void SendTouchEvent(HidTouch& touch, uint16_t type, uint16_t code, uint32_t value) {
  virtio_input_event_t event = {};
  event.type = type;
  event.code = code;
  event.value = value;

  touch.ReceiveEvent(&event);
}

}  // namespace

class VirtioInputTest : public zxtest::Test {};

TEST_F(VirtioInputTest, MultiTouchReportDescriptor) {
  virtio_input_absinfo_t x_info = {};
  virtio_input_absinfo_t y_info = {};
  HidTouch touch(x_info, y_info);

  // Assert that the report descriptor is correct.
  // In this case correct means a copy of the paradise touch report descriptor.
  uint8_t desc[HID_MAX_DESC_LEN];
  size_t desc_len;
  zx_status_t status =
      touch.GetDescriptor(HID_DESCRIPTION_TYPE_REPORT, desc, sizeof(desc), &desc_len);
  ASSERT_OK(status);

  size_t paradise_size;
  const uint8_t* paradise_desc = get_paradise_touch_report_desc(&paradise_size);
  ASSERT_EQ(paradise_size, desc_len);
  ASSERT_EQ(0, memcmp(paradise_desc, desc, desc_len));
}

TEST_F(VirtioInputTest, MultiTouchFingerEvents) {
  int VAL_MAX = 100;
  int X_VAL = 50;
  int Y_VAL = 100;
  virtio_input_absinfo_t x_info = {};
  x_info.min = 0;
  x_info.max = static_cast<uint16_t>(VAL_MAX);
  virtio_input_absinfo_t y_info = {};
  y_info.min = 0;
  y_info.max = static_cast<uint16_t>(VAL_MAX);
  HidTouch touch(x_info, y_info);

  // Assert that a single finger works.
  SendTouchEvent(touch, VIRTIO_INPUT_EV_ABS, VIRTIO_INPUT_EV_MT_SLOT, 0);
  SendTouchEvent(touch, VIRTIO_INPUT_EV_ABS, VIRTIO_INPUT_EV_MT_TRACKING_ID, 1);
  SendTouchEvent(touch, VIRTIO_INPUT_EV_ABS, VIRTIO_INPUT_EV_MT_POSITION_X,
                 static_cast<uint16_t>(X_VAL));
  SendTouchEvent(touch, VIRTIO_INPUT_EV_ABS, VIRTIO_INPUT_EV_MT_POSITION_Y,
                 static_cast<uint16_t>(Y_VAL));

  size_t paradise_size;
  const void* report = touch.GetReport(&paradise_size);
  const paradise_touch_t* paradise_touch = reinterpret_cast<const paradise_touch_t*>(report);

  ASSERT_EQ(sizeof(paradise_touch_t), paradise_size);
  ASSERT_EQ(1, paradise_touch->contact_count);
  ASSERT_EQ(PARADISE_FINGER_FLAGS_TSWITCH_MASK,
            paradise_touch->fingers[0].flags & PARADISE_FINGER_FLAGS_TSWITCH_MASK);
  ASSERT_EQ(X_VAL * PARADISE_X_MAX / VAL_MAX, paradise_touch->fingers[0].x);
  ASSERT_EQ(Y_VAL * PARADISE_Y_MAX / VAL_MAX, paradise_touch->fingers[0].y);

  ASSERT_EQ(0, paradise_touch->fingers[1].flags);
  ASSERT_EQ(0, paradise_touch->fingers[2].flags);
  ASSERT_EQ(0, paradise_touch->fingers[3].flags);
  ASSERT_EQ(0, paradise_touch->fingers[4].flags);

  // Assert that a second finger tracks.
  SendTouchEvent(touch, VIRTIO_INPUT_EV_ABS, VIRTIO_INPUT_EV_MT_SLOT, 1);
  SendTouchEvent(touch, VIRTIO_INPUT_EV_ABS, VIRTIO_INPUT_EV_MT_TRACKING_ID, 2);

  report = touch.GetReport(&paradise_size);
  paradise_touch = reinterpret_cast<const paradise_touch_t*>(report);

  ASSERT_EQ(sizeof(paradise_touch_t), paradise_size);
  ASSERT_EQ(2, paradise_touch->contact_count);

  ASSERT_EQ(PARADISE_FINGER_FLAGS_TSWITCH_MASK,
            paradise_touch->fingers[0].flags & PARADISE_FINGER_FLAGS_TSWITCH_MASK);
  ASSERT_EQ(PARADISE_FINGER_FLAGS_TSWITCH_MASK,
            paradise_touch->fingers[1].flags & PARADISE_FINGER_FLAGS_TSWITCH_MASK);
  ASSERT_EQ(0, paradise_touch->fingers[2].flags);
  ASSERT_EQ(0, paradise_touch->fingers[3].flags);
  ASSERT_EQ(0, paradise_touch->fingers[4].flags);

  // Pick up the second finger.

  // We don't send another SLOT event because we will rely on the slot already
  // being 1.
  SendTouchEvent(touch, VIRTIO_INPUT_EV_ABS, VIRTIO_INPUT_EV_MT_TRACKING_ID, -1);

  report = touch.GetReport(&paradise_size);
  paradise_touch = reinterpret_cast<const paradise_touch_t*>(report);

  ASSERT_EQ(sizeof(paradise_touch_t), paradise_size);
  ASSERT_EQ(1, paradise_touch->contact_count);

  ASSERT_EQ(PARADISE_FINGER_FLAGS_TSWITCH_MASK,
            paradise_touch->fingers[0].flags & PARADISE_FINGER_FLAGS_TSWITCH_MASK);
  ASSERT_EQ(0, paradise_touch->fingers[1].flags);
  ASSERT_EQ(0, paradise_touch->fingers[2].flags);
  ASSERT_EQ(0, paradise_touch->fingers[3].flags);
  ASSERT_EQ(0, paradise_touch->fingers[4].flags);
}

}  // namespace virtio
