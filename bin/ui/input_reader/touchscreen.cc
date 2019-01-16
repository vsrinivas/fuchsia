// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/touchscreen.h"

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/usages.h>

#include <stdint.h>
#include <stdio.h>
#include <vector>

#include "lib/fxl/logging.h"

namespace mozart {
bool Touchscreen::ParseTouchscreenDescriptor(
    const hid::ReportDescriptor *desc) {
  size_t touch_points = 0;
  TouchPointConfig configs[MAX_TOUCH_POINTS] = {};
  hid::Attributes scan_time = {};
  hid::Attributes contact_count = {};
  hid::Collection *finger_collection;
  uint32_t caps = 0;

  for (size_t i = 0; i < desc->count; i++) {
    const hid::ReportField field = desc->first_field[i];

    // Process the global items
    if (field.attr.usage == hid::USAGE(hid::usage::Page::kDigitizer,
                                       hid::usage::Digitizer::kContactCount)) {
      contact_count = field.attr;
      caps |= Capabilities::CONTACT_COUNT;
    }
    if (field.attr.usage == hid::USAGE(hid::usage::Page::kDigitizer,
                                       hid::usage::Digitizer::kScanTime)) {
      scan_time = field.attr;
      caps |= Capabilities::SCAN_TIME;
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
      FXL_LOG(ERROR)
          << "Touchscreen descriptor: No touch points found in a collection";
      return false;
    }
    if (touch_points > MAX_TOUCH_POINTS) {
      FXL_LOG(ERROR) << "Touchscreen descriptor: Current touchscreen has "
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
    FXL_LOG(ERROR) << "Touchscreen descriptor: Failed to find any touch points";
    return false;
  }

  // Ensure that all touch points have the same capabilities.
  for (size_t i = 1; i < touch_points; i++) {
    if (configs[i].capabilities != configs[0].capabilities) {
      FXL_LOG(ERROR)
          << "Touchscreen descriptor: Touch point capabilities are different";
      for (size_t j = 0; j < touch_points; j++) {
        FXL_LOG(ERROR) << "Touchscreen descriptor: touch_point[" << j
                       << "] = " << configs[i].capabilities;
      }
      return false;
    }
  }

  caps |= configs[0].capabilities;

  touch_points_ = touch_points;
  scan_time_ = scan_time;
  contact_count_ = contact_count;
  capabilities_ = caps;
  report_size_ = desc->byte_sz;
  report_id_ = desc->report_id;
  for (size_t i = 0; i < touch_points; i++) {
    configs_[i] = configs[i];
  }

  return true;
}

bool Touchscreen::ParseReport(const uint8_t *data, size_t len,
                              Report *report) const {
  assert(report != nullptr);

  hid::Report hid_report = {data, len};
  if (len != report_size_) {
    FXL_LOG(ERROR) << "Touchscreen HID Report is not correct size, (" << len
                   << " != " << report_size_ << ")";
    return false;
  }

  size_t contact_count = 0;
  for (size_t i = 0; i < touch_points_; i++) {
    auto config = &configs_[i];

    if (config->capabilities & Capabilities::TIP_SWITCH) {
      uint8_t tip_switch;
      bool success =
          hid::ExtractUint(hid_report, config->tip_switch, &tip_switch);
      if (!success || !tip_switch) {
        continue;
      }
    }
    auto contact = &report->contacts[contact_count];
    *contact = {};

    // XXX(konkers): Add 32 bit generic field extraction helpers.
    if (config->capabilities & Capabilities::CONTACT_ID) {
      if (!hid::ExtractUint(hid_report, config->contact_id, &contact->id)) {
        FXL_LOG(ERROR) << "Touchscreen report: Failed to parse CONTACT_ID";
        return false;
      }
    }
    if (config->capabilities & Capabilities::X) {
      uint16_t x;
      if (!hid::ExtractUint(hid_report, config->x, &x)) {
        FXL_LOG(ERROR) << "Touchscreen report: Failed to parse X";
        return false;
      }
      contact->x = static_cast<int32_t>(x);
    }
    if (config->capabilities & Capabilities::Y) {
      uint16_t y;
      if (!hid::ExtractUint(hid_report, config->y, &y)) {
        FXL_LOG(ERROR) << "Touchpad report: Failed to parse Y";
        return false;
      }
      contact->y = static_cast<int32_t>(y);
    }

    // TODO(SCN-1188): Add support for contact ellipse.

    contact_count++;
  }

  report->contact_count = contact_count;

  if (capabilities_ & Capabilities::SCAN_TIME) {
    uint32_t scan_time;
    if (!hid::ExtractUint(hid_report, scan_time_, &scan_time)) {
      FXL_LOG(ERROR) << "Touchpad report: Failed to parse SCAN_TIME";
      return false;
    }

    // TODO(ZX-3287) Convert scan time units to microseconds
    report->scan_time = scan_time;
  }

  return true;
}

bool Touchscreen::SetDescriptor(Touchscreen::Descriptor *touch_desc) {
  touch_desc->x_min = configs_[0].x.logc_mm.min;
  touch_desc->x_max = configs_[0].x.logc_mm.max;
  touch_desc->x_resolution = 1;

  touch_desc->y_min = configs_[0].y.logc_mm.min;
  touch_desc->y_max = configs_[0].y.logc_mm.max;
  touch_desc->y_resolution = 1;

  touch_desc->max_finger_id = contact_id_max_;

  return true;
}

}  // namespace mozart
