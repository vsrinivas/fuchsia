// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_report_reader/input_interpreter.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <zircon/errors.h>
#include <zircon/types.h>

namespace ui_input {

namespace {

fuchsia::ui::input::Axis ConvertAxis(fuchsia::input::report::Axis axis) {
  fuchsia::ui::input::Axis a = {};
  a.range.min = static_cast<int32_t>(axis.range.min);
  a.range.max = static_cast<int32_t>(axis.range.max);
  return a;
}

// Sets the fuchsia::ui::input Mouse button bit vector.
// prev_buttons represents the current button bit vector.
// button_id is the new button to set.
// The function returns the new button bit vector.
uint32_t SetMouseButton(uint32_t prev_buttons, uint8_t button_id) {
  if (button_id == 0 || button_id > 32) {
    return prev_buttons;
  }
  return prev_buttons | (1 << (button_id - 1));
}

}  // namespace

InputInterpreter::InputInterpreter(InputReaderBase* base,
                                   fuchsia::ui::input::InputDeviceRegistry* registry,
                                   std::string name)
    : base_(base), registry_(registry), name_(name) {}

InputInterpreter::~InputInterpreter() {}

void InputInterpreter::DispatchReport(const fuchsia::ui::input::InputDevicePtr& device,
                                      fuchsia::ui::input::InputReport report) {
  report.trace_id = TRACE_NONCE();
  TRACE_FLOW_BEGIN("input", "hid_read_to_listener", report.trace_id);
  device->DispatchReport(std::move(report));
}

std::unique_ptr<InputInterpreter> InputInterpreter::Create(
    InputReaderBase* base, zx::channel channel, fuchsia::ui::input::InputDeviceRegistry* registry,
    std::string name) {
  // Using `new` to access a non-public constructor.
  auto interpreter = std::unique_ptr<InputInterpreter>(new InputInterpreter(base, registry, name));
  zx_status_t status = interpreter->device_.Bind(std::move(channel));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "InputInterpreter::Create: Bind error: " << status;
    return nullptr;
  }
  interpreter->Initialize();
  return interpreter;
}

