// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_TESTS_MOCKS_MOCK_INPUT_DEVICE_REGISTRY_H_
#define LIB_UI_TESTS_MOCKS_MOCK_INPUT_DEVICE_REGISTRY_H_

#include <memory>
#include <unordered_map>

#include <fuchsia/ui/input/cpp/fidl.h>
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/macros.h"
#include "lib/ui/tests/mocks/mock_input_device.h"

namespace mozart {
namespace test {

using OnDeviceCallback = std::function<void(MockInputDevice*)>;

class MockInputDeviceRegistry : public fuchsia::ui::input::InputDeviceRegistry {
 public:
  MockInputDeviceRegistry(OnDeviceCallback on_device_callback,
                          OnReportCallback on_report_callback);
  ~MockInputDeviceRegistry();

  // |InputDeviceRegistry|:
  void RegisterDevice(fuchsia::ui::input::DeviceDescriptor descriptor,
                      fidl::InterfaceRequest<fuchsia::ui::input::InputDevice>
                          input_device_request) override;

 private:
  OnDeviceCallback on_device_callback_;
  OnReportCallback on_report_callback_;

  uint32_t next_device_token_ = 0;
  std::unordered_map<uint32_t, std::unique_ptr<MockInputDevice>> devices_by_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockInputDeviceRegistry);
};

}  // namespace test
}  // namespace mozart

#endif  // LIB_UI_TESTS_MOCKS_MOCK_INPUT_DEVICE_REGISTRY_H_
