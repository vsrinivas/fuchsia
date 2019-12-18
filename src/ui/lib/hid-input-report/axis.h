// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_HID_INPUT_REPORT_AXIS_H_
#define SRC_UI_LIB_HID_INPUT_REPORT_AXIS_H_

#include <fuchsia/input/report/llcpp/fidl.h>

#include <hid-parser/parser.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>

namespace hid_input_report {

::llcpp::fuchsia::input::report::Unit HidUnitToLlcppUnit(hid::unit::UnitType unit);

zx_status_t HidSensorUsageToLlcppSensorType(hid::usage::Sensor usage,
                                            ::llcpp::fuchsia::input::report::SensorType* type);

::llcpp::fuchsia::input::report::Axis LlcppAxisFromAttribute(const hid::Attributes& attrs);

}  // namespace hid_input_report

#endif  // SRC_UI_LIB_HID_INPUT_REPORT_AXIS_H_
