// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_reader/input_interpreter.h"

#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/logging.h"

namespace mozart {
namespace input {

void InputInterpreter::RegisterCallback(OnEventCallback callback) {
  callbacks_.push_back(callback);
}

void InputInterpreter::RegisterDevice(const InputDevice* device) {
  FTL_DCHECK(devices_.count(device) == 0);
  auto on_update = [this](mozart::InputEventPtr event) {
    for (auto callback : callbacks_) {
      callback(event.Clone());
    }
  };
  devices_.emplace(device, new DeviceState(on_update));
}

void InputInterpreter::UnregisterDevice(const InputDevice* device) {
  devices_.erase(device);
}

void InputInterpreter::RegisterDisplay(mozart::Size dimension) {
  display_size_ = dimension;
}

void InputInterpreter::OnReport(const InputDevice* device,
                                InputReport::ReportType type) {
  auto it = devices_.find(device);
  if (it == devices_.end()) {
    return;
  }

  TRACE_DURATION("input", "OnReport", "type", type);
  DeviceState* state = it->second.get();

  switch (type) {
    case InputReport::ReportType::kKeyboard:
      state->keyboard.Update(device->keyboard_report(),
                             device->keyboard_descriptor());
      break;
    case InputReport::ReportType::kMouse:
      state->mouse.Update(device->mouse_report(), device->mouse_descriptor(),
                          display_size_);
      break;
    case InputReport::ReportType::kStylus:
      state->stylus.Update(device->stylus_report(), device->stylus_descriptor(),
                           display_size_);
      break;
    case InputReport::ReportType::kTouchscreen:
      state->touchscreen.Update(device->touch_report(),
                                device->touchscreen_descriptor(),
                                display_size_);
      break;
  }
}

}  // namespace input
}  // namespace mozart
