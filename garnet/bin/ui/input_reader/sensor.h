// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_SENSOR_H_
#define GARNET_BIN_UI_INPUT_READER_SENSOR_H_

#include <hid-parser/parser.h>

#include <cstddef>

#include "garnet/bin/ui/input_reader/device.h"

namespace ui_input {

class Sensor : public Device {
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
  enum Capabilities : uint32_t {
    X = 1 << 0,
    Y = 1 << 1,
    Z = 1 << 2,
    SCALAR = 1 << 3,
  };
  uint32_t capabilities_ = 0;

  hid::Attributes x_ = {};
  hid::Attributes y_ = {};
  hid::Attributes z_ = {};
  hid::Attributes scalar_ = {};

  size_t report_size_ = 0;
  uint8_t report_id_ = 0;
};

}  // namespace ui_input

#endif  // GARNET_BIN_UI_INPUT_READER_SENSOR_H_
