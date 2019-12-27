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

ParseResult Keyboard::ParseInputReportDescriptor(
    const hid::ReportDescriptor& hid_report_descriptor) {
  // Use a set to make it easy to create a list of sorted and unique keys.
  std::set<uint32_t> key_values;
  std::array<hid::ReportField, fuchsia_input_report::KEYBOARD_MAX_NUM_KEYS> key_fields;
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
      if (num_keys == fuchsia_input_report::KEYBOARD_MAX_NUM_KEYS) {
        return ParseResult::kParseTooManyItems;
      }
    }
  }

  if (key_values.size() >= fuchsia_input_report::KEYBOARD_MAX_NUM_KEYS) {
    return ParseResult::kParseTooManyItems;
  }

  // No error, write to class members.
  descriptor_.input = KeyboardInputDescriptor();
  size_t i = 0;
  // Convert each of the HID keys to fuchsia keys.
  for (uint32_t key : key_values) {
    std::optional<fuchsia::ui::input2::Key> fuchsia_key =
        key_util::hid_key_to_fuchsia_key(hid::USAGE(hid::usage::Page::kKeyboardKeypad, key));
    if (fuchsia_key) {
      // Cast the key enum from HLCPP to LLCPP. We are guaranteed that this will be equivalent.
      descriptor_.input->keys[i++] = static_cast<llcpp::fuchsia::ui::input2::Key>(*fuchsia_key);
    }
  }
  descriptor_.input->num_keys = i;

  num_keys_ = num_keys;
  key_fields_ = key_fields;

  input_report_size_ = hid_report_descriptor.input_byte_sz;
  input_report_id_ = hid_report_descriptor.report_id;

  return kParseOk;
}

ParseResult Keyboard::ParseOutputReportDescriptor(
    const hid::ReportDescriptor& hid_report_descriptor) {
  std::array<hid::ReportField, ::llcpp::fuchsia::input::report::KEYBOARD_MAX_NUM_LEDS> led_fields;
  size_t num_leds = 0;

  for (size_t i = 0; i < hid_report_descriptor.output_count; i++) {
    const hid::ReportField& field = hid_report_descriptor.output_fields[i];
    if (field.attr.usage.page == hid::usage::Page::kLEDs) {
      if (num_leds == ::llcpp::fuchsia::input::report::KEYBOARD_MAX_NUM_LEDS) {
        return ParseResult::kParseTooManyItems;
      }
      led_fields[num_leds++] = field;
    }
  }

  if (num_leds == 0) {
    return kParseOk;
  }

  // No errors, write to class memebers.
  descriptor_.output = KeyboardOutputDescriptor();
  for (size_t i = 0; i < num_leds; i++) {
    zx_status_t status =
        HidLedUsageToLlcppLedType(static_cast<hid::usage::LEDs>(led_fields[i].attr.usage.usage),
                                  &descriptor_.output->leds[i]);
    if (status != ZX_OK) {
      return kParseBadReport;
    }
  }
  descriptor_.output->num_leds = num_leds;
  num_leds_ = num_leds;
  led_fields_ = led_fields;

  output_report_id_ = hid_report_descriptor.report_id;
  output_report_size_ = hid_report_descriptor.output_byte_sz;

  return kParseOk;
}

ParseResult Keyboard::ParseReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor) {
  ParseResult res = ParseInputReportDescriptor(hid_report_descriptor);
  if (res != kParseOk) {
    return res;
  }
  return ParseOutputReportDescriptor(hid_report_descriptor);
};

ReportDescriptor Keyboard::GetDescriptor() {
  ReportDescriptor report_descriptor = {};
  report_descriptor.descriptor = descriptor_;
  return report_descriptor;
}

ParseResult Keyboard::ParseInputReport(const uint8_t* data, size_t len, InputReport* report) {
  KeyboardInputReport keyboard_report = {};
  size_t key_index = 0;
  if (len != input_report_size_) {
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

    // Get the HID key.
    uint32_t hid_key;
    if (field.flags & hid::FieldTypeFlags::kArray) {
      if (val_out == HID_USAGE_KEY_ERROR_ROLLOVER) {
        return kParseBadReport;
      }
      hid_key = val_out;
    } else {
      hid_key = field.attr.usage.usage;
    }

    // Convert to fuchsia key.
    std::optional<fuchsia::ui::input2::Key> fuchsia_key =
        key_util::hid_key_to_fuchsia_key(hid::USAGE(hid::usage::Page::kKeyboardKeypad, hid_key));
    if (fuchsia_key) {
      // Cast the key enum from HLCPP to LLCPP. We are guaranteed that this will be equivalent.
      keyboard_report.pressed_keys[key_index++] =
          static_cast<llcpp::fuchsia::ui::input2::Key>(*fuchsia_key);
    }
  }

  keyboard_report.num_pressed_keys = key_index;

  // Now that we can't fail, set the real report.
  report->report = keyboard_report;

  return kParseOk;
}

ParseResult Keyboard::SetOutputReport(const fuchsia_input_report::OutputReport* report,
                                      uint8_t* data, size_t data_size, size_t* data_out_size) {
  if (!report->has_keyboard()) {
    return kParseNotImplemented;
  }
  if (!report->keyboard().has_enabled_leds()) {
    return kParseNotImplemented;
  }
  if (data_size < output_report_size_) {
    return kParseNoMemory;
  }
  for (size_t i = 0; i < data_size; i++) {
    data[i] = 0;
  }
  // Go through each enabled LED and set its report field to enabled.
  for (fuchsia_input_report::LedType led : report->keyboard().enabled_leds()) {
    bool found = false;
    for (size_t i = 0; i < num_leds_; i++) {
      hid::ReportField& hid_led = led_fields_[i];
      // Convert the usage to LedType.
      fuchsia_input_report::LedType hid_led_type;
      zx_status_t status = HidLedUsageToLlcppLedType(
          static_cast<hid::usage::LEDs>(hid_led.attr.usage.usage), &hid_led_type);
      if (status != ZX_OK) {
        return kParseBadReport;
      }
      if (hid_led_type == led) {
        found = true;
        if (!InsertAsUnitType(data, data_size, hid_led.attr, 1)) {
          return kParseBadReport;
        }
        break;
      }
    }
    if (!found) {
      return kParseItemNotFound;
    }
  }
  *data_out_size = output_report_size_;
  return kParseOk;
}

}  // namespace hid_input_report
