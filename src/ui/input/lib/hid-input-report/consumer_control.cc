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

namespace {

std::optional<fuchsia_input_report::ConsumerControlButton> HidToConsumerControlButton(
    hid::Usage usage) {
  struct {
    hid::Usage usage;
    fuchsia_input_report::ConsumerControlButton button;
  } usage_to_button[] = {
      {hid::USAGE(hid::usage::Page::kConsumer, hid::usage::Consumer::kVolumeUp),
       fuchsia_input_report::ConsumerControlButton::VOLUME_UP},
      {hid::USAGE(hid::usage::Page::kConsumer, hid::usage::Consumer::kVolumeDown),
       fuchsia_input_report::ConsumerControlButton::VOLUME_DOWN},
      {hid::USAGE(hid::usage::Page::kConsumer, hid::usage::Consumer::kReset),
       fuchsia_input_report::ConsumerControlButton::REBOOT},
      {hid::USAGE(hid::usage::Page::kConsumer, hid::usage::Consumer::kCameraAccessDisabled),
       fuchsia_input_report::ConsumerControlButton::CAMERA_DISABLE},
      {hid::USAGE(hid::usage::Page::kTelephony, hid::usage::Telephony::kPhoneMute),
       fuchsia_input_report::ConsumerControlButton::MIC_MUTE},
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
  std::array<hid::ReportField, fuchsia_input_report::CONSUMER_CONTROL_MAX_NUM_BUTTONS>
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
    fidl::Allocator* allocator, fuchsia_input_report::DeviceDescriptor::Builder* descriptor) {
  auto input = fuchsia_input_report::ConsumerControlInputDescriptor::Builder(
      allocator->make<fuchsia_input_report::ConsumerControlInputDescriptor::Frame>());

  // Set the buttons array.
  {
    auto buttons = allocator->make<fuchsia_input_report::ConsumerControlButton[]>(num_buttons_);
    for (size_t i = 0; i < num_buttons_; i++) {
      auto button = HidToConsumerControlButton(button_fields_[i].attr.usage);
      if (button) {
        buttons[i] = *button;
      }
    }
    auto buttons_view =
        allocator->make<fidl::VectorView<fuchsia_input_report::ConsumerControlButton>>(
            std::move(buttons), num_buttons_);
    input.set_buttons(std::move(buttons_view));
  }

  auto consumer = fuchsia_input_report::ConsumerControlDescriptor::Builder(
      allocator->make<fuchsia_input_report::ConsumerControlDescriptor::Frame>());
  consumer.set_input(
      allocator->make<fuchsia_input_report::ConsumerControlInputDescriptor>(input.build()));
  descriptor->set_consumer_control(
      allocator->make<fuchsia_input_report::ConsumerControlDescriptor>(consumer.build()));

  return ParseResult::kOk;
}

ParseResult ConsumerControl::ParseInputReport(const uint8_t* data, size_t len,
                                              fidl::Allocator* allocator,
                                              fuchsia_input_report::InputReport::Builder* report) {
  auto consumer_report = fuchsia_input_report::ConsumerControlInputReport::Builder(
      allocator->make<fuchsia_input_report::ConsumerControlInputReport::Frame>());

  std::array<fuchsia_input_report::ConsumerControlButton,
             fuchsia_input_report::CONSUMER_CONTROL_MAX_NUM_BUTTONS>
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

  auto fidl_buttons = allocator->make<fuchsia_input_report::ConsumerControlButton[]>(buttons_size);
  for (size_t i = 0; i < buttons_size; i++) {
    fidl_buttons[i] = buttons[i];
  }
  consumer_report.set_pressed_buttons(
      allocator->make<fidl::VectorView<fuchsia_input_report::ConsumerControlButton>>(
          std::move(fidl_buttons), buttons_size));

  report->set_consumer_control(
      allocator->make<fuchsia_input_report::ConsumerControlInputReport>(consumer_report.build()));
  return ParseResult::kOk;
}

}  // namespace hid_input_report
