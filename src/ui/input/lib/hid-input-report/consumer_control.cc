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
  descriptor_.input = ConsumerControlInputDescriptor();

  for (size_t i = 0; i < hid_report_descriptor.input_count; i++) {
    const hid::ReportField& field = hid_report_descriptor.input_fields[i];

    auto button = HidToConsumerControlButton(field.attr.usage);
    if (button) {
      if (num_buttons >= button_fields.size()) {
        return ParseResult::kParseTooManyItems;
      }
      descriptor_.input->buttons[num_buttons] = *button;
      button_fields[num_buttons] = field;
      num_buttons += 1;
    }
  }

  // No error, write to class members.
  descriptor_.input->num_buttons = num_buttons;

  num_buttons_ = num_buttons;
  button_fields_ = button_fields;

  input_report_size_ = hid_report_descriptor.input_byte_sz;
  input_report_id_ = hid_report_descriptor.report_id;

  return kParseOk;
}

ParseResult ConsumerControl::ParseReportDescriptor(
    const hid::ReportDescriptor& hid_report_descriptor) {
  return ParseInputReportDescriptor(hid_report_descriptor);
};

ReportDescriptor ConsumerControl::GetDescriptor() {
  ReportDescriptor report_descriptor = {};
  report_descriptor.descriptor = descriptor_;
  return report_descriptor;
}

ParseResult ConsumerControl::ParseInputReport(const uint8_t* data, size_t len,
                                              InputReport* report) {
  if (len != input_report_size_) {
    return kParseReportSizeMismatch;
  }

  ConsumerControlInputReport consumer_control_report = {};
  size_t button_index = 0;

  for (const hid::ReportField& field : button_fields_) {
    double val_out_double;
    if (!ExtractAsUnitType(data, len, field.attr, &val_out_double)) {
      continue;
    }

    uint32_t val_out = static_cast<uint32_t>(val_out_double);
    if (val_out == 0) {
      continue;
    }

    auto button = HidToConsumerControlButton(field.attr.usage);
    if (!button) {
      continue;
    }
    consumer_control_report.pressed_buttons[button_index++] = *button;
  }

  consumer_control_report.num_pressed_buttons = button_index;

  // Now that we can't fail, set the real report.
  report->report = consumer_control_report;

  return kParseOk;
}

}  // namespace hid_input_report
