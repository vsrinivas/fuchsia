// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/input/device_state.h"
#include "lib/ui/input/input_device_impl.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/input/fidl/input_device_registry.fidl.h"
#include "garnet/bin/ui/input_reader/input_reader.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/mtl/tasks/message_loop.h"

namespace print_input {

class App : public mozart::InputDeviceRegistry,
            public mozart::InputDeviceImpl::Listener {
 public:
  App() : reader_(this, true) { reader_.Start(); }
  ~App() {}

  void OnDeviceDisconnected(mozart::InputDeviceImpl* input_device) {
    FXL_VLOG(1) << "UnregisterDevice " << input_device->id();

    if (devices_.count(input_device->id()) != 0) {
      devices_[input_device->id()].second->OnUnregistered();
      devices_.erase(input_device->id());
    }
  }

  void OnReport(mozart::InputDeviceImpl* input_device,
                mozart::InputReportPtr report) {
    FXL_VLOG(2) << "DispatchReport " << input_device->id() << " " << *report;
    if (devices_.count(input_device->id()) == 0) {
      FXL_VLOG(1) << "DispatchReport: Unknown device " << input_device->id();
      return;
    }

    mozart::Size size;
    size.width = 100.0;
    size.height = 100.0;

    mozart::DeviceState* state = devices_[input_device->id()].second.get();

    FXL_CHECK(state);
    state->Update(std::move(report), size);
  }

 private:
  void RegisterDevice(
      mozart::DeviceDescriptorPtr descriptor,
      fidl::InterfaceRequest<mozart::InputDevice> input_device_request) {
    uint32_t device_id = next_device_token_++;

    FXL_VLOG(1) << "RegisterDevice " << *descriptor << " -> " << device_id;

    FXL_CHECK(devices_.count(device_id) == 0);

    std::unique_ptr<mozart::InputDeviceImpl> input_device =
        std::make_unique<mozart::InputDeviceImpl>(
            device_id, std::move(descriptor), std::move(input_device_request),
            this);

    std::unique_ptr<mozart::DeviceState> state =
        std::make_unique<mozart::DeviceState>(
            input_device->id(), input_device->descriptor(),
            [this](mozart::InputEventPtr event) { OnEvent(std::move(event)); });
    mozart::DeviceState* state_ptr = state.get();
    auto device_pair =
        std::make_pair(std::move(input_device), std::move(state));
    devices_.emplace(device_id, std::move(device_pair));
    state_ptr->OnRegistered();
  }

  void OnEvent(mozart::InputEventPtr event) { FXL_LOG(INFO) << *event; }

  uint32_t next_device_token_ = 0;
  mozart::input::InputReader reader_;
  std::unordered_map<uint32_t,
                     std::pair<std::unique_ptr<mozart::InputDeviceImpl>,
                               std::unique_ptr<mozart::DeviceState>>>
      devices_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace print_input

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  mtl::MessageLoop message_loop;
  print_input::App app;
  message_loop.Run();
  return 0;
}
