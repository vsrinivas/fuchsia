// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/hid-input-report/keyboard.h"

#include <stdint.h>

#include <set>

#include <fbl/span.h>
#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>
#include <hid/usages.h>

#include "src/ui/lib/hid-input-report/device.h"
#include "src/ui/lib/key_util/key_util.h"

namespace hid_input_report {

ParseResult Keyboard::ParseReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor) {
  // Use a set to make it easy to create a list of sorted and unique keys.
  std::set<uint32_t> key_values;
  std::array<hid::ReportField, ::llcpp::fuchsia::input::report::KEYBOARD_MAX_NUM_KEYS> key_fields;
  size_t num_keys = 0;

  for (size_t i = 0; i < hid_report_descriptor.input_count; i++) {
    const hid::ReportField& field = hid_report_descriptor.input_fields[i];

    if (field.attr.usage.page == hid::usage::Page::kKeyboardKeypad) {
      if (field.flags & hid::FieldTypeFlags::kArray) {
        for (uint8_t key = static_cast<uint8_t>(field.attr.logc_mm.min);
             key < static_cast<uint8_t>(field.attr.logc_mm.max); key++) {
          key_values.insert(key);
        }
      } else {
        key_values.insert(field.attr.usage.usage);
      }
      key_fields[num_keys++] = field;
      if (num_keys == ::llcpp::fuchsia::input::report::KEYBOARD_MAX_NUM_KEYS) {
        return ParseResult::kParseTooManyItems;
      }
    }
  }

  if (key_values.size() >= ::llcpp::fuchsia::input::report::KEYBOARD_MAX_NUM_KEYS) {
    return ParseResult::kParseTooManyItems;
  }

  // No error, write to class members.
  size_t i = 0;
  // Convert each of the HID keys to fuchsia keys.
  for (uint32_t key : key_values) {
    std::optional<fuchsia::ui::input2::Key> fuchsia_key =
        key_util::hid_key_to_fuchsia_key(hid::USAGE(hid::usage::Page::kKeyboardKeypad, key));
    if (fuchsia_key) {
      // Cast the key enum from HLCPP to LLCPP. We are guaranteed that this will be equivalent.
      descriptor_.keys[i++] = static_cast<llcpp::fuchsia::ui::input2::Key>(*fuchsia_key);
    }
  }
  descriptor_.num_keys = i;

  num_keys_ = num_keys;
  key_fields_ = key_fields;

  report_size_ = hid_report_descriptor.input_byte_sz;
  report_id_ = hid_report_descriptor.report_id;

  return kParseOk;
}

ReportDescriptor Keyboard::GetDescriptor() {
  ReportDescriptor report_descriptor = {};
  report_descriptor.descriptor = descriptor_;
  return report_descriptor;
}

ParseResult Keyboard::ParseReport(const uint8_t* data, size_t len, Report* report) {
  KeyboardReport keyboard_report = {};
  size_t key_index = 0;
  if (len != report_size_) {
    return kParseReportSizeMismatch;
  }

  for (hid::ReportField& field : fbl::Span(key_fields_.data(), num_keys_)) {
    double val_out_double;
    if (!ExtractAsUnitType(data, len, field.attr, &val_out_double)) {
      continue;
    }

    uint32_t val_out = static_cast<uint32_t>(val_out_double);
    if (val_out == 0) {
      continue;
    }

    if (field.flags & hid::FieldTypeFlags::kArray) {
      if (val_out == HID_USAGE_KEY_ERROR_ROLLOVER) {
        return kParseBadReport;
      }
      keyboard_report.pressed_keys[key_index++] = val_out;
    } else {
      keyboard_report.pressed_keys[key_index++] = field.attr.usage.usage;
    }
  }
  keyboard_report.num_pressed_keys = key_index;

  // Now that we can't fail, set the real report.
  report->report = keyboard_report;

  return kParseOk;
}

}  // namespace hid_input_report
