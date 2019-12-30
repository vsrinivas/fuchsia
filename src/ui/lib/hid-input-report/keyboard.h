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

  ParseResult ParseReport(const uint8_t* data, size_t len, Report* report) override;

  uint8_t ReportId() const override { return report_id_; }

 private:
  // Each item in |key_fields_| represents either a single key or a range of keys.
  // Ranges of keys will have the |kArray| flag set and will send a single key
  // value on each report. Single keys will be 1 if pressed, 0 if unpressed.
  std::array<hid::ReportField, fuchsia_input_report::KEYBOARD_MAX_NUM_KEYS> key_fields_;
  size_t num_keys_ = 0;

  KeyboardDescriptor descriptor_ = {};

  size_t report_size_ = 0;
  uint8_t report_id_ = 0;
};

}  // namespace hid_input_report

#endif  // SRC_UI_LIB_HID_INPUT_REPORT_KEYBOARD_H_