void InputInterpreter::Initialize() {
  device_.set_error_handler([this](zx_status_t status) {
    if (status != ZX_ERR_PEER_CLOSED) {
      FX_LOGS(ERROR) << "InputInterpreter: Device error: " << status;
    }
    base_->RemoveDevice(this);
  });

  RegisterDevices();
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

void InputInterpreter::RegisterMouse(const fuchsia::input::report::DeviceDescriptor& descriptor) {
  fuchsia::ui::input::DeviceDescriptor ui_descriptor;

  if (descriptor.has_device_info()) {
    auto info = std::make_unique<fuchsia::ui::input::DeviceInfo>();
    info->vendor_id = descriptor.device_info().vendor_id;
    info->product_id = descriptor.device_info().product_id;
    info->version = descriptor.device_info().version;
    ui_descriptor.device_info = std::move(info);
  }

  auto mouse = std::make_unique<fuchsia::ui::input::MouseDescriptor>();
  if (descriptor.mouse().input().has_movement_x()) {
    mouse->rel_x = ConvertAxis(descriptor.mouse().input().movement_x());
  }
  if (descriptor.mouse().input().has_movement_y()) {
    mouse->rel_y = ConvertAxis(descriptor.mouse().input().movement_y());
  }
  if (descriptor.mouse().input().has_buttons()) {
    for (uint8_t button : descriptor.mouse().input().buttons()) {
      mouse->buttons = SetMouseButton(mouse->buttons, button);
    }
  }
  ui_descriptor.mouse = std::move(mouse);
  registry_->RegisterDevice(std::move(ui_descriptor), mouse_ptr_.NewRequest());
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
      touch->x.range.min =
          static_cast<int32_t>(descriptor.touch().input().contacts()[0].position_x().range.min);
      touch->x.range.max =
          static_cast<int32_t>(descriptor.touch().input().contacts()[0].position_x().range.max);
    }
    if (descriptor.touch().input().contacts()[0].has_position_y()) {
      touch->y.range.min =
          static_cast<int32_t>(descriptor.touch().input().contacts()[0].position_y().range.min);
      touch->y.range.max =
          static_cast<int32_t>(descriptor.touch().input().contacts()[0].position_y().range.max);
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
  device_->GetDescriptor([this](fuchsia::input::report::DeviceDescriptor descriptor) {
    if (descriptor.has_touch() && descriptor.touch().has_input()) {
      if (descriptor.touch().input().has_touch_type() &&
          (descriptor.touch().input().touch_type() ==
           fuchsia::input::report::TouchType::TOUCHSCREEN)) {
        RegisterTouchscreen(descriptor);
      }
    }

    if (descriptor.has_mouse() && descriptor.mouse().has_input()) {
      RegisterMouse(descriptor);
    }

    if (descriptor.has_consumer_control() && descriptor.consumer_control().has_input()) {
      RegisterConsumerControl(descriptor);
    }

    // Now that devices are registered we can start reading requests.
    device_->GetInputReportsReader(reader_.NewRequest());
    reader_.set_error_handler([this](zx_status_t status) {
      if (status != ZX_ERR_PEER_CLOSED) {
        FX_LOGS(ERROR) << "InputInterpreter: Reader error: " << status;
      }
      base_->RemoveDevice(this);
    });

    // Kick off the first Read, which will queue up the rest of the reads.
    reader_->ReadInputReports([this](auto result) { ReadReports(std::move(result)); });
  });
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
        input_touch.x = static_cast<int32_t>(contact.position_x());
      }
      if (contact.has_position_y()) {
        input_touch.y = static_cast<int32_t>(contact.position_y());
      }
      input_touchscreen->touches.push_back(std::move(input_touch));
    }
  }
  input_report.touchscreen = std::move(input_touchscreen);
  DispatchReport(touch_ptr_, std::move(input_report));
}

void InputInterpreter::DispatchMouseReport(const fuchsia::input::report::InputReport& report) {
  fuchsia::ui::input::InputReport input_report;
  if (report.has_event_time()) {
    input_report.event_time = report.event_time();
  }
  auto input_mouse = std::make_unique<fuchsia::ui::input::MouseReport>();
  if (report.mouse().has_movement_x()) {
    input_mouse->rel_x = static_cast<uint32_t>(report.mouse().movement_x());
  }
  if (report.mouse().has_movement_y()) {
    input_mouse->rel_y = static_cast<uint32_t>(report.mouse().movement_y());
  }
  if (report.mouse().has_pressed_buttons()) {
    for (uint8_t button : report.mouse().pressed_buttons()) {
      input_mouse->pressed_buttons = SetMouseButton(input_mouse->pressed_buttons, button);
    }
  }
  input_report.mouse = std::move(input_mouse);
  DispatchReport(mouse_ptr_, std::move(input_report));
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

void InputInterpreter::ReadReports(
    fuchsia::input::report::InputReportsReader_ReadInputReports_Result result) {
  TRACE_DURATION("input", "input_report_reader Read");

  if (result.is_err()) {
    FX_LOGS(INFO) << "InputInterpreter: ReadInputReports received status code: " << result.err();
    base_->RemoveDevice(this);
    return;
  }

  if (base_->ActiveInput()) {
    for (const auto& report : result.response().reports) {
      if (report.has_trace_id()) {
        TRACE_FLOW_END("input", "input_report", report.trace_id());
      }
      if (report.has_touch()) {
        DispatchTouchReport(report);
      }
      if (report.has_consumer_control()) {
        DispatchConsumerControlReport(report);
      }
      if (report.has_mouse()) {
        DispatchMouseReport(report);
      }
    }
  }

  // Queue ourselves up again for another read.
  reader_->ReadInputReports([this](auto result) { ReadReports(std::move(result)); });
}

}  // namespace ui_input
