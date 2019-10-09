
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/keyboard.h"

#include <stdint.h>
#include <stdio.h>

#include <set>
#include <vector>

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>
#include <hid/usages.h>
#include <src/lib/fxl/logging.h>

#include "garnet/bin/ui/input_reader/device.h"

namespace ui_input {

bool Keyboard::ParseReportDescriptor(const hid::ReportDescriptor& report_descriptor,
                                     Descriptor* device_descriptor) {
  std::set<uint32_t> key_values;

  auto keyboard = fuchsia::ui::input::KeyboardDescriptor::New();
  for (size_t i = 0; i < report_descriptor.input_count; i++) {
    const hid::ReportField& field = report_descriptor.input_fields[i];

    if (field.attr.usage.page == hid::usage::Page::kKeyboardKeypad) {
      if (field.flags & hid::FieldTypeFlags::kArray) {
        for (uint8_t key = static_cast<uint8_t>(field.attr.logc_mm.min);
             key < static_cast<uint8_t>(field.attr.logc_mm.max); key++) {
          key_values.insert(key);
        }
      } else {
        key_values.insert(field.attr.usage.usage);
      }
      key_fields_.push_back(field);
    }
  }

  keyboard->keys = std::vector<uint32_t>(key_values.begin(), key_values.end());
  device_descriptor->has_keyboard = true;
  device_descriptor->keyboard_descriptor = std::move(keyboard);
  return true;
}

bool Keyboard::ParseReport(const uint8_t* data, size_t len,
                           fuchsia::ui::input::InputReport* report) {
  FXL_CHECK(report);
  FXL_CHECK(report->keyboard);

  report->keyboard->pressed_keys.resize(0);

  for (size_t i = 0; i < key_fields_.size(); i++) {
    hid::ReportField& field = key_fields_[i];

    double val_out_double;
    if (!ExtractAsUnitType(data, len, field.attr, &val_out_double)) {
      return false;
    }

    uint32_t val_out = static_cast<uint32_t>(val_out_double);
    if (val_out == 0) {
      continue;
    }

    if (field.flags & hid::FieldTypeFlags::kArray) {
      if (val_out == HID_USAGE_KEY_ERROR_ROLLOVER) {
        FXL_VLOG(2) << "hid: input_report: keyboard rollover error";
        return false;
      }
      report->keyboard->pressed_keys.push_back(val_out);
    } else {
      report->keyboard->pressed_keys.push_back(field.attr.usage.usage);
    }
  }

  return true;
}

}  // namespace ui_input
