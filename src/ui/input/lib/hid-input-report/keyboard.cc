// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/keyboard.h"

#include <stdint.h>

#include <fbl/span.h>
#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>
#include <hid/usages.h>

#include "src/ui/input/lib/hid-input-report/device.h"
#include "src/ui/lib/key_util/key_util.h"

namespace hid_input_report {

namespace {

void InsertFuchsiaKey3(uint32_t hid_usage, uint32_t hid_key,
                       std::set<fuchsia_input::wire::Key>* key_values) {
  std::optional<fuchsia::input::Key> fuchsia_key3 =
      key_util::hid_key_to_fuchsia_key3(hid::USAGE(hid_usage, hid_key));
  if (fuchsia_key3) {
    // Cast the key enum from HLCPP to LLCPP. We are guaranteed that this will be
    // equivalent.
    key_values->insert(static_cast<fuchsia_input::wire::Key>(*fuchsia_key3));
  }
}

}  // namespace

ParseResult Keyboard::ParseInputReportDescriptor(
    const hid::ReportDescriptor& hid_report_descriptor) {
  // Use a set to make it easy to create a list of sorted and unique keys.
  std::set<fuchsia_input::wire::Key> key_3_values;
  std::array<hid::ReportField, fuchsia_input_report::wire::kKeyboardMaxNumKeys> key_fields;
  size_t num_key_fields = 0;

  for (size_t i = 0; i < hid_report_descriptor.input_count; i++) {
    const hid::ReportField& field = hid_report_descriptor.input_fields[i];

    if (field.attr.usage.page == hid::usage::Page::kKeyboardKeypad) {
      if (field.flags & hid::FieldTypeFlags::kArray) {
        for (uint8_t key = static_cast<uint8_t>(field.attr.logc_mm.min);
             key < static_cast<uint8_t>(field.attr.logc_mm.max); key++) {
          InsertFuchsiaKey3(field.attr.usage.page, key, &key_3_values);
        }
      } else {
        InsertFuchsiaKey3(field.attr.usage.page, field.attr.usage.usage, &key_3_values);
      }

      key_fields[num_key_fields++] = field;
      if (num_key_fields == fuchsia_input_report::wire::kKeyboardMaxNumKeys) {
        return ParseResult::kTooManyItems;
      }
    }
  }

  if (key_3_values_.size() >= fuchsia_input_report::wire::kKeyboardMaxNumKeys) {
    return ParseResult::kTooManyItems;
  }

  // No error, write to class members.
  key_3_values_ = std::move(key_3_values);

  num_key_fields_ = num_key_fields;
  key_fields_ = key_fields;

  input_report_size_ = hid_report_descriptor.input_byte_sz;
  input_report_id_ = hid_report_descriptor.report_id;

  return ParseResult::kOk;
}

ParseResult Keyboard::ParseOutputReportDescriptor(
    const hid::ReportDescriptor& hid_report_descriptor) {
  std::array<hid::ReportField, fuchsia_input_report::wire::kKeyboardMaxNumLeds> led_fields;
  size_t num_leds = 0;

  for (size_t i = 0; i < hid_report_descriptor.output_count; i++) {
    const hid::ReportField& field = hid_report_descriptor.output_fields[i];
    if (field.attr.usage.page == hid::usage::Page::kLEDs) {
      if (num_leds == fuchsia_input_report::wire::kKeyboardMaxNumLeds) {
        return ParseResult::kTooManyItems;
      }
      led_fields[num_leds++] = field;
    }
  }

  if (num_leds == 0) {
    return ParseResult::kOk;
  }

  // No errors, write to class members.
  num_leds_ = num_leds;
  led_fields_ = led_fields;

  output_report_id_ = hid_report_descriptor.report_id;
  output_report_size_ = hid_report_descriptor.output_byte_sz;

  return ParseResult::kOk;
}

ParseResult Keyboard::ParseReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor) {
  ParseResult res = ParseInputReportDescriptor(hid_report_descriptor);
  if (res != ParseResult::kOk) {
    return res;
  }
  return ParseOutputReportDescriptor(hid_report_descriptor);
};

