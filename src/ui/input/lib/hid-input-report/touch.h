// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_LIB_HID_INPUT_REPORT_TOUCH_H_
#define SRC_UI_INPUT_LIB_HID_INPUT_REPORT_TOUCH_H_

#include "src/ui/input/lib/hid-input-report/device.h"

namespace hid_input_report {

class Touch : public Device {
 public:
  ParseResult ParseReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor) override;

  ParseResult CreateDescriptor(fidl::AnyArena& allocator,
                               fuchsia_input_report::wire::DeviceDescriptor& descriptor) override;

  ParseResult ParseInputReport(const uint8_t* data, size_t len, fidl::AnyArena& allocator,
                               fuchsia_input_report::wire::InputReport& input_report) override;

  uint8_t InputReportId() const override { return report_id_; }

  DeviceType GetDeviceType() const override { return DeviceType::kTouch; }

 private:
  struct ContactConfig {
    std::optional<hid::Attributes> contact_id;
    std::optional<hid::Attributes> tip_switch;
    std::optional<hid::Attributes> position_x;
    std::optional<hid::Attributes> position_y;
    std::optional<hid::Attributes> pressure;
    std::optional<hid::Attributes> contact_width;
    std::optional<hid::Attributes> contact_height;
  };
  ContactConfig contacts_[fuchsia_input_report::wire::kTouchMaxContacts] = {};
  size_t num_contacts_ = 0;

  hid::Attributes buttons_[fuchsia_input_report::wire::kTouchMaxNumButtons] = {};
  size_t num_buttons_ = 0;

  fuchsia_input_report::wire::TouchType touch_type_;

  size_t report_size_ = 0;
  uint8_t report_id_ = 0;
};

}  // namespace hid_input_report

#endif  // SRC_UI_INPUT_LIB_HID_INPUT_REPORT_TOUCH_H_
