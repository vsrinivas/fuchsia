// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_INPUT_INPUT_DEVICE_IMPL_H_
#define LIB_UI_INPUT_INPUT_DEVICE_IMPL_H_

#include <fuchsia/ui/input/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"

namespace ui_input {

class InputDeviceImpl : public fuchsia::ui::input::InputDevice {
 public:
  class Listener {
   public:
    virtual void OnDeviceDisconnected(InputDeviceImpl* input_device) = 0;
    virtual void OnReport(InputDeviceImpl* input_device,
                          fuchsia::ui::input::InputReport report) = 0;
  };

  InputDeviceImpl(uint32_t id, fuchsia::ui::input::DeviceDescriptor descriptor,
                  fidl::InterfaceRequest<fuchsia::ui::input::InputDevice>
                      input_device_request,
                  Listener* listener);
  ~InputDeviceImpl();

  uint32_t id() { return id_; }
  fuchsia::ui::input::DeviceDescriptor* descriptor() { return &descriptor_; }

  // Returns the last seen InputReport or nullptr if no reports have been
  // seen. At the moment we only ever save InputReports from a MediaButton,
  // so all other device types will always return nullptr.
  const fuchsia::ui::input::InputReport* LastReport() const {
    return last_report_.get();
  }

 private:
  // |InputDevice|
  void DispatchReport(fuchsia::ui::input::InputReport report) override;

  uint32_t id_;
  fuchsia::ui::input::DeviceDescriptor descriptor_;
  fuchsia::ui::input::InputReportPtr last_report_ = nullptr;
  fidl::Binding<fuchsia::ui::input::InputDevice> input_device_binding_;
  Listener* listener_;
};

}  // namespace ui_input

#endif  // LIB_UI_INPUT_INPUT_DEVICE_IMPL_H_
