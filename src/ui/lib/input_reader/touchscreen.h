// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_INPUT_READER_TOUCHSCREEN_H_
#define SRC_UI_LIB_INPUT_READER_TOUCHSCREEN_H_

#include <cstddef>

#include <hid-parser/parser.h>

#include "src/ui/lib/input_reader/device.h"
#include "src/ui/lib/input_reader/touch.h"

namespace ui_input {
class TouchScreen : public Device {
 public:
  // |Device|
  bool ParseReportDescriptor(const hid::ReportDescriptor& report_descriptor,
                             Device::Descriptor* device_descriptor) override;
  // |Device|
  bool ParseReport(const uint8_t* data, size_t len,
                   fuchsia::ui::input::InputReport* report) override;
  // |Device|
  uint8_t ReportId() const override { return touch_.ReportId(); }

 private:
  Touch touch_ = {};
};
}  // namespace ui_input

#endif  // SRC_UI_LIB_INPUT_READER_TOUCHSCREEN_H_
