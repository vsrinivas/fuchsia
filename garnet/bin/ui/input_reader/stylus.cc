// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/stylus.h"

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>
#include <src/lib/fxl/logging.h>
#include <stdint.h>
#include <stdio.h>

#include <vector>

#include "garnet/bin/ui/input_reader/device.h"

namespace ui_input {

bool Stylus::ParseReportDescriptor(
    const hid::ReportDescriptor& report_descriptor,
    Descriptor* device_descriptor) {
  hid::Attributes x = {};
  hid::Attributes y = {};
  hid::Attributes pressure = {};
  hid::Attributes tip_switch = {};
  hid::Attributes barrel_switch = {};
  hid::Attributes invert = {};
  hid::Attributes eraser = {};
  hid::Attributes in_range = {};
  uint32_t caps = 0;

  for (size_t i = 0; i < report_descriptor.input_count; i++) {
    const hid::ReportField& field = report_descriptor.input_fields[i];
    if (field.attr.usage == hid::USAGE(hid::usage::Page::kGenericDesktop,
                                       hid::usage::GenericDesktop::kX)) {
      x = field.attr;
      caps |= Capabilities::X;
    } else if (field.attr.usage == hid::USAGE(hid::usage::Page::kGenericDesktop,
                                              hid::usage::GenericDesktop::kY)) {
      y = field.attr;
      caps |= Capabilities::Y;
    } else if (field.attr.usage ==
               hid::USAGE(hid::usage::Page::kDigitizer,
                          hid::usage::Digitizer::kTipPressure)) {
      pressure = field.attr;
      caps |= Capabilities::PRESSURE;
    } else if (field.attr.usage ==
               hid::USAGE(hid::usage::Page::kDigitizer,
                          hid::usage::Digitizer::kBarrelSwitch)) {
      barrel_switch = field.attr;
      caps |= Capabilities::BARREL_SWITCH;
    } else if (field.attr.usage == hid::USAGE(hid::usage::Page::kDigitizer,
                                              hid::usage::Digitizer::kInvert)) {
      invert = field.attr;
      caps |= Capabilities::INVERT;
    } else if (field.attr.usage == hid::USAGE(hid::usage::Page::kDigitizer,
                                              hid::usage::Digitizer::kEraser)) {
      eraser = field.attr;
      caps |= Capabilities::ERASER;
    } else if (field.attr.usage ==
               hid::USAGE(hid::usage::Page::kDigitizer,
                          hid::usage::Digitizer::kInRange)) {
      in_range = field.attr;
      caps |= Capabilities::IN_RANGE;
    }
  }

  // No error, write to class members.
  x_ = x;
  y_ = y;
  pressure_ = pressure;
  tip_switch_ = tip_switch;
  barrel_switch_ = barrel_switch;
  invert_ = invert;
  eraser_ = eraser;
  in_range_ = in_range;
  capabilities_ = caps;

  report_size_ = report_descriptor.input_byte_sz;
  report_id_ = report_descriptor.report_id;

  device_descriptor->protocol = Protocol::Stylus;
  device_descriptor->has_stylus = true;
  device_descriptor->stylus_descriptor =
      fuchsia::ui::input::StylusDescriptor::New();

  device_descriptor->stylus_descriptor->x.range.min = x_.phys_mm.min;
  device_descriptor->stylus_descriptor->x.range.max = x_.phys_mm.max;
  device_descriptor->stylus_descriptor->x.resolution = 1;

  device_descriptor->stylus_descriptor->y.range.min = y_.phys_mm.min;
  device_descriptor->stylus_descriptor->y.range.max = y_.phys_mm.max;
  device_descriptor->stylus_descriptor->y.resolution = 1;

  device_descriptor->stylus_descriptor->is_invertible =
      ((capabilities_ & Capabilities::INVERT) != 0);

  device_descriptor->stylus_descriptor->buttons = 0;
  if (capabilities_ & Capabilities::BARREL_SWITCH) {
    device_descriptor->stylus_descriptor->buttons |=
        fuchsia::ui::input::kStylusBarrel;
  }

  return true;
}

bool Stylus::ParseReport(const uint8_t* data, size_t len,
                         fuchsia::ui::input::InputReport* report) {
  FXL_CHECK(report);
  FXL_CHECK(report->stylus);

  double x = 0;
  double y = 0;
  double pressure = 0;
  bool tip_switch = false;
  bool barrel_switch = false;
  bool invert = false;
  bool eraser = false;
  bool in_range = false;
  uint8_t tmp_val;

  if (len != report_size_) {
    FXL_LOG(INFO) << "Stylus HID Report is not correct size, (" << len
                  << " != " << report_size_ << ")";
    return false;
  }

  // rel_x and rel_y will have units of 10^-5 meters if the report defines units
  hid::Unit length_unit = {};
  length_unit.exp = -5;
  hid::unit::SetSystem(length_unit, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(length_unit, 1);

  if (capabilities_ & Capabilities::X) {
    if (!hid::ExtractAsUnit(data, len, x_, &x)) {
      FXL_LOG(INFO) << "Stylus report: Failed to parse X";
      return false;
    }

    // If this returns true, scan_time was converted. If it returns false,
    // scan_time is unchanged. Either way we return successfully.
    hid::unit::ConvertUnits(x_.unit, x, length_unit, &x);
  }
  if (capabilities_ & Capabilities::Y) {
    if (!hid::ExtractAsUnit(data, len, y_, &y)) {
      FXL_LOG(INFO) << "Stylus report: Failed to parse Y";
      return false;
    }

    // If this returns true, scan_time was converted. If it returns false,
    // scan_time is unchanged. Either way we return successfully.
    hid::unit::ConvertUnits(y_.unit, y, length_unit, &y);
  }
  if (capabilities_ & Capabilities::PRESSURE) {
    if (!hid::ExtractAsUnit(data, len, pressure_, &pressure)) {
      FXL_LOG(INFO) << "Stylus report: Failed to parse PRESSURE";
      return false;
    }
  }
  if (capabilities_ & Capabilities::TIP_SWITCH) {
    if (!hid::ExtractUint(data, len, tip_switch_, &tmp_val)) {
      FXL_LOG(INFO) << "Stylus report: Failed to parse TIP_SWITCH";
      return false;
    }
    tip_switch = (tmp_val == 1);
  }
  if (capabilities_ & Capabilities::BARREL_SWITCH) {
    if (!hid::ExtractUint(data, len, barrel_switch_, &tmp_val)) {
      FXL_LOG(INFO) << "Stylus report: Failed to parse BARREL_SWITCH";
      return false;
    }
    barrel_switch = (tmp_val == 1);
  }
  if (capabilities_ & Capabilities::INVERT) {
    if (!hid::ExtractUint(data, len, invert_, &tmp_val)) {
      FXL_LOG(INFO) << "Stylus report: Failed to parse INVERT";
      return false;
    }
    invert = (tmp_val == 1);
  }
  if (capabilities_ & Capabilities::ERASER) {
    if (!hid::ExtractUint(data, len, eraser_, &tmp_val)) {
      FXL_LOG(INFO) << "Stylus report: Failed to parse ERASER";
      return false;
    }
    eraser = (tmp_val == 1);
  }
  if (capabilities_ & Capabilities::IN_RANGE) {
    if (!hid::ExtractUint(data, len, in_range_, &tmp_val)) {
      FXL_LOG(INFO) << "Stylus report: Failed to parse IN_RANGE";
      return false;
    }
    in_range = (tmp_val == 1);
  }

  // Now that we can't fail, set the real report.
  report->stylus->x = x;
  report->stylus->y = y;
  report->stylus->pressure = pressure;
  report->stylus->is_in_contact = in_range && (tip_switch || eraser);
  report->stylus->is_inverted = invert;
  if (barrel_switch) {
    report->stylus->pressed_buttons |= fuchsia::ui::input::kStylusBarrel;
  }

  return true;
}

}  // namespace ui_input
