// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HID_INPUT_REPORT_DESCRIPTORS_H_
#define HID_INPUT_REPORT_DESCRIPTORS_H_

#include <stddef.h>
#include <stdint.h>

#include <variant>

#include <hid-parser/units.h>

namespace hid_input_report {

struct Range {
  int64_t min;
  int64_t max;
};

struct Axis {
  bool enabled = false;
  hid::unit::UnitType unit = hid::unit::UnitType::None;
  Range range = {};
};

// This is just a hardcoded value so we don't have to make memory allocations.
// Feel free to increase this number in the future.
constexpr size_t kMouseMaxButtons = 32;

struct MouseDescriptor {
  Axis movement_x = {};
  Axis movement_y = {};
  Axis scroll_v = {};
  Axis scroll_h = {};

  uint8_t num_buttons = 0;
  uint8_t button_ids[kMouseMaxButtons];
};

struct MouseReport {
  bool has_movement_x = false;
  int64_t movement_x = 0;

  bool has_movement_y = false;
  int64_t movement_y = 0;

  bool has_scroll_v = false;
  int64_t scroll_v = 0;

  bool has_scroll_h = false;
  int64_t scroll_h = 0;

  uint8_t num_buttons_pressed;
  uint8_t buttons_pressed[kMouseMaxButtons];
};

struct ReportDescriptor {
  std::variant<MouseDescriptor> descriptor;
};

struct Report {
  std::variant<std::monostate, MouseReport> report;
};

}  // namespace hid_input_report

#endif  // HID_INPUT_REPORT_DESCRIPTORS_H_
