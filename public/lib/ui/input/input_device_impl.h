// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_INPUT_INPUT_DEVICE_IMPL_H_
#define APPS_MOZART_LIB_INPUT_INPUT_DEVICE_IMPL_H_

#include "apps/mozart/services/input/input_device_registry.fidl.h"
#include "apps/mozart/services/input/input_reports.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace mozart {

class InputDeviceImpl : public mozart::InputDevice {
 public:
  class Listener {
  public:
    virtual void OnDeviceDisconnected(InputDeviceImpl* input_device) = 0;
    virtual void OnReport(InputDeviceImpl* input_device,
                          mozart::InputReportPtr report) = 0;
  };

  InputDeviceImpl(
      uint32_t id,
      mozart::DeviceDescriptorPtr descriptor,
      fidl::InterfaceRequest<mozart::InputDevice> input_device_request,
      Listener* listener);
  ~InputDeviceImpl();

  uint32_t id() { return id_; }
  mozart::DeviceDescriptor* descriptor() { return descriptor_.get(); }

 private:
  // |InputDevice|
  void DispatchReport(mozart::InputReportPtr report) override;

  uint32_t id_;
  mozart::DeviceDescriptorPtr descriptor_;
  fidl::Binding<mozart::InputDevice> input_device_binding_;
  Listener* listener_;
};

}  // namespace mozart

#endif  // APPS_MOZART_LIB_INPUT_INPUT_DEVICE_IMPL_H_
