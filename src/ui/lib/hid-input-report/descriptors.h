// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_HID_INPUT_REPORT_DESCRIPTORS_H_
#define SRC_UI_LIB_HID_INPUT_REPORT_DESCRIPTORS_H_

#include <fuchsia/input/report/llcpp/fidl.h>
#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <variant>

#include <hid-parser/units.h>
#include <hid-parser/usages.h>

#include "src/ui/lib/hid-input-report/axis.h"

namespace hid_input_report {

// This is just a hardcoded value so we don't have to make memory allocations.
// Feel free to increase this number in the future.
constexpr size_t kMouseMaxButtons = 32;

struct MouseDescriptor {
  std::optional<::llcpp::fuchsia::input::report::Axis> movement_x = {};
  std::optional<::llcpp::fuchsia::input::report::Axis> movement_y = {};
  std::optional<::llcpp::fuchsia::input::report::Axis> scroll_v = {};
  std::optional<::llcpp::fuchsia::input::report::Axis> scroll_h = {};

  uint8_t num_buttons = 0;
  std::array<uint8_t, ::llcpp::fuchsia::input::report::MOUSE_MAX_NUM_BUTTONS> buttons;
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

const uint32_t kSensorMaxValues = 64;

// |SensorDescriptor| describes the capabilities of a sensor device.
struct SensorDescriptor {
  std::array<::llcpp::fuchsia::input::report::SensorAxis,
             ::llcpp::fuchsia::input::report::SENSOR_MAX_VALUES>
      values;
  size_t num_values;
};

// |SensorReport| describes the sensor event delivered from the event stream.
// The values array will always be the same size as the descriptor values, and they
// will always be in the same order.
struct SensorReport {
  int64_t values[kSensorMaxValues];
  size_t num_values;
};

const uint32_t kTouchMaxContacts = 10;

struct ContactDescriptor {
  std::optional<::llcpp::fuchsia::input::report::Axis> contact_id;
  std::optional<::llcpp::fuchsia::input::report::Axis> is_pressed;
  std::optional<::llcpp::fuchsia::input::report::Axis> position_x;
  std::optional<::llcpp::fuchsia::input::report::Axis> position_y;
  std::optional<::llcpp::fuchsia::input::report::Axis> pressure;
  std::optional<::llcpp::fuchsia::input::report::Axis> contact_width;
  std::optional<::llcpp::fuchsia::input::report::Axis> contact_height;
};

struct TouchDescriptor {
  /// The type of touch device being used.
  ::llcpp::fuchsia::input::report::TouchType touch_type;

  uint32_t max_contacts;
  /// This describes each of the contact capabilities.
  std::array<ContactDescriptor, kTouchMaxContacts> contacts;
  size_t num_contacts;
};

/// |Contact| describes one touch on a touch device.
struct ContactReport {
  /// Identifier for the contact.
  /// Note: |contact_id| might not be sequential and will range from 0 to |max_contact_id|.
  uint32_t contact_id;
  bool has_contact_id;

  bool is_pressed;
  bool has_is_pressed;

  /// A contact's position on the x axis.
  int64_t position_x;
  bool has_position_x;
  /// A contact's position on the y axis.
  int64_t position_y;
  bool has_position_y;

  /// Pressure of the contact.
  int64_t pressure;
  bool has_pressure;

  /// Width of the area of contact.
  int64_t contact_width;
  bool has_contact_width;
  /// Height of the area of contact.
  int64_t contact_height;
  bool has_contact_height;
};

/// |TouchReport| describes the current contacts recorded by the touchscreen.
struct TouchReport {
  /// The contacts currently being reported by the device.
  std::array<ContactReport, kTouchMaxContacts> contacts;
  size_t num_contacts;
};

struct KeyboardDescriptor {
  std::array<::llcpp::fuchsia::ui::input2::Key,
             ::llcpp::fuchsia::input::report::KEYBOARD_MAX_NUM_KEYS>
      keys;
  size_t num_keys = 0;
};

struct KeyboardReport {
  // The list of keys currently pressed. These keys are expressed in HID key usages.
  std::array<uint32_t, ::llcpp::fuchsia::input::report::KEYBOARD_MAX_PRESSED_KEYS> pressed_keys;
  size_t num_pressed_keys;
};

struct ReportDescriptor {
  std::variant<MouseDescriptor, SensorDescriptor, TouchDescriptor, KeyboardDescriptor> descriptor;
};

struct Report {
  std::variant<std::monostate, MouseReport, SensorReport, TouchReport, KeyboardReport> report;
};

}  // namespace hid_input_report

#endif  // SRC_UI_LIB_HID_INPUT_REPORT_DESCRIPTORS_H_
