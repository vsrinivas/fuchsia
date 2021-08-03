// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/consumer_control.h"

#include <stdint.h>

#include <set>

#include <fbl/span.h>
#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>
#include <hid/usages.h>

#include "src/ui/input/lib/hid-input-report/device.h"

namespace hid_input_report {
using fuchsia_input_report::wire::ConsumerControlButton;

namespace {

std::optional<fuchsia_input_report::wire::ConsumerControlButton> HidToConsumerControlButton(
    hid::Usage usage) {
  struct {
    hid::Usage usage;
    ConsumerControlButton button;
  } usage_to_button[] = {
      {hid::USAGE(hid::usage::Page::kConsumer, hid::usage::Consumer::kVolumeUp),
       ConsumerControlButton::kVolumeUp},
      {hid::USAGE(hid::usage::Page::kConsumer, hid::usage::Consumer::kVolumeDown),
       ConsumerControlButton::kVolumeDown},
      {hid::USAGE(hid::usage::Page::kConsumer, hid::usage::Consumer::kReset),
       ConsumerControlButton::kReboot},
      {hid::USAGE(hid::usage::Page::kConsumer, hid::usage::Consumer::kCameraAccessDisabled),
       ConsumerControlButton::kCameraDisable},
      {hid::USAGE(hid::usage::Page::kTelephony, hid::usage::Telephony::kPhoneMute),
       ConsumerControlButton::kMicMute},
  };

  for (auto& map : usage_to_button) {
    if (map.usage == usage) {
      return map.button;
    }
  }

  return std::nullopt;
}

}  // namespace

ParseResult ConsumerControl::ParseInputReportDescriptor(
    const hid::ReportDescriptor& hid_report_descriptor) {
  std::array<hid::ReportField, fuchsia_input_report::wire::kConsumerControlMaxNumButtons>
      button_fields;
  size_t num_buttons = 0;

  for (size_t i = 0; i < hid_report_descriptor.input_count; i++) {
    const hid::ReportField& field = hid_report_descriptor.input_fields[i];

    auto button = HidToConsumerControlButton(field.attr.usage);
    if (button) {
      if (num_buttons >= button_fields.size()) {
        return ParseResult::kTooManyItems;
      }
      button_fields[num_buttons] = field;
      num_buttons += 1;
    }
  }

  // No error, write to class members.

  num_buttons_ = num_buttons;
  button_fields_ = button_fields;

  input_report_size_ = hid_report_descriptor.input_byte_sz;
  input_report_id_ = hid_report_descriptor.report_id;

  return ParseResult::kOk;
}

ParseResult ConsumerControl::ParseReportDescriptor(
    const hid::ReportDescriptor& hid_report_descriptor) {
  return ParseInputReportDescriptor(hid_report_descriptor);
};

ParseResult ConsumerControl::CreateDescriptor(
    fidl::AnyArena& allocator, fuchsia_input_report::wire::DeviceDescriptor& descriptor) {
  fuchsia_input_report::wire::ConsumerControlInputDescriptor input(allocator);

  // Set the buttons array.
  {
    fidl::VectorView<ConsumerControlButton> buttons(allocator, num_buttons_);
    for (size_t i = 0; i < num_buttons_; i++) {
      auto button = HidToConsumerControlButton(button_fields_[i].attr.usage);
      if (button) {
        buttons[i] = *button;
      }
    }
    input.set_buttons(allocator, std::move(buttons));
  }

  fuchsia_input_report::wire::ConsumerControlDescriptor consumer(allocator);
  consumer.set_input(allocator, std::move(input));
  descriptor.set_consumer_control(allocator, std::move(consumer));

  return ParseResult::kOk;
}

ParseResult ConsumerControl::ParseInputReport(
    const uint8_t* data, size_t len, fidl::AnyArena& allocator,
    fuchsia_input_report::wire::InputReport& input_report) {
  fuchsia_input_report::wire::ConsumerControlInputReport consumer_report(allocator);

  std::array<ConsumerControlButton, fuchsia_input_report::wire::kConsumerControlMaxNumButtons>
      buttons;
  size_t buttons_size = 0;

  for (const hid::ReportField& field : button_fields_) {
    double val_out;
    if (!ExtractAsUnitType(data, len, field.attr, &val_out)) {
      continue;
    }

    if (static_cast<uint32_t>(val_out) == 0) {
      continue;
    }

    auto button = HidToConsumerControlButton(field.attr.usage);
    if (!button) {
      continue;
    }
    buttons[buttons_size++] = *button;
  }

  fidl::VectorView<ConsumerControlButton> fidl_buttons(allocator, buttons_size);
  for (size_t i = 0; i < buttons_size; i++) {
    fidl_buttons[i] = buttons[i];
  }
  consumer_report.set_pressed_buttons(allocator, std::move(fidl_buttons));

  input_report.set_consumer_control(allocator, std::move(consumer_report));
  return ParseResult::kOk;
}

}  // namespace hid_input_report
