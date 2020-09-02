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

void InsertFuchsiaKey(uint32_t hid_usage, uint32_t hid_key,
                      std::set<::llcpp::fuchsia::ui::input2::Key>* key_values) {
  std::optional<fuchsia::ui::input2::Key> fuchsia_key =
      key_util::hid_key_to_fuchsia_key(hid::USAGE(hid_usage, hid_key));
  if (fuchsia_key) {
    // Cast the key enum from HLCPP to LLCPP. We are guaranteed that this will be
    // equivalent.
    key_values->insert(static_cast<llcpp::fuchsia::ui::input2::Key>(*fuchsia_key));
  }
}

void InsertFuchsiaKey3(uint32_t hid_usage, uint32_t hid_key,
                       std::set<::llcpp::fuchsia::input::Key>* key_values) {
  std::optional<fuchsia::input::Key> fuchsia_key3 =
      key_util::hid_key_to_fuchsia_key3(hid::USAGE(hid_usage, hid_key));
  if (fuchsia_key3) {
    // Cast the key enum from HLCPP to LLCPP. We are guaranteed that this will be
    // equivalent.
    key_values->insert(static_cast<llcpp::fuchsia::input::Key>(*fuchsia_key3));
  }
}

}  // namespace

ParseResult Keyboard::ParseInputReportDescriptor(
    const hid::ReportDescriptor& hid_report_descriptor) {
  // Use a set to make it easy to create a list of sorted and unique keys.
  std::set<::llcpp::fuchsia::ui::input2::Key> key_values;
  std::set<::llcpp::fuchsia::input::Key> key_3_values;
  std::array<hid::ReportField, fuchsia_input_report::KEYBOARD_MAX_NUM_KEYS> key_fields;
  size_t num_key_fields = 0;

  for (size_t i = 0; i < hid_report_descriptor.input_count; i++) {
    const hid::ReportField& field = hid_report_descriptor.input_fields[i];

    if (field.attr.usage.page == hid::usage::Page::kKeyboardKeypad) {
      if (field.flags & hid::FieldTypeFlags::kArray) {
        for (uint8_t key = static_cast<uint8_t>(field.attr.logc_mm.min);
             key < static_cast<uint8_t>(field.attr.logc_mm.max); key++) {
          InsertFuchsiaKey(field.attr.usage.page, key, &key_values);
          InsertFuchsiaKey3(field.attr.usage.page, key, &key_3_values);
        }
      } else {
        InsertFuchsiaKey(field.attr.usage.page, field.attr.usage.usage, &key_values);
        InsertFuchsiaKey3(field.attr.usage.page, field.attr.usage.usage, &key_3_values);
      }

      key_fields[num_key_fields++] = field;
      if (num_key_fields == fuchsia_input_report::KEYBOARD_MAX_NUM_KEYS) {
        return ParseResult::kTooManyItems;
      }
    }
  }

  if (key_values.size() >= fuchsia_input_report::KEYBOARD_MAX_NUM_KEYS) {
    return ParseResult::kTooManyItems;
  }

  // No error, write to class members.
  key_values_ = std::move(key_values);
  key_3_values_ = std::move(key_3_values);

  num_key_fields_ = num_key_fields;
  key_fields_ = key_fields;

  input_report_size_ = hid_report_descriptor.input_byte_sz;
  input_report_id_ = hid_report_descriptor.report_id;

  return ParseResult::kOk;
}

