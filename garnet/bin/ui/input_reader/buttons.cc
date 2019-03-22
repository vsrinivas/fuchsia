// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/buttons.h"

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>

#include <stdint.h>
#include <stdio.h>
#include <vector>

#include <lib/fxl/logging.h>

namespace mozart {

bool Buttons::ParseReportDescriptor(
    const hid::ReportDescriptor& report_descriptor,
    Descriptor* device_descriptor) {
  FXL_CHECK(device_descriptor);

  hid::Attributes volume = {};
  hid::Attributes phone_mute = {};
  uint32_t caps = 0;

  for (size_t i = 0; i < report_descriptor.input_count; i++) {
    const hid::ReportField& field = report_descriptor.input_fields[i];

    if (field.attr.usage == hid::USAGE(hid::usage::Page::kConsumer,
                                       hid::usage::Consumer::kVolume)) {
      volume = field.attr;
      caps |= Capabilities::VOLUME;
    } else if (field.attr.usage ==
               hid::USAGE(hid::usage::Page::kTelephony,
                          hid::usage::Telephony::kPhoneMute)) {
      phone_mute = field.attr;
      caps |= Capabilities::PHONE_MUTE;
    }
  }

  if (caps == 0) {
    FXL_LOG(ERROR) << "Buttons report descriptor: Buttons has no capabilities";
    return false;
  }

  volume_ = volume;
  phone_mute_ = phone_mute;

  report_size_ = report_descriptor.input_byte_sz;
  report_id_ = report_descriptor.report_id;
  capabilities_ = caps;

  // Set the device descriptor.
  device_descriptor->protocol = Protocol::Buttons;
  device_descriptor->has_buttons = true;
  device_descriptor->buttons_descriptor =
      fuchsia::ui::input::ButtonsDescriptor::New();
  if (caps & Capabilities::PHONE_MUTE) {
    device_descriptor->buttons_descriptor->buttons |=
        fuchsia::ui::input::kMicMute;
  }
  if (caps & Capabilities::VOLUME) {
    device_descriptor->buttons_descriptor->buttons |=
        fuchsia::ui::input::kVolumeUp;
    device_descriptor->buttons_descriptor->buttons |=
        fuchsia::ui::input::kVolumeDown;
  }
  return true;
}

bool Buttons::ParseReport(const uint8_t* data, size_t len,
                          fuchsia::ui::input::InputReport* report) {
  FXL_CHECK(report);
  FXL_CHECK(report->buttons);
  double volume = 0;
  double mic_mute = 0;

  if (report_size_ != len) {
    FXL_LOG(ERROR) << "Sensor report: Expected size " << report_size_
                   << "Received size " << len;
    return false;
  }

  if (capabilities_ & Capabilities::VOLUME) {
    if (!hid::ExtractAsUnit(data, len, volume_, &volume)) {
      FXL_LOG(ERROR) << "Sensor report: Failed to parse volume";
      return false;
    }
  }
  if (capabilities_ & Capabilities::PHONE_MUTE) {
    if (!hid::ExtractAsUnit(data, len, phone_mute_, &mic_mute)) {
      FXL_LOG(ERROR) << "Sensor report: Failed to parse phone_mute";
      return false;
    }
  }

  report->buttons->mic_mute = 0;
  report->buttons->volume = 0;

  if (capabilities_ & Capabilities::PHONE_MUTE) {
    report->buttons->mic_mute = (mic_mute > 0);
  }
  if (capabilities_ & Capabilities::VOLUME) {
    report->buttons->volume = static_cast<int8_t>(volume);
  }

  return true;
}

}  // namespace mozart
