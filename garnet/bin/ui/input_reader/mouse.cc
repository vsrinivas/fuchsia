// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/mouse.h"

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>

#include <stdint.h>
#include <stdio.h>
#include <vector>

#include <lib/fxl/logging.h>

namespace mozart {
bool Mouse::ParseDescriptor(const hid::ReportDescriptor *desc) {
  hid::Attributes left_click = {};
  hid::Attributes middle_click = {};
  hid::Attributes right_click = {};
  hid::Attributes x = {};
  hid::Attributes y = {};
  uint32_t caps = 0;

  for (size_t i = 0; i < desc->input_count; i++) {
    const hid::ReportField field = desc->input_fields[i];

    if (field.attr.usage == hid::USAGE(hid::usage::Page::kButton, 1)) {
      left_click = field.attr;
      caps |= Capabilities::LEFT_CLICK;

    } else if (field.attr.usage == hid::USAGE(hid::usage::Page::kButton, 2)) {
      middle_click = field.attr;
      caps |= Capabilities::MIDDLE_CLICK;

    } else if (field.attr.usage == hid::USAGE(hid::usage::Page::kButton, 3)) {
      right_click = field.attr;
      caps |= Capabilities::RIGHT_CLICK;

    } else if (field.attr.usage == hid::USAGE(hid::usage::Page::kGenericDesktop,
                                              hid::usage::GenericDesktop::kX)) {
      x = field.attr;
      caps |= Capabilities::X;

    } else if (field.attr.usage == hid::USAGE(hid::usage::Page::kGenericDesktop,
                                              hid::usage::GenericDesktop::kY)) {
      y = field.attr;
      caps |= Capabilities::Y;
    }
  }

  uint32_t base_caps =
      (Capabilities::X | Capabilities::Y | Capabilities::LEFT_CLICK);
  if ((caps & base_caps) != base_caps) {
    FXL_LOG(ERROR) << "Mouse descriptor: Missing basic capabilities";
    return false;
  }

  // No error, write to class members.
  left_click_ = left_click;
  middle_click_ = middle_click;
  right_click_ = right_click;
  x_ = x;
  y_ = y;
  capabilities_ = caps;

  report_size_ = desc->input_byte_sz;
  report_id_ = desc->report_id;
  return true;
}

bool Mouse::ParseReport(const uint8_t *data, size_t len, Report *report) const {
  FXL_CHECK(report);

  hid::Report hid_report = {data, len};
  if (len != report_size_) {
    FXL_LOG(ERROR) << "Mouse HID Report is not correct size, (" << len
                   << " != " << report_size_ << ")";
    return false;
  }

  uint32_t clicked;
  if (capabilities_ & Capabilities::LEFT_CLICK) {
    if (!hid::ExtractUint(hid_report, left_click_, &clicked)) {
      FXL_LOG(ERROR) << "Mouse report: Failed to parse LEFT_CLICK";
      return false;
    }
    report->left_click = (clicked == 1);
  }
  if (capabilities_ & Capabilities::MIDDLE_CLICK) {
    if (!hid::ExtractUint(hid_report, middle_click_, &clicked)) {
      FXL_LOG(ERROR) << "Mouse report: Failed to parse MIDDLE_CLICK";
      return false;
    }
    report->middle_click = (clicked == 1);
  }
  if (capabilities_ & Capabilities::RIGHT_CLICK) {
    if (!hid::ExtractUint(hid_report, right_click_, &clicked)) {
      FXL_LOG(ERROR) << "Mouse report: Failed to parse RIGHT_CLICK";
      return false;
    }
    report->right_click = (clicked == 1);
  }

  // rel_x and rel_y will have units of 10^-5 meters if the report defines units
  hid::Unit length_unit = {};
  length_unit.exp = -5;
  hid::unit::SetSystem(length_unit, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(length_unit, 1);

  if (capabilities_ & Capabilities::X) {
    double x;
    if (!hid::ExtractAsUnit(hid_report, x_, &x)) {
      FXL_LOG(ERROR) << "Mouse report: Failed to parse X";
      return false;
    }

    // If this returns true, scan_time was converted. If it returns false,
    // scan_time is unchanged. Either way we return successfully.
    hid::unit::ConvertUnits(x_.unit, x, length_unit, &x);

    report->rel_x = static_cast<uint32_t>(x);
  }
  if (capabilities_ & Capabilities::Y) {
    double y;
    if (!hid::ExtractAsUnit(hid_report, y_, &y)) {
      FXL_LOG(ERROR) << "Mouse report: Failed to parse Y";
      return false;
    }

    // If this returns true, scan_time was converted. If it returns false,
    // scan_time is unchanged. Either way we return successfully.
    hid::unit::ConvertUnits(y_.unit, y, length_unit, &y);

    report->rel_y = static_cast<uint32_t>(y);
  }

  return true;
}

}  // namespace mozart
