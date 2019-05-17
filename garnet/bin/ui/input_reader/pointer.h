// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_POINTER_H_
#define GARNET_BIN_UI_INPUT_READER_POINTER_H_

#include <hid-parser/parser.h>

#include <cstddef>

#include "garnet/bin/ui/input_reader/device.h"

namespace ui_input {

// This represents a HID pointer device. These can be considered a single
// touch touchscreen by the higher levels.
class Pointer : public Device {
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
    X = 1 << 0,
    Y = 1 << 1,
    BUTTON = 1 << 2,
  };

  hid::Attributes x_ = {};
  hid::Attributes y_ = {};
  hid::Attributes button_ = {};
  uint32_t capabilities_ = 0;
  size_t report_size_ = 0;
  uint8_t report_id_ = 0;
};
}  // namespace ui_input

#endif  // GARNET_BIN_UI_INPUT_READER_POINTER_H_
