// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HID_INPUT_REPORT_DESCRIPTORS_H_
#define HID_INPUT_REPORT_DESCRIPTORS_H_

#include <stddef.h>
#include <stdint.h>

#include <variant>

#include <hid-input-report/axis.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>

namespace hid_input_report {

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

// A |SensorAxis| has both a normal |Axis| and also the |SensorType|.
struct SensorAxis {
  Axis axis;
  // The hid usage type for the sensor.
  hid::usage::Sensor type;
};

const uint32_t kSensorMaxValues = 64;

// |SensorDescriptor| describes the capabilities of a sensor device.
struct SensorDescriptor {
  SensorAxis values[kSensorMaxValues] = {};
  size_t num_values;
};

// |SensorReport| describes the sensor event delivered from the event stream.
// The values array will always be the same size as the descriptor values, and they
// will always be in the same order.
struct SensorReport {
  int64_t values[kSensorMaxValues];
  size_t num_values;
};

struct ReportDescriptor {
  std::variant<MouseDescriptor, SensorDescriptor> descriptor;
};

struct Report {
  std::variant<std::monostate, MouseReport, SensorReport> report;
};

}  // namespace hid_input_report

#endif  // HID_INPUT_REPORT_DESCRIPTORS_H_
