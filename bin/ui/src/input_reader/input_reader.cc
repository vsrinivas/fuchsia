// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_reader/input_reader.h"

#define DEV_INPUT "/dev/class/input"

namespace mozart {
namespace input {

InputReader::InputReader(InputInterpreter* interpreter)
    : interpreter_(interpreter) {}

InputReader::~InputReader() {
  while (!devices_.empty()) {
    DeviceRemoved(devices_.begin()->first);
  }
}

void InputReader::Start() {
  device_watcher_ = mtl::DeviceWatcher::Create(
      DEV_INPUT, [this](int dir_fd, std::string filename) {
        if (device_ids_.count(filename) == 0) {
          device_ids_[filename] = device_ids_.size() + 1;
        }
        std::unique_ptr<InputDevice> device =
            InputDevice::Open(dir_fd, filename, device_ids_[filename]);
        if (device)
          DeviceAdded(std::move(device));
      });
}

void InputReader::DeviceRemoved(mx_handle_t handle) {
  FTL_VLOG(1) << "Input device " << devices_.at(handle).first->name()
              << " removed";
  mtl::MessageLoop::GetCurrent()->RemoveHandler(devices_.at(handle).second);
  interpreter_->UnregisterDevice(devices_[handle].first.get());
  devices_.erase(handle);
}

void InputReader::DeviceAdded(std::unique_ptr<InputDevice> device) {
  FTL_VLOG(1) << "Input device " << device->name() << " added ";
  mx_handle_t handle = device->handle();
  mx_signals_t signals = MX_USER_SIGNAL_0;
  mtl::MessageLoop::HandlerKey key =
      mtl::MessageLoop::GetCurrent()->AddHandler(this, handle, signals);
  interpreter_->RegisterDevice(device.get());
  devices_.emplace(handle, std::make_pair(std::move(device), key));
}

void InputReader::OnDeviceHandleReady(mx_handle_t handle,
                                      mx_signals_t pending) {
  InputDevice* device = devices_[handle].first.get();
  if (pending & MX_USER_SIGNAL_0) {
    bool ret = device->Read([this, device](InputReport::ReportType type) {
      interpreter_->OnReport(device, type);
    });
    if (!ret) {
      DeviceRemoved(handle);
    }
  }
}

#pragma mark mtl::MessageLoopHandler
// |mtl::MessageLoopHandler|:

void InputReader::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  if (devices_.count(handle)) {
    OnDeviceHandleReady(handle, pending);
  }
}

}  // namespace input
}  // namespace mozart
