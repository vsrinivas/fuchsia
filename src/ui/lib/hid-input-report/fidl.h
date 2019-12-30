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
  fuchsia_input_report::MouseDescriptor descriptor;
  fuchsia_input_report::MouseDescriptor::Builder builder =
      fuchsia_input_report::MouseDescriptor::Build();

  fidl::VectorView<uint8_t> buttons_view;

  MouseDescriptor data;
};

struct FidlSensorDescriptor {
  fuchsia_input_report::SensorDescriptor descriptor;
  fuchsia_input_report::SensorDescriptor::Builder builder =
      fuchsia_input_report::SensorDescriptor::Build();

  fidl::VectorView<fuchsia_input_report::SensorAxis> values_view;

  SensorDescriptor data;
};

struct FidlContactDescriptor {
  fuchsia_input_report::ContactDescriptor::Builder builder =
      fuchsia_input_report::ContactDescriptor::Build();
};

struct FidlTouchDescriptor {
  fuchsia_input_report::TouchDescriptor descriptor;
  fuchsia_input_report::TouchDescriptor::Builder builder =
      fuchsia_input_report::TouchDescriptor::Build();

  fidl::VectorView<fuchsia_input_report::ContactDescriptor> contacts_view;
  std::array<fuchsia_input_report::ContactDescriptor, fuchsia_input_report::TOUCH_MAX_CONTACTS>
      contacts_built;
  std::array<FidlContactDescriptor, fuchsia_input_report::TOUCH_MAX_CONTACTS> contacts_builder;

  TouchDescriptor data;
};

struct FidlKeyboardDescriptor {
  fuchsia_input_report::KeyboardDescriptor descriptor;
  fuchsia_input_report::KeyboardDescriptor::Builder builder =
      fuchsia_input_report::KeyboardDescriptor::Build();

  fidl::VectorView<::llcpp::fuchsia::ui::input2::Key> keys_view;

  KeyboardDescriptor data;
};

struct FidlDescriptor {
  fuchsia_input_report::DeviceDescriptor::Builder builder =
      fuchsia_input_report::DeviceDescriptor::Build();

  FidlMouseDescriptor mouse;
  FidlSensorDescriptor sensor;
  FidlTouchDescriptor touch;
  FidlKeyboardDescriptor keyboard;
};

struct FidlMouseReport {
  fuchsia_input_report::MouseReport report;
  fuchsia_input_report::MouseReport::Builder builder = fuchsia_input_report::MouseReport::Build();
  fidl::VectorView<uint8_t> buttons_view;

  // Holds the actual data that the builders/views point to.
  MouseReport data;
};

struct FidlSensorReport {
  fuchsia_input_report::SensorReport report;
  fuchsia_input_report::SensorReport::Builder builder = fuchsia_input_report::SensorReport::Build();
  fidl::VectorView<int64_t> values_view;

  // Holds the actual data that the builders/views point to.
  SensorReport data;
};

struct FidlContactReport {
  fuchsia_input_report::ContactReport::Builder builder =
      fuchsia_input_report::ContactReport::Build();
};

struct FidlTouchReport {
  fuchsia_input_report::TouchReport report;
  fuchsia_input_report::TouchReport::Builder builder = fuchsia_input_report::TouchReport::Build();
  std::array<FidlContactReport, fuchsia_input_report::TOUCH_MAX_CONTACTS> contacts;
  std::array<fuchsia_input_report::ContactReport, fuchsia_input_report::TOUCH_MAX_CONTACTS>
      contacts_built;
  fidl::VectorView<fuchsia_input_report::ContactReport> contacts_view;

  // Holds the actual data that the builders/views point to.
  TouchReport data;
};

struct FidlKeyboardReport {
  fuchsia_input_report::KeyboardReport report;
  fuchsia_input_report::KeyboardReport::Builder builder =
      fuchsia_input_report::KeyboardReport::Build();
  fidl::VectorView<::llcpp::fuchsia::ui::input2::Key> pressed_keys_view;

  KeyboardReport data;
};

struct FidlReport {
  fuchsia_input_report::InputReport::Builder builder = fuchsia_input_report::InputReport::Build();
  std::variant<FidlMouseReport, FidlSensorReport, FidlTouchReport, FidlKeyboardReport> report;
};

// Builds the |FidlDescriptor| object from the |ReportDescriptor|.
zx_status_t SetFidlDescriptor(const ReportDescriptor& hid_descriptor, FidlDescriptor* descriptor);
// Builds the |FidlReport| object from the |Report|.
zx_status_t SetFidlReport(const Report& hid_report, FidlReport* report);

}  // namespace hid_input_report

#endif  // SRC_UI_LIB_HID_INPUT_REPORT_FIDL_H_
