// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_reader/mouse.h"

#include <stdint.h>
#include <stdio.h>

#include <vector>

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/input_reader/device.h"

namespace ui_input {

bool Mouse::ParseReportDescriptor(const hid::ReportDescriptor& report_descriptor,
                                  Descriptor* device_descriptor) {
  hid::Attributes left_click = {};
  hid::Attributes middle_click = {};
  hid::Attributes right_click = {};
  hid::Attributes x = {};
  hid::Attributes y = {};
  uint32_t caps = 0;

  for (size_t i = 0; i < report_descriptor.input_count; i++) {
    const hid::ReportField& field = report_descriptor.input_fields[i];

    if (field.attr.usage == hid::USAGE(hid::usage::Page::kButton, 1)) {
      left_click = field.attr;
      caps |= Capabilities::LEFT_CLICK;

    } else if (field.attr.usage == hid::USAGE(hid::usage::Page::kButton, 2)) {
      middle_click = field.attr;
      caps |= Capabilities::MIDDLE_CLICK;

    } else if (field.attr.usage == hid::USAGE(hid::usage::Page::kButton, 3)) {
      right_click = field.attr;
      caps |= Capabilities::RIGHT_CLICK;

    } else if (field.attr.usage ==
               hid::USAGE(hid::usage::Page::kGenericDesktop, hid::usage::GenericDesktop::kX)) {
      x = field.attr;
      caps |= Capabilities::X;

    } else if (field.attr.usage ==
               hid::USAGE(hid::usage::Page::kGenericDesktop, hid::usage::GenericDesktop::kY)) {
      y = field.attr;
      caps |= Capabilities::Y;
    }
  }

  uint32_t base_caps = (Capabilities::X | Capabilities::Y | Capabilities::LEFT_CLICK);
  if ((caps & base_caps) != base_caps) {
    FXL_LOG(INFO) << "Mouse descriptor: Missing basic capabilities";
    return false;
  }

  // No error, write to class members.
  left_click_ = left_click;
  middle_click_ = middle_click;
  right_click_ = right_click;
  x_ = x;
  y_ = y;
  capabilities_ = caps;

  report_size_ = report_descriptor.input_byte_sz;
  report_id_ = report_descriptor.report_id;

  // Set the device descriptor.
  device_descriptor->protocol = Protocol::Mouse;
  device_descriptor->has_mouse = true;
  device_descriptor->mouse_type = MouseDeviceType::HID;
  device_descriptor->mouse_descriptor = fuchsia::ui::input::MouseDescriptor::New();
  // At the moment all mice send relative units, so these min and max values
  // do not affect anything. Set them to maximum range.
  device_descriptor->mouse_descriptor->rel_x.range.min = INT32_MIN;
  device_descriptor->mouse_descriptor->rel_x.range.max = INT32_MAX;
  device_descriptor->mouse_descriptor->rel_x.resolution = 1;

  device_descriptor->mouse_descriptor->rel_y.range.min = INT32_MIN;
  device_descriptor->mouse_descriptor->rel_y.range.max = INT32_MAX;
  device_descriptor->mouse_descriptor->rel_y.resolution = 1;

  device_descriptor->mouse_descriptor->buttons |= fuchsia::ui::input::kMouseButtonPrimary;
  device_descriptor->mouse_descriptor->buttons |= fuchsia::ui::input::kMouseButtonSecondary;
  device_descriptor->mouse_descriptor->buttons |= fuchsia::ui::input::kMouseButtonTertiary;

  return true;
}

bool Mouse::ParseReport(const uint8_t* data, size_t len, fuchsia::ui::input::InputReport* report) {
  FXL_CHECK(report);
  FXL_CHECK(report->mouse);

  Report mouse_report = {};
  if (len != report_size_) {
    FXL_LOG(INFO) << "Mouse HID Report is not correct size, (" << len << " != " << report_size_
                  << ")";
    return false;
  }

  uint32_t clicked;
  if (capabilities_ & Capabilities::LEFT_CLICK) {
    if (!hid::ExtractUint(data, len, left_click_, &clicked)) {
      FXL_LOG(INFO) << "Mouse report: Failed to parse LEFT_CLICK";
      return false;
    }
    mouse_report.left_click = (clicked == 1);
  }
  if (capabilities_ & Capabilities::MIDDLE_CLICK) {
    if (!hid::ExtractUint(data, len, middle_click_, &clicked)) {
      FXL_LOG(INFO) << "Mouse report: Failed to parse MIDDLE_CLICK";
      return false;
    }
    mouse_report.middle_click = (clicked == 1);
  }
  if (capabilities_ & Capabilities::RIGHT_CLICK) {
    if (!hid::ExtractUint(data, len, right_click_, &clicked)) {
      FXL_LOG(INFO) << "Mouse report: Failed to parse RIGHT_CLICK";
      return false;
    }
    mouse_report.right_click = (clicked == 1);
  }

  // rel_x and rel_y will have units of 10^-5 meters if the report defines units
  hid::Unit length_unit = {};
  length_unit.exp = -5;
  hid::unit::SetSystem(length_unit, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(length_unit, 1);

  if (capabilities_ & Capabilities::X) {
    double x;
    if (!hid::ExtractAsUnit(data, len, x_, &x)) {
      FXL_LOG(INFO) << "Mouse report: Failed to parse X";
      return false;
    }

    // If this returns true, scan_time was converted. If it returns false,
    // scan_time is unchanged. Either way we return successfully.
    hid::unit::ConvertUnits(x_.unit, x, length_unit, &x);

    mouse_report.rel_x = static_cast<int32_t>(x);
  }
  if (capabilities_ & Capabilities::Y) {
    double y;
    if (!hid::ExtractAsUnit(data, len, y_, &y)) {
      FXL_LOG(INFO) << "Mouse report: Failed to parse Y";
      return false;
    }

    // If this returns true, scan_time was converted. If it returns false,
    // scan_time is unchanged. Either way we return successfully.
    hid::unit::ConvertUnits(y_.unit, y, length_unit, &y);

    mouse_report.rel_y = static_cast<int32_t>(y);
  }

  // Now that we can't fail, set the real report.
  report->mouse->rel_x = mouse_report.rel_x;
  report->mouse->rel_y = mouse_report.rel_y;

  report->mouse->pressed_buttons = 0;
  report->mouse->pressed_buttons |=
      mouse_report.left_click ? fuchsia::ui::input::kMouseButtonPrimary : 0;
  report->mouse->pressed_buttons |=
      mouse_report.middle_click ? fuchsia::ui::input::kMouseButtonSecondary : 0;
  report->mouse->pressed_buttons |=
      mouse_report.right_click ? fuchsia::ui::input::kMouseButtonTertiary : 0;

  return true;
}

}  // namespace ui_input