ParseResult Keyboard::ParseOutputReportDescriptor(
    const hid::ReportDescriptor& hid_report_descriptor) {
  std::array<hid::ReportField, ::llcpp::fuchsia::input::report::KEYBOARD_MAX_NUM_LEDS> led_fields;
  size_t num_leds = 0;

  for (size_t i = 0; i < hid_report_descriptor.output_count; i++) {
    const hid::ReportField& field = hid_report_descriptor.output_fields[i];
    if (field.attr.usage.page == hid::usage::Page::kLEDs) {
      if (num_leds == ::llcpp::fuchsia::input::report::KEYBOARD_MAX_NUM_LEDS) {
        return ParseResult::kTooManyItems;
      }
      led_fields[num_leds++] = field;
    }
  }

  if (num_leds == 0) {
    return ParseResult::kOk;
  }

  // No errors, write to class memebers.
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

ParseResult Keyboard::CreateDescriptor(
    fidl::Allocator* allocator, fuchsia_input_report::DeviceDescriptor::Builder* descriptor) {
  auto keyboard = fuchsia_input_report::KeyboardDescriptor::Builder(
      allocator->make<fuchsia_input_report::KeyboardDescriptor::Frame>());

  // Input Descriptor parsing.
  if (input_report_size_ > 0) {
    auto keyboard_input = fuchsia_input_report::KeyboardInputDescriptor::Builder(
        allocator->make<fuchsia_input_report::KeyboardInputDescriptor::Frame>());

    size_t keys_index = 0;
    auto keys = allocator->make<::llcpp::fuchsia::ui::input2::Key[]>(key_values_.size());
    for (auto& key : key_values_) {
      keys[keys_index++] = key;
    }
    size_t keys_3_index = 0;
    auto keys_3 = allocator->make<::llcpp::fuchsia::input::Key[]>(key_3_values_.size());
    for (auto& key : key_3_values_) {
      keys_3[keys_3_index++] = key;
    }

    keyboard_input.set_keys(allocator->make<fidl::VectorView<::llcpp::fuchsia::ui::input2::Key>>(
        std::move(keys), key_values_.size()));
    keyboard_input.set_keys3(allocator->make<fidl::VectorView<::llcpp::fuchsia::input::Key>>(
        std::move(keys_3), key_3_values_.size()));
    keyboard.set_input(
        allocator->make<fuchsia_input_report::KeyboardInputDescriptor>(keyboard_input.build()));
  }

  // Output Descriptor parsing.
  if (output_report_size_ > 0) {
    auto keyboard_output = fuchsia_input_report::KeyboardOutputDescriptor::Builder(
        allocator->make<fuchsia_input_report::KeyboardOutputDescriptor::Frame>());

    size_t leds_index = 0;
    auto leds = allocator->make<fuchsia_input_report::LedType[]>(num_leds_);
    for (hid::ReportField& field : fbl::Span(led_fields_.data(), num_leds_)) {
      zx_status_t status = HidLedUsageToLlcppLedType(
          static_cast<hid::usage::LEDs>(field.attr.usage.usage), &leds[leds_index++]);
      if (status != ZX_OK) {
        return ParseResult::kBadReport;
      }
    }

    auto leds_view = allocator->make<fidl::VectorView<fuchsia_input_report::LedType>>(
        std::move(leds), num_leds_);
    keyboard_output.set_leds(std::move(leds_view));
    keyboard.set_output(
        allocator->make<fuchsia_input_report::KeyboardOutputDescriptor>(keyboard_output.build()));
  }

  descriptor->set_keyboard(
      allocator->make<fuchsia_input_report::KeyboardDescriptor>(keyboard.build()));
  return ParseResult::kOk;
}

ParseResult Keyboard::ParseInputReport(const uint8_t* data, size_t len, fidl::Allocator* allocator,
                                       fuchsia_input_report::InputReport::Builder* report) {
  if (len != input_report_size_) {
    return ParseResult::kReportSizeMismatch;
  }

  auto keyboard_report = fuchsia_input_report::KeyboardInputReport::Builder(
      allocator->make<fuchsia_input_report::KeyboardInputReport::Frame>());

  size_t num_pressed_keys = 0;
  size_t num_pressed_keys_3 = 0;
  std::array<::llcpp::fuchsia::ui::input2::Key, fuchsia_input_report::KEYBOARD_MAX_NUM_KEYS>
      pressed_keys;
  std::array<::llcpp::fuchsia::input::Key, fuchsia_input_report::KEYBOARD_MAX_NUM_KEYS>
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
    auto fuchsia_key =
        key_util::hid_key_to_fuchsia_key(hid::USAGE(hid::usage::Page::kKeyboardKeypad, hid_key));
    if (fuchsia_key) {
      // Cast the key enum from HLCPP to LLCPP. We are guaranteed that this will be equivalent.
      pressed_keys[num_pressed_keys++] = static_cast<llcpp::fuchsia::ui::input2::Key>(*fuchsia_key);
    }
    auto fuchsia_key_3 =
        key_util::hid_key_to_fuchsia_key3(hid::USAGE(hid::usage::Page::kKeyboardKeypad, hid_key));
    if (fuchsia_key_3) {
      pressed_keys_3[num_pressed_keys_3++] =
          static_cast<llcpp::fuchsia::input::Key>(*fuchsia_key_3);
    }
  }

  auto fidl_pressed_keys = allocator->make<::llcpp::fuchsia::ui::input2::Key[]>(num_pressed_keys);
  for (size_t i = 0; i < num_pressed_keys; i++) {
    fidl_pressed_keys[i] = pressed_keys[i];
  }

  auto fidl_pressed_keys_3 = allocator->make<::llcpp::fuchsia::input::Key[]>(num_pressed_keys_3);
  for (size_t i = 0; i < num_pressed_keys_3; i++) {
    fidl_pressed_keys_3[i] = pressed_keys_3[i];
  }

  keyboard_report.set_pressed_keys(
      allocator->make<fidl::VectorView<::llcpp::fuchsia::ui::input2::Key>>(
          std::move(fidl_pressed_keys), num_pressed_keys));
  keyboard_report.set_pressed_keys3(allocator->make<fidl::VectorView<::llcpp::fuchsia::input::Key>>(
      std::move(fidl_pressed_keys_3), num_pressed_keys_3));

  report->set_keyboard(
      allocator->make<fuchsia_input_report::KeyboardInputReport>(keyboard_report.build()));
  return ParseResult::kOk;
}

ParseResult Keyboard::SetOutputReport(const fuchsia_input_report::OutputReport* report,
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
  for (fuchsia_input_report::LedType led : report->keyboard().enabled_leds()) {
    bool found = false;
    for (size_t i = 0; i < num_leds_; i++) {
      hid::ReportField& hid_led = led_fields_[i];
      // Convert the usage to LedType.
      fuchsia_input_report::LedType hid_led_type;
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
