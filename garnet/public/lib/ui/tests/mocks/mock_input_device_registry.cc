// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/tests/mocks/mock_input_device_registry.h"

namespace mozart {
namespace test {

MockInputDeviceRegistry::MockInputDeviceRegistry(
    OnDeviceCallback on_device_callback, OnReportCallback on_report_callback)
    : on_device_callback_(std::move(on_device_callback)),
      on_report_callback_(std::move(on_report_callback)) {}

MockInputDeviceRegistry::~MockInputDeviceRegistry() {}

void MockInputDeviceRegistry::RegisterDevice(
    fuchsia::ui::input::DeviceDescriptor descriptor,
    fidl::InterfaceRequest<fuchsia::ui::input::InputDevice>
        input_device_request) {
  uint32_t device_id = ++next_device_token_;

  std::unique_ptr<MockInputDevice> input_device =
      std::make_unique<MockInputDevice>(device_id, std::move(descriptor),
                                        std::move(input_device_request),
                                        on_report_callback_.share());

  MockInputDevice* input_device_ptr = input_device.get();
  devices_by_id_.emplace(device_id, std::move(input_device));

  if (on_device_callback_)
    on_device_callback_(input_device_ptr);
}

}  // namespace test
}  // namespace mozart
