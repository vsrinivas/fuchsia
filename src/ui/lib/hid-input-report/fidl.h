// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_HID_INPUT_REPORT_FIDL_H_
#define SRC_UI_LIB_HID_INPUT_REPORT_FIDL_H_

#include <fuchsia/input/report/llcpp/fidl.h>
#include <fuchsia/ui/input2/cpp/fidl.h>

#include "src/ui/lib/hid-input-report/descriptors.h"

namespace hid_input_report {

struct FidlMouseDescriptor {
  ::llcpp::fuchsia::input::report::MouseDescriptor descriptor;
  ::llcpp::fuchsia::input::report::MouseDescriptor::Builder builder =
      ::llcpp::fuchsia::input::report::MouseDescriptor::Build();

  ::llcpp::fuchsia::input::report::Axis movement_x = {};
  ::llcpp::fuchsia::input::report::Axis movement_y = {};
  ::llcpp::fuchsia::input::report::Axis scroll_v = {};
  ::llcpp::fuchsia::input::report::Axis scroll_h = {};
  fidl::VectorView<uint8_t> buttons_view;
  uint8_t buttons[hid_input_report::kMouseMaxButtons] = {};
};

struct FidlSensorDescriptor {
  ::llcpp::fuchsia::input::report::SensorDescriptor descriptor;
  ::llcpp::fuchsia::input::report::SensorDescriptor::Builder builder =
      ::llcpp::fuchsia::input::report::SensorDescriptor::Build();
  std::array<::llcpp::fuchsia::input::report::SensorAxis,
             ::llcpp::fuchsia::input::report::SENSOR_MAX_VALUES>
      values;
  fidl::VectorView<::llcpp::fuchsia::input::report::SensorAxis> values_view;
};

struct FidlContactDescriptor {
  ::llcpp::fuchsia::input::report::ContactDescriptor::Builder builder =
      ::llcpp::fuchsia::input::report::ContactDescriptor::Build();

  // The data for the ContactDescriptor.
  ::llcpp::fuchsia::input::report::Axis contact_id = {};
  ::llcpp::fuchsia::input::report::Axis is_pressed = {};
  ::llcpp::fuchsia::input::report::Axis position_x = {};
  ::llcpp::fuchsia::input::report::Axis position_y = {};
  ::llcpp::fuchsia::input::report::Axis pressure = {};
  ::llcpp::fuchsia::input::report::Axis confidence = {};
  ::llcpp::fuchsia::input::report::Axis contact_width = {};
  ::llcpp::fuchsia::input::report::Axis contact_height = {};
};

struct FidlTouchDescriptor {
  ::llcpp::fuchsia::input::report::TouchDescriptor descriptor;
  ::llcpp::fuchsia::input::report::TouchDescriptor::Builder builder =
      ::llcpp::fuchsia::input::report::TouchDescriptor::Build();

  fidl::VectorView<::llcpp::fuchsia::input::report::ContactDescriptor> contacts_view;
  std::array<::llcpp::fuchsia::input::report::ContactDescriptor,
             ::llcpp::fuchsia::input::report::TOUCH_MAX_CONTACTS>
      contacts_built;
  std::array<FidlContactDescriptor, ::llcpp::fuchsia::input::report::TOUCH_MAX_CONTACTS> contacts;

  uint32_t max_contacts;
  ::llcpp::fuchsia::input::report::TouchType touch_type = {};
};

struct FidlKeyboardDescriptor {
  ::llcpp::fuchsia::input::report::KeyboardDescriptor descriptor;
  ::llcpp::fuchsia::input::report::KeyboardDescriptor::Builder builder =
      ::llcpp::fuchsia::input::report::KeyboardDescriptor::Build();
  fidl::VectorView<::llcpp::fuchsia::ui::input2::Key> keys_view;

  // Holds the actual data that the builders/views point to.
  std::array<::llcpp::fuchsia::ui::input2::Key,
             ::llcpp::fuchsia::input::report::KEYBOARD_MAX_NUM_KEYS>
      keys_data;
};

struct FidlDescriptor {
  ::llcpp::fuchsia::input::report::DeviceDescriptor::Builder builder =
      ::llcpp::fuchsia::input::report::DeviceDescriptor::Build();

  FidlMouseDescriptor mouse;
  FidlSensorDescriptor sensor;
  FidlTouchDescriptor touch;
  FidlKeyboardDescriptor keyboard;
};

struct FidlMouseReport {
  ::llcpp::fuchsia::input::report::MouseReport report;
  ::llcpp::fuchsia::input::report::MouseReport::Builder builder =
      ::llcpp::fuchsia::input::report::MouseReport::Build();
  fidl::VectorView<uint8_t> buttons_view;

  // Holds the actual data that the builders/views point to.
  MouseReport data;
};

struct FidlSensorReport {
  ::llcpp::fuchsia::input::report::SensorReport report;
  ::llcpp::fuchsia::input::report::SensorReport::Builder builder =
      ::llcpp::fuchsia::input::report::SensorReport::Build();
  fidl::VectorView<int64_t> values_view;

  // Holds the actual data that the builders/views point to.
  SensorReport data;
};

struct FidlContactReport {
  ::llcpp::fuchsia::input::report::ContactReport::Builder builder =
      ::llcpp::fuchsia::input::report::ContactReport::Build();
};

struct FidlTouchReport {
  ::llcpp::fuchsia::input::report::TouchReport report;
  ::llcpp::fuchsia::input::report::TouchReport::Builder builder =
      ::llcpp::fuchsia::input::report::TouchReport::Build();
  std::array<FidlContactReport, ::llcpp::fuchsia::input::report::TOUCH_MAX_CONTACTS> contacts;
  std::array<::llcpp::fuchsia::input::report::ContactReport,
             ::llcpp::fuchsia::input::report::TOUCH_MAX_CONTACTS>
      contacts_built;
  fidl::VectorView<::llcpp::fuchsia::input::report::ContactReport> contacts_view;

  // Holds the actual data that the builders/views point to.
  TouchReport data;
};

struct FidlKeyboardReport {
  ::llcpp::fuchsia::input::report::KeyboardReport report;
  ::llcpp::fuchsia::input::report::KeyboardReport::Builder builder =
      ::llcpp::fuchsia::input::report::KeyboardReport::Build();
  fidl::VectorView<::llcpp::fuchsia::ui::input2::Key> pressed_keys_view;

  // Holds the actual data that the builders/views point to.
  std::array<::llcpp::fuchsia::ui::input2::Key,
             ::llcpp::fuchsia::input::report::KEYBOARD_MAX_PRESSED_KEYS>
      pressed_keys_data;
};

struct FidlReport {
  ::llcpp::fuchsia::input::report::InputReport::Builder builder =
      ::llcpp::fuchsia::input::report::InputReport::Build();
  std::variant<FidlMouseReport, FidlSensorReport, FidlTouchReport, FidlKeyboardReport> report;
};

// Builds the |FidlDescriptor| object from the |ReportDescriptor|.
zx_status_t SetFidlDescriptor(const ReportDescriptor& hid_descriptor, FidlDescriptor* descriptor);
// Builds the |FidlReport| object from the |Report|.
zx_status_t SetFidlReport(const Report& hid_report, FidlReport* report);

}  // namespace hid_input_report

#endif  // SRC_UI_LIB_HID_INPUT_REPORT_FIDL_H_
