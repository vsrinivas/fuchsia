// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_HID_INPUT_REPORT_TOUCH_H_
#define SRC_UI_LIB_HID_INPUT_REPORT_TOUCH_H_

#include "src/ui/lib/hid-input-report/descriptors.h"
#include "src/ui/lib/hid-input-report/device.h"

namespace hid_input_report {

class Touch : public Device {
 public:
  ParseResult ParseReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor) override;
  ReportDescriptor GetDescriptor() override;

  ParseResult ParseInputReport(const uint8_t* data, size_t len, InputReport* report) override;

  uint8_t InputReportId() const override { return report_id_; }

 private:
  struct ContactConfig {
    hid::Attributes contact_id;
    hid::Attributes tip_switch;
    hid::Attributes position_x;
    hid::Attributes position_y;
    hid::Attributes pressure;
    hid::Attributes confidence;
    hid::Attributes azimuth;
    hid::Attributes contact_width;
    hid::Attributes contact_height;
  };
  ContactConfig contacts_[fuchsia_input_report::TOUCH_MAX_CONTACTS] = {};

  TouchDescriptor descriptor_ = {};

  size_t report_size_ = 0;
  uint8_t report_id_ = 0;
};

}  // namespace hid_input_report

#endif  // SRC_UI_LIB_HID_INPUT_REPORT_TOUCH_H_
