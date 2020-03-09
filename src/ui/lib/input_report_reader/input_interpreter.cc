// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_report_reader/input_interpreter.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <trace/event.h>

#include "src/lib/fxl/arraysize.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/time/time_point.h"

namespace ui_input {

InputInterpreter::InputInterpreter(zx::channel channel,
                                   fuchsia::ui::input::InputDeviceRegistry* registry,
                                   std::string name)
    : device_(std::move(channel)), registry_(registry), name_(name) {}

InputInterpreter::~InputInterpreter() {}

void InputInterpreter::DispatchReport(const fuchsia::ui::input::InputDevicePtr& device,
                                      fuchsia::ui::input::InputReport report) {
  report.trace_id = TRACE_NONCE();
  TRACE_FLOW_BEGIN("input", "hid_read_to_listener", report.trace_id);
  device->DispatchReport(std::move(report));
}

bool InputInterpreter::Initialize() {
  // Get the event.
  zx_status_t out_status;
  zx_status_t status = device_.GetReportsEvent(&out_status, &event_);
  if ((status != ZX_OK) || (out_status != ZX_OK)) {
    return false;
  }

  RegisterDevices();

  return true;
}

void InputInterpreter::RegisterConsumerControl(
    const fuchsia::input::report::DeviceDescriptor& descriptor) {
  fuchsia::ui::input::DeviceDescriptor ui_descriptor;

  if (descriptor.has_device_info()) {
    auto info = std::make_unique<fuchsia::ui::input::DeviceInfo>();
    info->vendor_id = descriptor.device_info().vendor_id;
    info->product_id = descriptor.device_info().product_id;
    info->version = descriptor.device_info().version;
    ui_descriptor.device_info = std::move(info);
  }

  auto media_buttons = std::make_unique<fuchsia::ui::input::MediaButtonsDescriptor>();
  if (descriptor.consumer_control().input().has_buttons()) {
    for (auto button : descriptor.consumer_control().input().buttons()) {
      switch (button) {
        case fuchsia::input::report::ConsumerControlButton::VOLUME_UP:
          media_buttons->buttons |= fuchsia::ui::input::kVolumeUp;
          break;
        case fuchsia::input::report::ConsumerControlButton::VOLUME_DOWN:
          media_buttons->buttons |= fuchsia::ui::input::kVolumeDown;
          break;
        case fuchsia::input::report::ConsumerControlButton::MIC_MUTE:
          media_buttons->buttons |= fuchsia::ui::input::kMicMute;
          break;
        case fuchsia::input::report::ConsumerControlButton::PAUSE:
          media_buttons->buttons |= fuchsia::ui::input::kPause;
          break;
        case fuchsia::input::report::ConsumerControlButton::REBOOT:
          media_buttons->buttons |= fuchsia::ui::input::kReset;
          break;
        default:
          break;
      }
    }
  }
  ui_descriptor.media_buttons = std::move(media_buttons);
  registry_->RegisterDevice(std::move(ui_descriptor), consumer_control_ptr_.NewRequest());
}

void InputInterpreter::RegisterTouchscreen(
    const fuchsia::input::report::DeviceDescriptor& descriptor) {
  fuchsia::ui::input::DeviceDescriptor ui_descriptor;

  if (descriptor.has_device_info()) {
    auto info = std::make_unique<fuchsia::ui::input::DeviceInfo>();
    info->vendor_id = descriptor.device_info().vendor_id;
    info->product_id = descriptor.device_info().product_id;
    info->version = descriptor.device_info().version;
    ui_descriptor.device_info = std::move(info);
  }

  auto touch = std::make_unique<fuchsia::ui::input::TouchscreenDescriptor>();
  if (descriptor.touch().input().has_contacts()) {
    if (descriptor.touch().input().contacts()[0].has_position_x()) {
      touch->x.range.min = descriptor.touch().input().contacts()[0].position_x().range.min;
      touch->x.range.max = descriptor.touch().input().contacts()[0].position_x().range.max;
    }
    if (descriptor.touch().input().contacts()[0].has_position_y()) {
      touch->y.range.min = descriptor.touch().input().contacts()[0].position_y().range.min;
      touch->y.range.max = descriptor.touch().input().contacts()[0].position_y().range.max;
    }
  }
  if (descriptor.touch().input().has_max_contacts()) {
    touch->max_finger_id = descriptor.touch().input().max_contacts();
  }
  ui_descriptor.touchscreen = std::move(touch);
  registry_->RegisterDevice(std::move(ui_descriptor), touch_ptr_.NewRequest());
}

void InputInterpreter::RegisterDevices() {
  fuchsia::input::report::DeviceDescriptor descriptor;
  zx_status_t status = device_.GetDescriptor(&descriptor);
  if (status != ZX_OK) {
    return;
  }

  if (descriptor.has_touch() && descriptor.touch().has_input()) {
    if (descriptor.touch().input().has_touch_type() &&
        (descriptor.touch().input().touch_type() ==
         fuchsia::input::report::TouchType::TOUCHSCREEN)) {
      RegisterTouchscreen(descriptor);
    }
  }

  if (descriptor.has_consumer_control() && descriptor.consumer_control().has_input()) {
    RegisterConsumerControl(descriptor);
  }
}

void InputInterpreter::DispatchTouchReport(const fuchsia::input::report::InputReport& report) {
  fuchsia::ui::input::InputReport input_report;
  if (report.has_event_time()) {
    input_report.event_time = report.event_time();
  }
  auto input_touchscreen = std::make_unique<fuchsia::ui::input::TouchscreenReport>();
  if (report.touch().has_contacts()) {
    for (const fuchsia::input::report::ContactInputReport& contact : report.touch().contacts()) {
      fuchsia::ui::input::Touch input_touch;
      if (contact.has_contact_id()) {
        input_touch.finger_id = contact.contact_id();
      }
      if (contact.has_position_x()) {
        input_touch.x = contact.position_x();
      }
      if (contact.has_position_y()) {
        input_touch.y = contact.position_y();
      }
      input_touchscreen->touches.push_back(std::move(input_touch));
    }
  }
  input_report.touchscreen = std::move(input_touchscreen);
  DispatchReport(touch_ptr_, std::move(input_report));
}

void InputInterpreter::DispatchConsumerControlReport(
    const fuchsia::input::report::InputReport& report) {
  fuchsia::ui::input::InputReport input_report;
  if (report.has_event_time()) {
    input_report.event_time = report.event_time();
  }
  auto media_buttons = std::make_unique<fuchsia::ui::input::MediaButtonsReport>();
  if (report.consumer_control().has_pressed_buttons()) {
    for (const fuchsia::input::report::ConsumerControlButton& button :
         report.consumer_control().pressed_buttons()) {
      switch (button) {
        case fuchsia::input::report::ConsumerControlButton::VOLUME_UP:
          media_buttons->volume_up = true;
          break;
        case fuchsia::input::report::ConsumerControlButton::VOLUME_DOWN:
          media_buttons->volume_down = true;
          break;
        case fuchsia::input::report::ConsumerControlButton::MIC_MUTE:
          media_buttons->mic_mute = true;
          break;
        case fuchsia::input::report::ConsumerControlButton::PAUSE:
          media_buttons->pause = true;
          break;
        case fuchsia::input::report::ConsumerControlButton::REBOOT:
          media_buttons->reset = true;
          break;
        default:
          break;
      }
    }
  }
  input_report.media_buttons = std::move(media_buttons);
  DispatchReport(consumer_control_ptr_, std::move(input_report));
}

bool InputInterpreter::Read(bool discard) {
  TRACE_DURATION("input", "input_report_reader Read");
  std::vector<fuchsia::input::report::InputReport> reports;
  zx_status_t status = device_.GetReports(&reports);
  if (status != ZX_OK) {
    return false;
  }
  if (discard) {
    return true;
  }

  for (const auto& report : reports) {
    if (report.has_trace_id()) {
      TRACE_FLOW_END("input", "input_report", report.trace_id());
    }
    if (report.has_touch()) {
      DispatchTouchReport(report);
    }
    if (report.has_consumer_control()) {
      DispatchConsumerControlReport(report);
    }
  }

  // Create reports and call DispatchReport
  return true;
}

}  // namespace ui_input
