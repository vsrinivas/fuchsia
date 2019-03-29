// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/touchscreen.h"

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>

#include <stdint.h>
#include <stdio.h>
#include <vector>

#include "src/lib/fxl/logging.h"

namespace mozart {

bool TouchScreen::ParseReportDescriptor(
    const hid::ReportDescriptor& report_descriptor,
    Device::Descriptor* device_descriptor) {
  if (!touch_.ParseTouchDescriptor(report_descriptor)) {
    return false;
  }

  device_descriptor->protocol = Protocol::Touch;
  device_descriptor->has_touchscreen = true;
  device_descriptor->touch_type = TouchDeviceType::HID;

  Touch::Descriptor touch_desc;
  touch_.SetDescriptor(&touch_desc);

  device_descriptor->touchscreen_descriptor =
      fuchsia::ui::input::TouchscreenDescriptor::New();
  device_descriptor->touchscreen_descriptor->x.range.min = touch_desc.x_min;
  device_descriptor->touchscreen_descriptor->x.range.max = touch_desc.x_max;
  device_descriptor->touchscreen_descriptor->x.resolution =
      touch_desc.x_resolution;

  device_descriptor->touchscreen_descriptor->y.range.min = touch_desc.y_min;
  device_descriptor->touchscreen_descriptor->y.range.max = touch_desc.y_max;
  device_descriptor->touchscreen_descriptor->y.resolution =
      touch_desc.x_resolution;

  device_descriptor->touchscreen_descriptor->max_finger_id =
      touch_desc.max_finger_id;
  return true;
}

bool TouchScreen::ParseReport(const uint8_t* data, size_t len,
                              fuchsia::ui::input::InputReport* report) {
  FXL_CHECK(report);
  FXL_CHECK(report->touchscreen);

  Touch::Report touchscreen;
  if (!touch_.ParseReport(data, len, &touchscreen)) {
    return false;
  }

  report->touchscreen->touches.resize(touchscreen.contact_count);
  for (size_t i = 0; i < touchscreen.contact_count; ++i) {
    fuchsia::ui::input::Touch touch;
    touch.finger_id = touchscreen.contacts[i].id;
    touch.x = touchscreen.contacts[i].x;
    touch.y = touchscreen.contacts[i].y;
    // TODO(SCN-1188): Add support for contact ellipse.
    touch.width = 5;
    touch.height = 5;
    report->touchscreen->touches.at(i) = std::move(touch);
  }
  return true;
}

}  // namespace mozart
