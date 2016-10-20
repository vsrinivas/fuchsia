// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_INTERPRETER_H_
#define APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_INTERPRETER_H_

#include <map>
#include <vector>

#include "apps/mozart/services/input/interfaces/input_events.mojom.h"
#include "apps/mozart/src/launcher/input/input_device.h"
#include "apps/mozart/src/launcher/input/input_report.h"
#include "apps/mozart/src/launcher/input/input_state.h"

namespace mozart {
namespace input {

using OnEventCallback = std::function<void(mozart::EventPtr event)>;

class InputInterpreter {
 public:
  void RegisterCallback(OnEventCallback callback);
  void RegisterDevice(const InputDevice* device);
  void UnregisterDevice(const InputDevice* device);
  void OnReport(const InputDevice* device, InputReport::ReportType type);

 private:
  std::vector<OnEventCallback> callbacks_;
  std::map<const InputDevice*, DeviceState> devices_;
};

}  // namespace input
}  // namespace mozart

#endif  // APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_INTERPRETER_H_
