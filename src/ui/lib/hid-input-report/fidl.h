// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_HID_INPUT_REPORT_FIDL_H_
#define SRC_UI_LIB_HID_INPUT_REPORT_FIDL_H_

#include <fuchsia/input/report/llcpp/fidl.h>
#include <fuchsia/ui/input2/cpp/fidl.h>

#include "src/ui/lib/hid-input-report/descriptors.h"

namespace hid_input_report {

struct FidlMouseInputDescriptor {
  fuchsia_input_report::MouseInputDescriptor descriptor;
  fuchsia_input_report::MouseInputDescriptor::Builder builder =
      fuchsia_input_report::MouseInputDescriptor::Build();

  fidl::VectorView<uint8_t> buttons_view;

  MouseInputDescriptor data;
};

struct FidlMouseDescriptor {
  fuchsia_input_report::MouseDescriptor descriptor;
  fuchsia_input_report::MouseDescriptor::Builder builder =
      fuchsia_input_report::MouseDescriptor::Build();

  FidlMouseInputDescriptor input;
};

struct FidlSensorInputDescriptor {
  fuchsia_input_report::SensorInputDescriptor descriptor;
  fuchsia_input_report::SensorInputDescriptor::Builder builder =
      fuchsia_input_report::SensorInputDescriptor::Build();

  fidl::VectorView<fuchsia_input_report::SensorAxis> values_view;

  SensorInputDescriptor data;
};

struct FidlSensorDescriptor {
  fuchsia_input_report::SensorDescriptor descriptor;
  fuchsia_input_report::SensorDescriptor::Builder builder =
      fuchsia_input_report::SensorDescriptor::Build();

  FidlSensorInputDescriptor input;
};

struct FidlContactInputDescriptor {
  fuchsia_input_report::ContactInputDescriptor::Builder builder =
      fuchsia_input_report::ContactInputDescriptor::Build();
};

struct FidlTouchInputDescriptor {
  fuchsia_input_report::TouchInputDescriptor descriptor;
  fuchsia_input_report::TouchInputDescriptor::Builder builder =
      fuchsia_input_report::TouchInputDescriptor::Build();

  fidl::VectorView<fuchsia_input_report::ContactInputDescriptor> contacts_view;
  std::array<fuchsia_input_report::ContactInputDescriptor, fuchsia_input_report::TOUCH_MAX_CONTACTS>
      contacts_built;
  std::array<FidlContactInputDescriptor, fuchsia_input_report::TOUCH_MAX_CONTACTS> contacts_builder;

  TouchInputDescriptor data;
};

struct FidlTouchDescriptor {
  fuchsia_input_report::TouchDescriptor descriptor;
  fuchsia_input_report::TouchDescriptor::Builder builder =
      fuchsia_input_report::TouchDescriptor::Build();

  FidlTouchInputDescriptor input;
};

struct FidlKeyboardInputDescriptor {
  fuchsia_input_report::KeyboardInputDescriptor descriptor;
  fuchsia_input_report::KeyboardInputDescriptor::Builder builder =
      fuchsia_input_report::KeyboardInputDescriptor::Build();

  fidl::VectorView<::llcpp::fuchsia::ui::input2::Key> keys_view;

  KeyboardInputDescriptor data;
};

struct FidlKeyboardOutputDescriptor {
  fuchsia_input_report::KeyboardOutputDescriptor descriptor;
  fuchsia_input_report::KeyboardOutputDescriptor::Builder builder =
      fuchsia_input_report::KeyboardOutputDescriptor::Build();

  fidl::VectorView<fuchsia_input_report::LedType> leds_view;

  KeyboardOutputDescriptor data;
};

struct FidlKeyboardDescriptor {
  fuchsia_input_report::KeyboardDescriptor descriptor;
  fuchsia_input_report::KeyboardDescriptor::Builder builder =
      fuchsia_input_report::KeyboardDescriptor::Build();

  FidlKeyboardInputDescriptor input;
  FidlKeyboardOutputDescriptor output;
};

struct FidlDescriptor {
  fuchsia_input_report::DeviceDescriptor::Builder builder =
      fuchsia_input_report::DeviceDescriptor::Build();

  FidlMouseDescriptor mouse;
  FidlSensorDescriptor sensor;
  FidlTouchDescriptor touch;
  FidlKeyboardDescriptor keyboard;
};

struct FidlMouseInputReport {
  fuchsia_input_report::MouseInputReport report;
  fuchsia_input_report::MouseInputReport::Builder builder =
      fuchsia_input_report::MouseInputReport::Build();
  fidl::VectorView<uint8_t> buttons_view;

  // Holds the actual data that the builders/views point to.
  MouseInputReport data;
};

struct FidlSensorInputReport {
  fuchsia_input_report::SensorInputReport report;
  fuchsia_input_report::SensorInputReport::Builder builder =
      fuchsia_input_report::SensorInputReport::Build();
  fidl::VectorView<int64_t> values_view;

  // Holds the actual data that the builders/views point to.
  SensorInputReport data;
};

struct FidlContactInputReport {
  fuchsia_input_report::ContactInputReport::Builder builder =
      fuchsia_input_report::ContactInputReport::Build();
};

struct FidlTouchInputReport {
  fuchsia_input_report::TouchInputReport report;
  fuchsia_input_report::TouchInputReport::Builder builder =
      fuchsia_input_report::TouchInputReport::Build();
  std::array<FidlContactInputReport, fuchsia_input_report::TOUCH_MAX_CONTACTS> contacts;
  std::array<fuchsia_input_report::ContactInputReport, fuchsia_input_report::TOUCH_MAX_CONTACTS>
      contacts_built;
  fidl::VectorView<fuchsia_input_report::ContactInputReport> contacts_view;

  // Holds the actual data that the builders/views point to.
  TouchInputReport data;
};

struct FidlKeyboardInputReport {
  fuchsia_input_report::KeyboardInputReport report;
  fuchsia_input_report::KeyboardInputReport::Builder builder =
      fuchsia_input_report::KeyboardInputReport::Build();
  fidl::VectorView<::llcpp::fuchsia::ui::input2::Key> pressed_keys_view;

  KeyboardInputReport data;
};

struct FidlInputReport {
  fuchsia_input_report::InputReport::Builder builder = fuchsia_input_report::InputReport::Build();

  zx_time_t time;
  std::variant<FidlMouseInputReport, FidlSensorInputReport, FidlTouchInputReport,
               FidlKeyboardInputReport>
      report;
};

// Builds the |FidlDescriptor| object from the |ReportDescriptor|.
zx_status_t SetFidlDescriptor(const ReportDescriptor& hid_descriptor, FidlDescriptor* descriptor);
// Builds the |FidlReport| object from the |Report|.
zx_status_t SetFidlInputReport(const InputReport& hid_report, FidlInputReport* report);

}  // namespace hid_input_report

#endif  // SRC_UI_LIB_HID_INPUT_REPORT_FIDL_H_
