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

namespace ui_input {

// This class takes a zx::channel that connects to a device, and speaks to the
// device using fuchsia.input.report FIDL. This class converts the information
// to fuchsia.ui.input FIDL and sends through the InputDeviceRegistry.
// NOTE: At the moment this class only supports Touch and ConsumerControl because
// that is all that is currently needed for RootPresenter. If any additional devices
// need to be supported, please file a bug.
class InputInterpreter {
 public:
  InputInterpreter(zx::channel channel, fuchsia::ui::input::InputDeviceRegistry* registry,
                   std::string name);
  ~InputInterpreter();

  bool Initialize();
  bool Read(bool discard);

  const std::string& name() const { return name_; }
  zx_handle_t handle() { return event_.get(); }

 private:
  void RegisterDevices();
  void RegisterTouchscreen(const fuchsia::input::report::DeviceDescriptor& descriptor);
  void RegisterConsumerControl(const fuchsia::input::report::DeviceDescriptor& descriptor);

  void DispatchReport(const fuchsia::ui::input::InputDevicePtr& device,
                      fuchsia::ui::input::InputReport report);
  void DispatchTouchReport(const fuchsia::input::report::InputReport& report);
  void DispatchConsumerControlReport(const fuchsia::input::report::InputReport& report);

  fuchsia::input::report::InputDevice_SyncProxy device_;
  fuchsia::ui::input::InputDeviceRegistry* registry_;
  zx::channel channel_;

  zx::event event_;

  std::string name_;
  fuchsia::ui::input::InputDevicePtr touch_ptr_;
  fuchsia::ui::input::InputDevicePtr consumer_control_ptr_;
};

}  // namespace ui_input

#endif  // SRC_UI_LIB_INPUT_REPORT_READER_INPUT_INTERPRETER_H_
