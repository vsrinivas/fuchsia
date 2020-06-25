// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_INPUT_REPORT_READER_INPUT_INTERPRETER_H_
#define SRC_UI_LIB_INPUT_REPORT_READER_INPUT_INTERPRETER_H_

#include <fuchsia/input/report/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/zx/event.h>
#include <zircon/types.h>

#include <array>
#include <string>
#include <vector>

#include "input_reader.h"

namespace ui_input {

// This class takes a zx::channel that connects to a device, and speaks to the
// device using fuchsia.input.report FIDL. This class converts the information
// to fuchsia.ui.input FIDL and sends through the InputDeviceRegistry.
// NOTE: At the moment this class only supports Touch and ConsumerControl because
// that is all that is currently needed for RootPresenter. If any additional devices
// need to be supported, please file a bug.
class InputInterpreter {
 public:
  ~InputInterpreter();

  // Creating an InputInterpreter takes a raw, unowned ptr to InputReaderBase. This is safe
  // because InputReaderBase owns InputInterpreter and will always outlive InputInterpreter.
  static std::unique_ptr<InputInterpreter> Create(InputReaderBase* base, zx::channel channel,
                            fuchsia::ui::input::InputDeviceRegistry* registry, std::string name);

  const std::string& name() const { return name_; }

 private:
  InputInterpreter(InputReaderBase* base, fuchsia::ui::input::InputDeviceRegistry* registry,
                   std::string name);

  void Initialize();

  void RegisterDevices();
  void RegisterTouchscreen(const fuchsia::input::report::DeviceDescriptor& descriptor);
  void RegisterConsumerControl(const fuchsia::input::report::DeviceDescriptor& descriptor);
  void RegisterMouse(const fuchsia::input::report::DeviceDescriptor& descriptor);

  void ReadReports(fuchsia::input::report::InputReportsReader_ReadInputReports_Result result);
  void DispatchReport(const fuchsia::ui::input::InputDevicePtr& device,
                      fuchsia::ui::input::InputReport report);
  void DispatchTouchReport(const fuchsia::input::report::InputReport& report);
  void DispatchMouseReport(const fuchsia::input::report::InputReport& report);
  void DispatchConsumerControlReport(const fuchsia::input::report::InputReport& report);

  InputReaderBase* base_;
  fuchsia::input::report::InputDevicePtr device_;
  fuchsia::input::report::InputReportsReaderPtr reader_;
  fuchsia::ui::input::InputDeviceRegistry* registry_;

  std::string name_;
  fuchsia::ui::input::InputDevicePtr touch_ptr_;
  fuchsia::ui::input::InputDevicePtr consumer_control_ptr_;
  fuchsia::ui::input::InputDevicePtr mouse_ptr_;
};

}  // namespace ui_input

#endif  // SRC_UI_LIB_INPUT_REPORT_READER_INPUT_INTERPRETER_H_
