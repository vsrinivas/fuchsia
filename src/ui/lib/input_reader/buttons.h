// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_INPUT_READER_BUTTONS_H_
#define SRC_UI_LIB_INPUT_READER_BUTTONS_H_

#include <cstddef>

#include <hid-parser/parser.h>

#include "src/ui/lib/input_reader/device.h"
#include "src/ui/lib/input_reader/touch.h"

namespace ui_input {
class Buttons : public Device {
 public:
  // |Device|
  bool ParseReportDescriptor(const hid::ReportDescriptor& report_descriptor,
                             Device::Descriptor* device_descriptor) override;
  // |Device|
  bool ParseReport(const uint8_t* data, size_t len,
                   fuchsia::ui::input::InputReport* report) override;
  // |Device|
  uint8_t ReportId() const override { return report_id_; }

 private:
  enum Capabilities : uint32_t {
    VOLUME_UP = 1 << 0,
    VOLUME_DOWN = 1 << 1,
    RESET = 1 << 2,
    PHONE_MUTE = 1 << 3,
    PAUSE = 1 << 4,
  };
  uint32_t capabilities_ = 0;

  hid::Attributes volume_up_ = {};
  hid::Attributes volume_down_ = {};
  hid::Attributes reset_ = {};
  hid::Attributes phone_mute_ = {};
  hid::Attributes pause_ = {};

  size_t report_size_ = 0;
  uint8_t report_id_ = 0;
};
}  // namespace ui_input

#endif  // SRC_UI_LIB_INPUT_READER_BUTTONS_H_
