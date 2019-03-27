// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_STYLUS_H_
#define GARNET_BIN_UI_INPUT_READER_STYLUS_H_

#include <cstddef>

#include "garnet/bin/ui/input_reader/device.h"

#include <hid-parser/parser.h>

namespace mozart {

class Stylus : public Device {
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
    PRESSURE = 1 << 2,
    TIP_SWITCH = 1 << 3,
    BARREL_SWITCH = 1 << 4,
    INVERT = 1 << 5,
    ERASER = 1 << 6,
    IN_RANGE = 1 << 7,
  };

  hid::Attributes x_ = {};
  hid::Attributes y_ = {};
  hid::Attributes pressure_ = {};
  hid::Attributes tip_switch_ = {};
  hid::Attributes barrel_switch_ = {};
  hid::Attributes invert_ = {};
  hid::Attributes eraser_ = {};
  hid::Attributes in_range_ = {};

  uint32_t capabilities_ = 0;
  size_t report_size_ = 0;
  uint8_t report_id_ = 0;
};
}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_STYLUS_H_