ParseResult Keyboard::CreateDescriptor(fidl::AnyArena& allocator,
                                       fuchsia_input_report::wire::DeviceDescriptor& descriptor) {
  fuchsia_input_report::wire::KeyboardDescriptor keyboard(allocator);

  // Input Descriptor parsing.
  if (input_report_size_ > 0) {
    fuchsia_input_report::wire::KeyboardInputDescriptor keyboard_input(allocator);

    size_t keys_3_index = 0;
    fidl::VectorView<fuchsia_input::wire::Key> keys_3(allocator, key_3_values_.size());
    for (auto& key : key_3_values_) {
      keys_3[keys_3_index++] = key;
    }

    keyboard_input.set_keys3(allocator, std::move(keys_3));
    keyboard.set_input(allocator, std::move(keyboard_input));
  }

  // Output Descriptor parsing.
  if (output_report_size_ > 0) {
    fuchsia_input_report::wire::KeyboardOutputDescriptor keyboard_output(allocator);

    size_t leds_index = 0;
    fidl::VectorView<fuchsia_input_report::wire::LedType> leds(allocator, num_leds_);
    for (hid::ReportField& field : fbl::Span(led_fields_.data(), num_leds_)) {
      zx_status_t status = HidLedUsageToLlcppLedType(
          static_cast<hid::usage::LEDs>(field.attr.usage.usage), &leds[leds_index++]);
      if (status != ZX_OK) {
        return ParseResult::kBadReport;
      }
    }

    keyboard_output.set_leds(allocator, std::move(leds));
    keyboard.set_output(allocator, std::move(keyboard_output));
  }

  descriptor.set_keyboard(allocator, std::move(keyboard));
  return ParseResult::kOk;
}

ParseResult Keyboard::ParseInputReport(const uint8_t* data, size_t len, fidl::AnyArena& allocator,
                                       fuchsia_input_report::wire::InputReport& input_report) {
  if (len != input_report_size_) {
    return ParseResult::kReportSizeMismatch;
  }

  fuchsia_input_report::wire::KeyboardInputReport keyboard_report(allocator);

  size_t num_pressed_keys_3 = 0;
  std::array<fuchsia_input::wire::Key, fuchsia_input_report::wire::kKeyboardMaxNumKeys>
      pressed_keys_3;
  for (hid::ReportField& field : fbl::Span(key_fields_.data(), num_key_fields_)) {
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
        return ParseResult::kBadReport;
      }
      hid_key = val_out;
    } else {
      hid_key = field.attr.usage.usage;
    }

    // Convert to fuchsia key.
    auto fuchsia_key_3 =
        key_util::hid_key_to_fuchsia_key3(hid::USAGE(hid::usage::Page::kKeyboardKeypad, hid_key));
    if (fuchsia_key_3) {
      // Cast the key enum from HLCPP to LLCPP. We are guaranteed that this will be equivalent.
      pressed_keys_3[num_pressed_keys_3++] = static_cast<fuchsia_input::wire::Key>(*fuchsia_key_3);
    }
  }

  fidl::VectorView<fuchsia_input::wire::Key> fidl_pressed_keys_3(allocator, num_pressed_keys_3);
  for (size_t i = 0; i < num_pressed_keys_3; i++) {
    fidl_pressed_keys_3[i] = pressed_keys_3[i];
  }

  keyboard_report.set_pressed_keys3(allocator, std::move(fidl_pressed_keys_3));

  input_report.set_keyboard(allocator, std::move(keyboard_report));
  return ParseResult::kOk;
}

ParseResult Keyboard::SetOutputReport(const fuchsia_input_report::wire::OutputReport* report,
                                      uint8_t* data, size_t data_size, size_t* data_out_size) {
  if (!report->has_keyboard()) {
    return ParseResult::kNotImplemented;
  }
  if (!report->keyboard().has_enabled_leds()) {
    return ParseResult::kNotImplemented;
  }
  if (data_size < output_report_size_) {
    return ParseResult::kNoMemory;
  }
  for (size_t i = 0; i < data_size; i++) {
    data[i] = 0;
  }
  // Go through each enabled LED and set its report field to enabled.
  for (fuchsia_input_report::wire::LedType led : report->keyboard().enabled_leds()) {
    bool found = false;
    for (size_t i = 0; i < num_leds_; i++) {
      hid::ReportField& hid_led = led_fields_[i];
      // Convert the usage to LedType.
      fuchsia_input_report::wire::LedType hid_led_type;
      zx_status_t status = HidLedUsageToLlcppLedType(
          static_cast<hid::usage::LEDs>(hid_led.attr.usage.usage), &hid_led_type);
      if (status != ZX_OK) {
        return ParseResult::kBadReport;
      }
      if (hid_led_type == led) {
        found = true;
        if (!InsertAsUnitType(data, data_size, hid_led.attr, 1)) {
          return ParseResult::kBadReport;
        }
        break;
      }
    }
    if (!found) {
      return ParseResult::kItemNotFound;
    }
  }
  *data_out_size = output_report_size_;
  return ParseResult::kOk;
}

}  // namespace hid_input_report
