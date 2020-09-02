// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_LIB_HID_INPUT_REPORT_KEYBOARD_H_
#define SRC_UI_INPUT_LIB_HID_INPUT_REPORT_KEYBOARD_H_

#include <set>

#include "src/ui/input/lib/hid-input-report/device.h"

namespace hid_input_report {

class Keyboard : public Device {
 public:
  ParseResult ParseReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor) override;

  ParseResult SetOutputReport(const fuchsia_input_report::OutputReport* report, uint8_t* data,
                              size_t data_size, size_t* data_out_size) override;

  ParseResult CreateDescriptor(
      fidl::Allocator* allocator,
      fuchsia_input_report::DeviceDescriptor::Builder* descriptor) override;

  ParseResult ParseInputReport(const uint8_t* data, size_t len, fidl::Allocator* allocator,
                               fuchsia_input_report::InputReport::Builder* report) override;

  uint8_t InputReportId() const override { return input_report_id_; }

  DeviceType GetDeviceType() const override { return DeviceType::kKeyboard; }

 private:
  ParseResult ParseInputReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor);
  ParseResult ParseOutputReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor);

  // Fields for the input reports.
  // Each item in |key_fields_| represents either a single key or a range of keys.
  // Ranges of keys will have the |kArray| flag set and will send a single key
  // value on each report. Single keys will be 1 if pressed, 0 if unpressed.
  size_t num_key_fields_;
  std::array<hid::ReportField, fuchsia_input_report::KEYBOARD_MAX_NUM_KEYS> key_fields_;
  size_t input_report_size_ = 0;
  uint8_t input_report_id_ = 0;

  // The ordered, unique list of key values.
  std::set<::llcpp::fuchsia::ui::input2::Key> key_values_;
  // The ordered, unique list of key values.
  std::set<::llcpp::fuchsia::input::Key> key_3_values_;

  // Fields for the output reports.
  std::array<hid::ReportField, ::llcpp::fuchsia::input::report::KEYBOARD_MAX_NUM_LEDS> led_fields_;
  size_t num_leds_ = 0;
  uint8_t output_report_id_ = 0;
  size_t output_report_size_ = 0;
};

}  // namespace hid_input_report

#endif  // SRC_UI_INPUT_LIB_HID_INPUT_REPORT_KEYBOARD_H_
