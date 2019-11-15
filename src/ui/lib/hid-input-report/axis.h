// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_HID_INPUT_REPORT_AXIS_H_
#define SRC_UI_LIB_HID_INPUT_REPORT_AXIS_H_

#include <hid-parser/parser.h>
#include <hid-parser/units.h>

namespace hid_input_report {

struct Range {
  int64_t min;
  int64_t max;
};

struct Axis {
  bool enabled = false;
  hid::unit::UnitType unit = hid::unit::UnitType::None;
  Range range = {};
};

void SetAxisFromAttribute(const hid::Attributes& attrs, Axis* axis);

}  // namespace hid_input_report

#endif  // SRC_UI_LIB_HID_INPUT_REPORT_AXIS_H_
