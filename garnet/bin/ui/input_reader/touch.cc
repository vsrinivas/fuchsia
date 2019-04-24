// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/touch.h"

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>
#include <stdint.h>
#include <stdio.h>

#include <vector>

#include "src/lib/fxl/logging.h"

namespace ui_input {
bool Touch::ParseTouchDescriptor(const hid::ReportDescriptor &desc) {
  size_t touch_points = 0;
  TouchPointConfig configs[MAX_TOUCH_POINTS] = {};
  hid::Attributes scan_time = {};
  hid::Attributes contact_count = {};
  hid::Attributes button = {};
  hid::Collection *finger_collection;
  uint32_t caps = 0;

  for (size_t i = 0; i < desc.input_count; i++) {
    const hid::ReportField field = desc.input_fields[i];

    // Process the global items if we haven't seen them before
    if (!(caps & Capabilities::CONTACT_COUNT) &&
        (field.attr.usage ==
         hid::USAGE(hid::usage::Page::kDigitizer,
                    hid::usage::Digitizer::kContactCount))) {
      contact_count = field.attr;
      caps |= Capabilities::CONTACT_COUNT;
    }
    if (!(caps & Capabilities::SCAN_TIME) &&
        (field.attr.usage == hid::USAGE(hid::usage::Page::kDigitizer,
                                        hid::usage::Digitizer::kScanTime))) {
      scan_time = field.attr;
      caps |= Capabilities::SCAN_TIME;
    }

    if (!(caps & Capabilities::BUTTON) &&
        (field.attr.usage.page == hid::usage::Page::kButton)) {
      button = field.attr;
      caps |= Capabilities::BUTTON;
    }

    // Now we move on to processing touch points, so don't process the item if
    // it's not part of a touch point collection
    if (field.col->usage != hid::USAGE(hid::usage::Page::kDigitizer,
                                       hid::usage::Digitizer::kFinger)) {
      continue;
    }

    // If our collection pointer is different than the previous collection
    // pointer, we have started a new collection and are on a new touch point
    if (field.col != finger_collection) {
      finger_collection = field.col;
      touch_points++;
    }

    if (touch_points < 1) {
      FXL_LOG(INFO)
          << "Touch descriptor: No touch points found in a collection";
      return false;
    }
    if (touch_points > MAX_TOUCH_POINTS) {
      FXL_LOG(INFO) << "Touch descriptor: Current touchscreen has "
                    << touch_points
                    << " touch points which is above hardcoded limit of "
                    << MAX_TOUCH_POINTS;
      return false;
    }
    TouchPointConfig *config = &configs[touch_points - 1];

    if (field.attr.usage == hid::USAGE(hid::usage::Page::kDigitizer,
                                       hid::usage::Digitizer::kContactID)) {
      config->contact_id = field.attr;
      config->capabilities |= Capabilities::CONTACT_ID;
      if (config->contact_id.logc_mm.max > contact_id_max_) {
        contact_id_max_ = config->contact_id.logc_mm.max;
      }
    }
    if (field.attr.usage == hid::USAGE(hid::usage::Page::kDigitizer,
                                       hid::usage::Digitizer::kTipSwitch)) {
      config->tip_switch = field.attr;
      config->capabilities |= Capabilities::TIP_SWITCH;
    }
    if (field.attr.usage == hid::USAGE(hid::usage::Page::kGenericDesktop,
                                       hid::usage::GenericDesktop::kX)) {
      config->x = field.attr;
      config->capabilities |= Capabilities::X;
    }
    if (field.attr.usage == hid::USAGE(hid::usage::Page::kGenericDesktop,
                                       hid::usage::GenericDesktop::kY)) {
      config->y = field.attr;
      config->capabilities |= Capabilities::Y;
    }
  }

  if (touch_points == 0) {
    FXL_LOG(INFO) << "Touch descriptor: Failed to find any touch points";
    return false;
  }

  // Ensure that all touch points have the same capabilities.
  for (size_t i = 1; i < touch_points; i++) {
    if (configs[i].capabilities != configs[0].capabilities) {
      FXL_LOG(INFO)
          << "Touch descriptor: Touch point capabilities are different";
      for (size_t j = 0; j < touch_points; j++) {
        FXL_LOG(INFO) << "Touch descriptor: touch_point[" << j
                      << "] = " << configs[i].capabilities;
      }
      return false;
    }
  }

  caps |= configs[0].capabilities;

  touch_points_ = touch_points;
  scan_time_ = scan_time;
  button_ = button;
  contact_count_ = contact_count;
  capabilities_ = caps;
  report_size_ = desc.input_byte_sz;
  report_id_ = desc.report_id;
  for (size_t i = 0; i < touch_points; i++) {
    configs_[i] = configs[i];
  }

  return true;
}

bool Touch::ParseReport(const uint8_t *data, size_t len, Report *report) const {
  assert(report != nullptr);

  if (len != report_size_) {
    FXL_LOG(INFO) << "Touch HID Report is not correct size, (" << len
                  << " != " << report_size_ << ")";
    return false;
  }

  // X and Y will have units of 10^-5 meters
  hid::Unit length_unit = {};
  length_unit.exp = -5;
  hid::unit::SetSystem(length_unit, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(length_unit, 1);

  size_t contact_count = 0;
  for (size_t i = 0; i < touch_points_; i++) {
    auto config = &configs_[i];

    if (config->capabilities & Capabilities::TIP_SWITCH) {
      uint8_t tip_switch;
      bool success =
          hid::ExtractUint(data, len, config->tip_switch, &tip_switch);
      if (!success || !tip_switch) {
        continue;
      }
    }
    auto contact = &report->contacts[contact_count];
    *contact = {};

    // XXX(konkers): Add 32 bit generic field extraction helpers.
    if (config->capabilities & Capabilities::CONTACT_ID) {
      if (!hid::ExtractUint(data, len, config->contact_id, &contact->id)) {
        FXL_LOG(INFO) << "Touch report: Failed to parse CONTACT_ID";
        return false;
      }
    }
    if (config->capabilities & Capabilities::X) {
      double x;
      if (!hid::ExtractAsUnit(data, len, config->x, &x)) {
        FXL_LOG(INFO) << "Touch report: Failed to parse X";
        return false;
      }
      // If this returns true, x was converted. If it returns false,
      // x is unchanged. Either way we return successfully.
      hid::unit::ConvertUnits(config->x.unit, x, length_unit, &x);
      contact->x = static_cast<int32_t>(x);
    }
    if (config->capabilities & Capabilities::Y) {
      double y;
      if (!hid::ExtractAsUnit(data, len, config->y, &y)) {
        FXL_LOG(INFO) << "Touchpad report: Failed to parse Y";
        return false;
      }
      // If this returns true, x was converted. If it returns false,
      // x is unchanged. Either way we return successfully.
      hid::unit::ConvertUnits(config->y.unit, y, length_unit, &y);
      contact->y = static_cast<int32_t>(y);
    }

    // TODO(SCN-1188): Add support for contact ellipse.

    contact_count++;
  }

  report->contact_count = contact_count;

  if (capabilities_ & Capabilities::BUTTON) {
    uint8_t button;
    if (!hid::ExtractUint(data, len, button_, &button)) {
      FXL_LOG(INFO) << "Touchpad report: Failed to parse BUTTON";
      return false;
    }
    report->button = (button == 1);
  }

  if (capabilities_ & Capabilities::SCAN_TIME) {
    // If we don't have a unit, extract the raw data
    if (scan_time_.unit.type == 0) {
      if (!hid::ExtractUint(data, len, scan_time_, &report->scan_time)) {
        return false;
      }
    } else {
      double scan_time;
      if (!hid::ExtractAsUnit(data, len, scan_time_, &scan_time)) {
        FXL_LOG(INFO) << "Touchpad report: Failed to parse SCAN_TIME";
        return false;
      }

      hid::Unit time_unit = {};
      time_unit.exp = -6;
      hid::unit::SetSystem(time_unit, hid::unit::System::si_linear);
      hid::unit::SetTimeExp(time_unit, 1);
      // If this returns true, scan_time was converted. If it returns false,
      // scan_time is unchanged. Either way we return successfully.
      hid::unit::ConvertUnits(scan_time_.unit, scan_time, time_unit,
                              &scan_time);

      report->scan_time = static_cast<uint32_t>(scan_time);
    }
  }
  return true;
}

bool Touch::SetDescriptor(Touch::Descriptor *touch_desc) {
  // X and Y will have units of 10^-5 meters
  hid::Unit length_unit = {};
  length_unit.exp = -5;
  hid::unit::SetSystem(length_unit, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(length_unit, 1);

  double val_out;

  if (hid::unit::ConvertUnits(configs_[0].x.unit, configs_[0].x.phys_mm.min,
                              length_unit, &val_out)) {
    touch_desc->x_min = static_cast<int32_t>(val_out);
  } else {
    touch_desc->x_min = configs_[0].x.phys_mm.min;
  }

  if (hid::unit::ConvertUnits(configs_[0].x.unit, configs_[0].x.phys_mm.max,
                              length_unit, &val_out)) {
    touch_desc->x_max = static_cast<int32_t>(val_out);
  } else {
    touch_desc->x_max = configs_[0].x.phys_mm.max;
  }
  touch_desc->x_resolution = 1;

  if (hid::unit::ConvertUnits(configs_[0].y.unit, configs_[0].y.phys_mm.min,
                              length_unit, &val_out)) {
    touch_desc->y_min = static_cast<int32_t>(val_out);
  } else {
    touch_desc->y_min = configs_[0].y.phys_mm.min;
  }

  if (hid::unit::ConvertUnits(configs_[0].y.unit, configs_[0].y.phys_mm.max,
                              length_unit, &val_out)) {
    touch_desc->y_max = static_cast<int32_t>(val_out);
  } else {
    touch_desc->y_max = configs_[0].y.phys_mm.max;
  }

  touch_desc->y_resolution = 1;

  touch_desc->max_finger_id = contact_id_max_;

  return true;
}

}  // namespace ui_input
