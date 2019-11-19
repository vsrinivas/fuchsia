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
  FidlMouseDescriptor() {}
  ::llcpp::fuchsia::input::report::MouseDescriptor mouse_descriptor;
  ::llcpp::fuchsia::input::report::MouseDescriptor::Builder mouse_builder =
      ::llcpp::fuchsia::input::report::MouseDescriptor::Build();

  ::llcpp::fuchsia::input::report::Axis movement_x = {};
  ::llcpp::fuchsia::input::report::Axis movement_y = {};
  ::llcpp::fuchsia::input::report::Axis scroll_v = {};
  ::llcpp::fuchsia::input::report::Axis scroll_h = {};
  fidl::VectorView<uint8_t> buttons_view;
  uint8_t buttons[hid_input_report::kMouseMaxButtons] = {};
};

struct FidlSensorDescriptor {
  FidlSensorDescriptor() {}
  ::llcpp::fuchsia::input::report::SensorDescriptor sensor_descriptor;
  ::llcpp::fuchsia::input::report::SensorDescriptor::Builder sensor_builder =
      ::llcpp::fuchsia::input::report::SensorDescriptor::Build();
  std::array<::llcpp::fuchsia::input::report::SensorAxis,
             ::llcpp::fuchsia::input::report::SENSOR_MAX_VALUES>
      values;
  fidl::VectorView<::llcpp::fuchsia::input::report::SensorAxis> values_view;
};

struct FidlContactDescriptor {
  FidlContactDescriptor() {}
  ::llcpp::fuchsia::input::report::ContactDescriptor::Builder contact_builder =
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
  FidlTouchDescriptor() {}
  ::llcpp::fuchsia::input::report::TouchDescriptor touch_descriptor;
  ::llcpp::fuchsia::input::report::TouchDescriptor::Builder touch_builder =
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
  FidlKeyboardDescriptor() {}
  ::llcpp::fuchsia::input::report::KeyboardDescriptor keyboard_descriptor;
  ::llcpp::fuchsia::input::report::KeyboardDescriptor::Builder keyboard_builder =
      ::llcpp::fuchsia::input::report::KeyboardDescriptor::Build();
  fidl::VectorView<::llcpp::fuchsia::ui::input2::Key> keys_view;

  // Holds the actual data that the builders/views point to.
  std::array<::llcpp::fuchsia::ui::input2::Key,
             ::llcpp::fuchsia::input::report::KEYBOARD_MAX_NUM_KEYS>
      keys_data;
};

struct FidlDescriptor {
  FidlDescriptor() {}
  ::llcpp::fuchsia::input::report::DeviceDescriptor::Builder descriptor_builder =
      ::llcpp::fuchsia::input::report::DeviceDescriptor::Build();

  FidlMouseDescriptor mouse_descriptor;
  FidlSensorDescriptor sensor_descriptor;
  FidlTouchDescriptor touch_descriptor;
  FidlKeyboardDescriptor keyboard_descriptor;
};

struct FidlMouseReport {
  FidlMouseReport() {}
  ::llcpp::fuchsia::input::report::MouseReport mouse_report;
  ::llcpp::fuchsia::input::report::MouseReport::Builder mouse_builder =
      ::llcpp::fuchsia::input::report::MouseReport::Build();
  fidl::VectorView<uint8_t> buttons_view;

  // Holds the actual data that the builders/views point to.
  MouseReport report_data;
};

struct FidlSensorReport {
  FidlSensorReport() {}
  ::llcpp::fuchsia::input::report::SensorReport sensor_report;
  ::llcpp::fuchsia::input::report::SensorReport::Builder sensor_builder =
      ::llcpp::fuchsia::input::report::SensorReport::Build();
  fidl::VectorView<int64_t> values_view;

  // Holds the actual data that the builders/views point to.
  SensorReport report_data;
};

struct FidlContactReport {
  ::llcpp::fuchsia::input::report::ContactReport::Builder contact =
      ::llcpp::fuchsia::input::report::ContactReport::Build();
};

struct FidlTouchReport {
  FidlTouchReport() {}
  ::llcpp::fuchsia::input::report::TouchReport touch_report;
  ::llcpp::fuchsia::input::report::TouchReport::Builder touch_builder =
      ::llcpp::fuchsia::input::report::TouchReport::Build();
  std::array<FidlContactReport, ::llcpp::fuchsia::input::report::TOUCH_MAX_CONTACTS> contacts;
  std::array<::llcpp::fuchsia::input::report::ContactReport,
             ::llcpp::fuchsia::input::report::TOUCH_MAX_CONTACTS>
      contacts_built;
  fidl::VectorView<::llcpp::fuchsia::input::report::ContactReport> contacts_view;

  // Holds the actual data that the builders/views point to.
  TouchReport report_data;
};

struct FidlKeyboardReport {
  FidlKeyboardReport() {}
  ::llcpp::fuchsia::input::report::KeyboardReport keyboard_report;
  ::llcpp::fuchsia::input::report::KeyboardReport::Builder keyboard_builder =
      ::llcpp::fuchsia::input::report::KeyboardReport::Build();
  fidl::VectorView<::llcpp::fuchsia::ui::input2::Key> pressed_keys_view;

  // Holds the actual data that the builders/views point to.
  std::array<::llcpp::fuchsia::ui::input2::Key,
             ::llcpp::fuchsia::input::report::KEYBOARD_MAX_PRESSED_KEYS>
      pressed_keys_data;
};

struct FidlReport {
  FidlReport() {}
  ::llcpp::fuchsia::input::report::InputReport::Builder report_builder =
      ::llcpp::fuchsia::input::report::InputReport::Build();
  std::variant<FidlMouseReport, FidlSensorReport, FidlTouchReport, FidlKeyboardReport> report;
};

// Builds the |FidlDescriptor| object from the |ReportDescriptor|.
zx_status_t SetFidlDescriptor(const ReportDescriptor& hid_descriptor, FidlDescriptor* descriptor);
// Builds the |FidlReport| object from the |Report|.
zx_status_t SetFidlReport(const Report& hid_report, FidlReport* report);

}  // namespace hid_input_report

#endif  // SRC_UI_LIB_HID_INPUT_REPORT_FIDL_H_
