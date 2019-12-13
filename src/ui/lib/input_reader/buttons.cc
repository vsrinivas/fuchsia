// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_reader/buttons.h"

#include <stdint.h>
#include <stdio.h>

#include <vector>

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>

#include "src/lib/fxl/logging.h"

namespace ui_input {

bool Buttons::ParseReportDescriptor(const hid::ReportDescriptor& report_descriptor,
                                    Descriptor* device_descriptor) {
  FXL_CHECK(device_descriptor);

  hid::Attributes volume_up = {};
  hid::Attributes volume_down = {};
  hid::Attributes reset = {};
  hid::Attributes phone_mute = {};
  hid::Attributes pause = {};
  uint32_t caps = 0;

  for (size_t i = 0; i < report_descriptor.input_count; i++) {
    const hid::ReportField& field = report_descriptor.input_fields[i];

    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kConsumer, hid::usage::Consumer::kVolumeUp)) {
      volume_up = field.attr;
      caps |= Capabilities::VOLUME_UP;
    } else if (field.attr.usage ==
               hid::USAGE(hid::usage::Page::kConsumer, hid::usage::Consumer::kVolumeDown)) {
      volume_down = field.attr;
      caps |= Capabilities::VOLUME_DOWN;
    } else if (field.attr.usage ==
               hid::USAGE(hid::usage::Page::kConsumer, hid::usage::Consumer::kReset)) {
      reset = field.attr;
      caps |= Capabilities::RESET;
    } else if (field.attr.usage ==
               hid::USAGE(hid::usage::Page::kTelephony, hid::usage::Telephony::kPhoneMute)) {
      phone_mute = field.attr;
      caps |= Capabilities::PHONE_MUTE;
    } else if (field.attr.usage ==
               hid::USAGE(hid::usage::Page::kConsumer, hid::usage::Consumer::kPause)) {
      pause = field.attr;
      caps |= Capabilities::PAUSE;
    }
  }

  if (caps == 0) {
    FXL_LOG(INFO) << "Buttons report descriptor: Buttons has no capabilities";
    return false;
  }

  volume_up_ = volume_up;
  volume_down_ = volume_down;
  reset_ = reset;
  phone_mute_ = phone_mute;
  pause_ = pause;

  report_size_ = report_descriptor.input_byte_sz;
  report_id_ = report_descriptor.report_id;
  capabilities_ = caps;

  // Set the device descriptor.
  device_descriptor->protocol = Protocol::MediaButtons;
  device_descriptor->has_media_buttons = true;
  device_descriptor->buttons_descriptor = fuchsia::ui::input::MediaButtonsDescriptor::New();
  if (caps & Capabilities::PHONE_MUTE) {
    device_descriptor->buttons_descriptor->buttons |= fuchsia::ui::input::kMicMute;
  }
  if (caps & Capabilities::VOLUME_UP) {
    device_descriptor->buttons_descriptor->buttons |= fuchsia::ui::input::kVolumeUp;
  }
  if (caps & Capabilities::VOLUME_DOWN) {
    device_descriptor->buttons_descriptor->buttons |= fuchsia::ui::input::kVolumeDown;
  }
  if (caps & Capabilities::RESET) {
    device_descriptor->buttons_descriptor->buttons |= fuchsia::ui::input::kReset;
  }
  if (caps & Capabilities::PAUSE) {
    device_descriptor->buttons_descriptor->buttons |= fuchsia::ui::input::kPause;
  }
  return true;
}

bool Buttons::ParseReport(const uint8_t* data, size_t len,
                          fuchsia::ui::input::InputReport* report) {
  FXL_CHECK(report);
  FXL_CHECK(report->media_buttons);
  double volume_up = 0;
  double volume_down = 0;
  double reset = 0;
  double mic_mute = 0;
  double pause = 0;

  if (report_size_ != len) {
    FXL_LOG(INFO) << "Sensor report: Expected size " << report_size_ << "Received size " << len;
    return false;
  }

  if (capabilities_ & Capabilities::VOLUME_UP) {
    if (!hid::ExtractAsUnit(data, len, volume_up_, &volume_up)) {
      FXL_LOG(INFO) << "Sensor report: Failed to parse volume";
      return false;
    }
  }
  if (capabilities_ & Capabilities::VOLUME_DOWN) {
    if (!hid::ExtractAsUnit(data, len, volume_down_, &volume_down)) {
      FXL_LOG(INFO) << "Sensor report: Failed to parse volume";
      return false;
    }
  }
  if (capabilities_ & Capabilities::RESET) {
    if (!hid::ExtractAsUnit(data, len, reset_, &reset)) {
      FXL_LOG(INFO) << "Sensor report: Failed to parse reset";
      return false;
    }
  }
  if (capabilities_ & Capabilities::PHONE_MUTE) {
    if (!hid::ExtractAsUnit(data, len, phone_mute_, &mic_mute)) {
      FXL_LOG(INFO) << "Sensor report: Failed to parse phone_mute";
      return false;
    }
  }
  if (capabilities_ & Capabilities::PAUSE) {
    if (!hid::ExtractAsUnit(data, len, pause_, &pause)) {
      FXL_LOG(INFO) << "Sensor report: Failed to parse pause";
      return false;
    }
  }

  report->media_buttons->mic_mute = 0;
  report->media_buttons->volume_up = 0;
  report->media_buttons->volume_down = 0;
  report->media_buttons->reset = 0;
  report->media_buttons->pause = 0;

  if (capabilities_ & Capabilities::PHONE_MUTE) {
    report->media_buttons->mic_mute = (mic_mute > 0);
  }
  if (capabilities_ & Capabilities::VOLUME_UP) {
    report->media_buttons->volume_up = (volume_up > 0);
  }
  if (capabilities_ & Capabilities::VOLUME_DOWN) {
    report->media_buttons->volume_down = (volume_down > 0);
  }
  if (capabilities_ & Capabilities::RESET) {
    report->media_buttons->reset = (reset > 0);
  }
  if (capabilities_ & Capabilities::PAUSE) {
    report->media_buttons->pause = (pause > 0);
  }

  return true;
}

}  // namespace ui_input
