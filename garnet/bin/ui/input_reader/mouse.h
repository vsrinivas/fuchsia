// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_MOUSE_H_
#define GARNET_BIN_UI_INPUT_READER_MOUSE_H_

#include <hid-parser/parser.h>

#include <cstddef>

#include "garnet/bin/ui/input_reader/device.h"

namespace ui_input {

class Mouse : public Device {
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
    LEFT_CLICK = 1 << 0,
    MIDDLE_CLICK = 1 << 1,
    RIGHT_CLICK = 1 << 2,
    X = 1 << 3,
    Y = 1 << 4,
  };

  struct Report {
    bool left_click;
    bool middle_click;
    bool right_click;
    // These are the relative changes in X and Y. Most mouse reports don't
    // declare units, and just have these range from -127 to 127. However,
    // if they do declare units then rel_x and rel_y will be in the 10s
    // of microns, 10^-5 m, to be consistent with the touch units.
    int32_t rel_x;
    int32_t rel_y;
  };
  hid::Attributes x_ = {};
  hid::Attributes y_ = {};
  hid::Attributes left_click_ = {};
  hid::Attributes middle_click_ = {};
  hid::Attributes right_click_ = {};

  uint32_t capabilities_ = 0;
  size_t report_size_ = 0;
  uint8_t report_id_ = 0;
};
}  // namespace ui_input

#endif  // GARNET_BIN_UI_INPUT_READER_MOUSE_H_
