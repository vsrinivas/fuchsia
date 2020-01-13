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

struct MouseInputDescriptor {
  std::optional<fuchsia_input_report::Axis> movement_x = {};
  std::optional<fuchsia_input_report::Axis> movement_y = {};
  std::optional<fuchsia_input_report::Axis> scroll_v = {};
  std::optional<fuchsia_input_report::Axis> scroll_h = {};

  uint8_t num_buttons = 0;
  std::array<uint8_t, fuchsia_input_report::MOUSE_MAX_NUM_BUTTONS> buttons;
};

struct MouseInputReport {
  std::optional<int64_t> movement_x;
  std::optional<int64_t> movement_y;
  std::optional<int64_t> scroll_v;
  std::optional<int64_t> scroll_h;
  uint8_t num_buttons_pressed;
  std::array<uint8_t, fuchsia_input_report::MOUSE_MAX_NUM_BUTTONS> buttons_pressed;
};

// |SensorInputDescriptor| describes the capabilities of a sensor device.
struct SensorInputDescriptor {
  std::array<fuchsia_input_report::SensorAxis, fuchsia_input_report::SENSOR_MAX_VALUES> values;
  size_t num_values;
};

// |SensorInputReport| describes the sensor event delivered from the event stream.
// The values array will always be the same size as the descriptor values, and they
// will always be in the same order.
struct SensorInputReport {
  std::array<int64_t, fuchsia_input_report::SENSOR_MAX_VALUES> values;
  size_t num_values;
};

struct ContactInputDescriptor {
  std::optional<fuchsia_input_report::Axis> contact_id;
  std::optional<fuchsia_input_report::Axis> is_pressed;
  std::optional<fuchsia_input_report::Axis> position_x;
  std::optional<fuchsia_input_report::Axis> position_y;
  std::optional<fuchsia_input_report::Axis> pressure;
  std::optional<fuchsia_input_report::Axis> contact_width;
  std::optional<fuchsia_input_report::Axis> contact_height;
};

struct TouchInputDescriptor {
  /// The type of touch device being used.
  fuchsia_input_report::TouchType touch_type;

  uint32_t max_contacts;
  /// This describes each of the contact capabilities.
  std::array<ContactInputDescriptor, fuchsia_input_report::TOUCH_MAX_CONTACTS> contacts;
  size_t num_contacts;
};

struct TouchDescriptor {
  std::optional<TouchInputDescriptor> input;
};

/// |Contact| describes one touch on a touch device.
struct ContactInputReport {
  /// Identifier for the contact.
  /// Note: |contact_id| might not be sequential and will range from 0 to |max_contact_id|.
  std::optional<uint32_t> contact_id;
  std::optional<bool> is_pressed;
  std::optional<int64_t> position_x;
  std::optional<int64_t> position_y;
  std::optional<int64_t> pressure;
  std::optional<int64_t> contact_width;
  std::optional<int64_t> contact_height;
};

/// |TouchInputReport| describes the current contacts recorded by the touchscreen.
struct TouchInputReport {
  /// The contacts currently being reported by the device.
  std::array<ContactInputReport, fuchsia_input_report::TOUCH_MAX_CONTACTS> contacts;
  size_t num_contacts;
};

struct KeyboardInputDescriptor {
  std::array<::llcpp::fuchsia::ui::input2::Key, fuchsia_input_report::KEYBOARD_MAX_NUM_KEYS> keys;
  size_t num_keys = 0;
};

struct KeyboardOutputDescriptor {
  std::array<fuchsia_input_report::LedType, fuchsia_input_report::KEYBOARD_MAX_NUM_LEDS> leds;
  size_t num_leds;
};

struct KeyboardDescriptor {
  std::optional<KeyboardInputDescriptor> input;
  std::optional<KeyboardOutputDescriptor> output;
};

struct KeyboardInputReport {
  std::array<::llcpp::fuchsia::ui::input2::Key, fuchsia_input_report::KEYBOARD_MAX_NUM_KEYS>
      pressed_keys;
  size_t num_pressed_keys;
};

struct KeyboardOutputReport {
  std::array<fuchsia_input_report::LedType, fuchsia_input_report::KEYBOARD_MAX_NUM_LEDS>
      enabled_leds;
  size_t num_enabled_leds;
};

struct MouseDescriptor {
  std::optional<MouseInputDescriptor> input;
};

struct SensorDescriptor {
  std::optional<SensorInputDescriptor> input;
};

struct ReportDescriptor {
  std::variant<MouseDescriptor, SensorDescriptor, TouchDescriptor, KeyboardDescriptor> descriptor;
};

struct InputReport {
  std::variant<std::monostate, MouseInputReport, SensorInputReport, TouchInputReport,
               KeyboardInputReport>
      report;
};

}  // namespace hid_input_report

#endif  // SRC_UI_LIB_HID_INPUT_REPORT_DESCRIPTORS_H_
