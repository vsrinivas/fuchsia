// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/touchpad.h"

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>
#include <stdint.h>
#include <stdio.h>

#include <vector>

#include "src/lib/fxl/logging.h"

namespace ui_input {

bool Touchpad::ParseReportDescriptor(
    const hid::ReportDescriptor& report_descriptor,
    Device::Descriptor* device_descriptor) {
  if (!touch_.ParseTouchDescriptor(report_descriptor)) {
    return false;
  }

  device_descriptor->protocol = Protocol::Touchpad;
  device_descriptor->has_mouse = true;
  device_descriptor->mouse_type = MouseDeviceType::TOUCH;
  device_descriptor->mouse_descriptor =
      fuchsia::ui::input::MouseDescriptor::New();

  // At the moment all mice send relative units, so these min and max values
  // do not affect anything. Set them to maximum range.
  device_descriptor->mouse_descriptor->rel_x.range.min = INT32_MIN;
  device_descriptor->mouse_descriptor->rel_x.range.max = INT32_MAX;
  device_descriptor->mouse_descriptor->rel_x.resolution = 1;

  device_descriptor->mouse_descriptor->rel_y.range.min = INT32_MIN;
  device_descriptor->mouse_descriptor->rel_y.range.max = INT32_MAX;
  device_descriptor->mouse_descriptor->rel_y.resolution = 1;

  device_descriptor->mouse_descriptor->buttons |=
      fuchsia::ui::input::kMouseButtonPrimary;

  return true;
}

bool Touchpad::ParseTouchpadReport(const Touch::Report& touchpad,
                                   fuchsia::ui::input::InputReport* report) {
  report->mouse->rel_x = 0;
  report->mouse->rel_y = 0;
  report->mouse->pressed_buttons = 0;

  // If all fingers are lifted reset our tracking finger.
  if (touchpad.contact_count == 0) {
    has_touch_ = false;
    tracking_finger_was_lifted_ = true;
    return true;
  }

  // If we don't have a tracking finger then set one.
  if (!has_touch_) {
    has_touch_ = true;
    tracking_finger_was_lifted_ = false;
    tracking_finger_id_ = touchpad.contacts[0].id;

    mouse_abs_x_ = touchpad.contacts[0].x;
    mouse_abs_y_ = touchpad.contacts[0].y;
    return true;
  }

  // Find the finger we are tracking.
  const Touch::ContactReport* contact = nullptr;
  for (size_t i = 0; i < touchpad.contact_count; i++) {
    if (touchpad.contacts[i].id == tracking_finger_id_) {
      contact = &touchpad.contacts[i];
      break;
    }
  }

  // If our tracking finger isn't pressed return early.
  if (contact == nullptr) {
    tracking_finger_was_lifted_ = true;
    return true;
  }

  // If our tracking finger was lifted then reset the abs values otherwise
  // the pointer will jump rapidly.
  if (tracking_finger_was_lifted_) {
    tracking_finger_was_lifted_ = false;
    mouse_abs_x_ = contact->x;
    mouse_abs_y_ = contact->y;
  }

  // The touch driver returns in units of 10^-5m, but the resolution expected
  // by |report| is 10^-3.
  report->mouse->rel_x = (contact->x - mouse_abs_x_) / 100;
  report->mouse->rel_y = (contact->y - mouse_abs_y_) / 100;

  report->mouse->pressed_buttons =
      touchpad.button ? fuchsia::ui::input::kMouseButtonPrimary : 0;

  mouse_abs_x_ = touchpad.contacts[0].x;
  mouse_abs_y_ = touchpad.contacts[0].y;
  return true;
}

bool Touchpad::ParseReport(const uint8_t* data, size_t len,
                           fuchsia::ui::input::InputReport* report) {
  FXL_CHECK(report);
  FXL_CHECK(report->mouse);

  Touch::Report touchscreen;
  if (!touch_.ParseReport(data, len, &touchscreen)) {
    return false;
  }

  return ParseTouchpadReport(touchscreen, report);
}

}  // namespace ui_input
