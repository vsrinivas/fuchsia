// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_reader/input_reader.h"

#define DEV_INPUT "/dev/class/input"

namespace mozart {
namespace input {

InputReader::DeviceInfo::DeviceInfo(
    std::unique_ptr<InputInterpreter> interpreter,
    mtl::MessageLoop::HandlerKey key)
    : interpreter_(std::move(interpreter)), key_(key) {}

InputReader::DeviceInfo::~DeviceInfo() {}

InputReader::InputReader(mozart::InputDeviceRegistry* registry)
    : registry_(registry) {}

InputReader::~InputReader() {
  while (!devices_.empty()) {
    DeviceRemoved(devices_.begin()->first);
  }
}

void InputReader::Start() {
  FTL_CHECK(registry_);

  device_watcher_ = mtl::DeviceWatcher::Create(
      DEV_INPUT, [this](int dir_fd, std::string filename) {
        std::unique_ptr<InputInterpreter> interpreter =
            InputInterpreter::Open(dir_fd, filename, registry_);
        if (interpreter)
          DeviceAdded(std::move(interpreter));
      });
}

void InputReader::DeviceRemoved(mx_handle_t handle) {
  FTL_VLOG(1) << "Input device " << devices_.at(handle)->interpreter()->name()
              << " removed";
  mtl::MessageLoop::GetCurrent()->RemoveHandler(devices_.at(handle)->key());
  devices_.erase(handle);
}

void InputReader::DeviceAdded(std::unique_ptr<InputInterpreter> interpreter) {
  FTL_VLOG(1) << "Input device " << interpreter->name() << " added ";
  mx_handle_t handle = interpreter->handle();
  mx_signals_t signals = MX_USER_SIGNAL_0;
  mtl::MessageLoop::HandlerKey key =
      mtl::MessageLoop::GetCurrent()->AddHandler(this, handle, signals);

  std::unique_ptr<DeviceInfo> info =
      std::make_unique<DeviceInfo>(std::move(interpreter), key);
  devices_.emplace(handle, std::move(info));
}

void InputReader::OnDeviceHandleReady(mx_handle_t handle,
                                      mx_signals_t pending) {
  InputInterpreter* interpreter = devices_[handle]->interpreter();
  if (pending & MX_USER_SIGNAL_0) {
    bool ret = interpreter->Read();
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
