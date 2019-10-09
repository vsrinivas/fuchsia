// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_KEYBOARD_H_
#define GARNET_BIN_UI_INPUT_READER_KEYBOARD_H_

#include <cstddef>

#include <hid-parser/parser.h>

#include "garnet/bin/ui/input_reader/device.h"

namespace ui_input {

class Keyboard : public Device {
 public:
  // |Device|
  bool ParseReportDescriptor(const hid::ReportDescriptor& report_descriptor,
                             Descriptor* device_descriptor) override;
  // |Device|
  bool ParseReport(const uint8_t* data, size_t len,
                   fuchsia::ui::input::InputReport* report) override;
  // |Device|
  uint8_t ReportId() const override { return report_id_; }

 private:
  // Each item in |key_fields_| represents either a single key or a range of keys.
  // Ranges of keys will have the |kArray| flag set and will send a single key
  // value on each report. Single keys will be 1 if pressed, 0 if unpressed.
  std::vector<hid::ReportField> key_fields_;

  uint8_t report_id_ = 0;
};
}  // namespace ui_input

#endif  // GARNET_BIN_UI_INPUT_READER_KEYBOARD_H_
