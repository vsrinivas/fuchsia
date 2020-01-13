// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_HID_INPUT_REPORT_KEYBOARD_H_
#define SRC_UI_LIB_HID_INPUT_REPORT_KEYBOARD_H_

#include "src/ui/lib/hid-input-report/descriptors.h"
#include "src/ui/lib/hid-input-report/device.h"

namespace hid_input_report {

class Keyboard : public Device {
 public:
  ParseResult ParseReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor) override;
  ReportDescriptor GetDescriptor() override;

  ParseResult ParseInputReport(const uint8_t* data, size_t len, InputReport* report) override;

  uint8_t InputReportId() const override { return input_report_id_; }

 private:
  ParseResult ParseInputReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor);
  ParseResult ParseOutputReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor);

  // Fields for the input reports.
  size_t num_keys_ = 0;
  // Each item in |key_fields_| represents either a single key or a range of keys.
  // Ranges of keys will have the |kArray| flag set and will send a single key
  // value on each report. Single keys will be 1 if pressed, 0 if unpressed.
  std::array<hid::ReportField, fuchsia_input_report::KEYBOARD_MAX_NUM_KEYS> key_fields_;
  size_t input_report_size_ = 0;
  uint8_t input_report_id_ = 0;

  // Fields for the output reports.
  std::array<hid::ReportField, ::llcpp::fuchsia::input::report::KEYBOARD_MAX_NUM_LEDS> led_fields_;
  size_t num_leds_ = 0;
  uint8_t output_report_id_ = 0;
  size_t output_report_size_ = 0;

  KeyboardDescriptor descriptor_ = {};
};

}  // namespace hid_input_report

#endif  // SRC_UI_LIB_HID_INPUT_REPORT_KEYBOARD_H_
