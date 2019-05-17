// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/pointer.h"

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>
#include <src/lib/fxl/logging.h>
#include <stdint.h>
#include <stdio.h>

#include <vector>

namespace ui_input {

bool Pointer::ParseReportDescriptor(
    const hid::ReportDescriptor& report_descriptor,
    Descriptor* device_descriptor) {
  hid::Attributes button = {};
  hid::Attributes x = {};
  hid::Attributes y = {};
  uint32_t caps = 0;

  for (size_t i = 0; i < report_descriptor.input_count; i++) {
    const hid::ReportField& field = report_descriptor.input_fields[i];

    if (field.attr.usage == hid::USAGE(hid::usage::Page::kButton, 1)) {
      button = field.attr;
      caps |= Capabilities::BUTTON;

    } else if (field.attr.usage == hid::USAGE(hid::usage::Page::kGenericDesktop,
                                              hid::usage::GenericDesktop::kX)) {
      x = field.attr;
      caps |= Capabilities::X;

    } else if (field.attr.usage == hid::USAGE(hid::usage::Page::kGenericDesktop,
                                              hid::usage::GenericDesktop::kY)) {
      y = field.attr;
      caps |= Capabilities::Y;
    }
  }

  uint32_t base_caps =
      (Capabilities::X | Capabilities::Y | Capabilities::BUTTON);
  if ((caps & base_caps) != base_caps) {
    FXL_LOG(INFO) << "Pointer descriptor: Missing basic capabilities";
    return false;
  }

  // No error, write to class members.
  button_ = button;
  x_ = x;
  y_ = y;
  capabilities_ = caps;

  report_size_ = report_descriptor.input_byte_sz;
  report_id_ = report_descriptor.report_id;

  device_descriptor->protocol = Protocol::Touch;
  device_descriptor->has_touchscreen = true;
  device_descriptor->touch_type = TouchDeviceType::HID;

  device_descriptor->touchscreen_descriptor =
      fuchsia::ui::input::TouchscreenDescriptor::New();
  device_descriptor->touchscreen_descriptor->x.range.min = x.phys_mm.min;
  device_descriptor->touchscreen_descriptor->x.range.max = x.phys_mm.max;
  device_descriptor->touchscreen_descriptor->x.resolution = 1;

  device_descriptor->touchscreen_descriptor->y.range.min = y.phys_mm.min;
  device_descriptor->touchscreen_descriptor->y.range.max = y.phys_mm.max;
  device_descriptor->touchscreen_descriptor->y.resolution = 1;

  device_descriptor->touchscreen_descriptor->max_finger_id = 1;

  return true;
}

bool Pointer::ParseReport(const uint8_t* data, size_t len,
                          fuchsia::ui::input::InputReport* report) {
  FXL_CHECK(report);
  FXL_CHECK(report->touchscreen);

  if (len != report_size_) {
    FXL_LOG(INFO) << "Pointer HID Report is not correct size, (" << len
                  << " != " << report_size_ << ")";
    return false;
  }

  uint32_t button;
  if (capabilities_ & Capabilities::BUTTON) {
    if (!hid::ExtractUint(data, len, button_, &button)) {
      FXL_LOG(INFO) << "Pointer report: Failed to parse BUTTON";
      return false;
    }
    // Return early if the screen is not being pressed.
    if (!button) {
      report->touchscreen->touches.resize(0);
      return true;
    }
  }

  fuchsia::ui::input::Touch touch = {};
  touch.finger_id = 0;
  touch.width = 5;
  touch.height = 5;

  if (capabilities_ & Capabilities::X) {
    uint32_t x = 0;
    if (!hid::ExtractUint(data, len, x_, &x)) {
      FXL_LOG(INFO) << "Pointer report: Failed to parse X";
      return false;
    }
    touch.x = x;
  }

  if (capabilities_ & Capabilities::Y) {
    uint32_t y = 0;
    if (!hid::ExtractUint(data, len, y_, &y)) {
      FXL_LOG(INFO) << "Pointer report: Failed to parse Y";
      return false;
    }
    touch.y = y;
  }

  report->touchscreen->touches.resize(1);
  report->touchscreen->touches.at(0) = std::move(touch);

  return true;
}

}  // namespace ui_input
