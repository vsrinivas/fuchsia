// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_INPUT_INPUT_DEVICE_IMPL_H_
#define LIB_UI_INPUT_INPUT_DEVICE_IMPL_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include "lib/fidl/cpp/binding.h"

namespace mozart {

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

 private:
  // |InputDevice|
  void DispatchReport(fuchsia::ui::input::InputReport report) override;

  uint32_t id_;
  fuchsia::ui::input::DeviceDescriptor descriptor_;
  fidl::Binding<fuchsia::ui::input::InputDevice> input_device_binding_;
  Listener* listener_;
};

}  // namespace mozart

#endif  // LIB_UI_INPUT_INPUT_DEVICE_IMPL_H_
