// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_INPUT_READER_INPUT_INTERPRETER_H_
#define APPS_MOZART_SRC_INPUT_READER_INPUT_INTERPRETER_H_

#include <map>
#include <vector>

#include "apps/mozart/services/input/input_events.fidl.h"
#include "apps/mozart/src/input_reader/input_device.h"
#include "apps/mozart/src/input_reader/input_report.h"
#include "apps/mozart/src/input_reader/input_state.h"

namespace mozart {
namespace input {

class InterpreterListener {
public:
  virtual void OnEvent(mozart::InputEventPtr event) = 0;
  virtual void OnDeviceAdded(const InputDevice* device) = 0;
  virtual void OnDeviceRemoved(const InputDevice* device) = 0;
};

class InputInterpreter {
 public:
  void SetListener(InterpreterListener* listener);

  void RegisterDisplay(mozart::Size dimension);
  void RegisterDevice(const InputDevice* device);
  void UnregisterDevice(const InputDevice* device);
  void OnReport(const InputDevice* device, InputReport::ReportType type);

 private:
  std::vector<InterpreterListener*> listeners_;
  std::map<const InputDevice*, std::unique_ptr<DeviceState>> devices_;
  mozart::Size display_size_;
};

}  // namespace input
}  // namespace mozart

#endif  // APPS_MOZART_SRC_INPUT_READER_INPUT_INTERPRETER_H_
