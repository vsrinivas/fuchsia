// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_reader/input_interpreter.h"

#include "apps/mozart/glue/base/trace_event.h"
#include "lib/ftl/logging.h"

namespace mozart {
namespace input {

void InputInterpreter::RegisterCallback(OnEventCallback callback) {
  callbacks_.push_back(callback);
}

void InputInterpreter::RegisterDevice(const InputDevice* device) {
  if (!devices_.count(device)) {
    FTL_LOG(INFO) << "Registering " << device->name();
    TRACE_EVENT1("input", "RegisterDevice", "device", device->name());
    devices_[device] = DeviceState();
  }
}

void InputInterpreter::UnregisterDevice(const InputDevice* device) {
  FTL_LOG(INFO) << "Unregistering " << device->name();
  TRACE_EVENT1("input", "UnregisterDevice", "device", device->name());
  devices_.erase(device);
}

void InputInterpreter::RegisterDisplay(mojo::Size dimension) {
  display_size_ = dimension;
}

void InputInterpreter::OnReport(const InputDevice* device,
                                InputReport::ReportType type) {
  auto it = devices_.find(device);
  if (it == devices_.end()) {
    return;
  }

  TRACE_EVENT1("input", "OnReport", "type", type);
  DeviceState& state = it->second;
  auto on_update = [this](mozart::EventPtr event) {
    for (auto callback : callbacks_) {
      callback(event.Clone());
    }
  };

  switch (type) {
    case InputReport::ReportType::kKeyboard:
      state.keyboard.Update(device->keyboard_report(),
                            device->keyboard_descriptor(), on_update);
      break;
    case InputReport::ReportType::kMouse:
      state.mouse.Update(device->mouse_report(), device->mouse_descriptor(),
                         display_size_, on_update);
      break;
    case InputReport::ReportType::kStylus:
      state.stylus.Update(device->stylus_report(), device->stylus_descriptor(),
                          display_size_, on_update);
      break;
    case InputReport::ReportType::kTouchscreen:
      state.touchscreen.Update(device->touch_report(),
                               device->touchscreen_descriptor(), display_size_,
                               on_update);
      break;
  }
}

}  // namespace input
}  // namespace mozart
