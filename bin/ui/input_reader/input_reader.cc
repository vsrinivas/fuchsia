// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/input_reader.h"

#include <fcntl.h>
#include <zircon/device/display.h>
#include <unistd.h>

#define DEV_INPUT "/dev/class/input"
#define DEV_CONSOLE "/dev/class/framebuffer"
#define DEV_VC "000"

namespace mozart {
namespace input {

InputReader::DeviceInfo::DeviceInfo(
    std::unique_ptr<InputInterpreter> interpreter,
    fsl::MessageLoop::HandlerKey key)
    : interpreter_(std::move(interpreter)), key_(key) {}

InputReader::DeviceInfo::~DeviceInfo() {}

InputReader::InputReader(mozart::InputDeviceRegistry* registry,
                         bool ignore_console)
    : registry_(registry), ignore_console_(ignore_console) {}

InputReader::~InputReader() {
  while (!devices_.empty()) {
    DeviceRemoved(devices_.begin()->first);
  }
  if (display_ownership_handler_key_) {
    fsl::MessageLoop::GetCurrent()->RemoveHandler(
        display_ownership_handler_key_);
  }
}

void InputReader::Start() {
  FXL_CHECK(registry_);

  device_watcher_ = fsl::DeviceWatcher::Create(
      DEV_INPUT, [this](int dir_fd, std::string filename) {
        std::unique_ptr<InputInterpreter> interpreter =
            InputInterpreter::Open(dir_fd, filename, registry_);
        if (interpreter)
          DeviceAdded(std::move(interpreter));
      });

  console_watcher_ = fsl::DeviceWatcher::Create(
      DEV_CONSOLE, [this](int dir_fd, std::string filename) {
        WatchDisplayOwnershipChanges(dir_fd);
      });
}

// Register to receive notifications that display ownership has changed
void InputReader::WatchDisplayOwnershipChanges(int dir_fd) {
  // Open gfx console's device and receive display_watcher through its ioctl
  int gfx_console_fd = openat(dir_fd, DEV_VC, O_RDWR);
  if (gfx_console_fd >= 0) {
    ssize_t result = ioctl_display_get_ownership_change_event(
        gfx_console_fd, &display_ownership_event_);
    if (result == sizeof(display_ownership_event_)) {
      // Add handler to listen for signals on this event
      zx_signals_t signals = ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1;
      display_ownership_handler_key_ =
          fsl::MessageLoop::GetCurrent()->AddHandler(
              this, display_ownership_event_, signals);
    } else {
      FXL_DLOG(ERROR)
          << "IOCTL_DISPLAY_GET_OWNERSHIP_CHANGE_EVENT failed: result="
          << result;
    }
    close(gfx_console_fd);
  } else {
    FXL_DLOG(ERROR) << "Failed to open " << DEV_VC << ": errno=" << errno;
  }
}

void InputReader::DeviceRemoved(zx_handle_t handle) {
  FXL_VLOG(1) << "Input device " << devices_.at(handle)->interpreter()->name()
              << " removed";
  fsl::MessageLoop::GetCurrent()->RemoveHandler(devices_.at(handle)->key());
  devices_.erase(handle);
}

void InputReader::DeviceAdded(std::unique_ptr<InputInterpreter> interpreter) {
  FXL_VLOG(1) << "Input device " << interpreter->name() << " added ";
  zx_handle_t handle = interpreter->handle();
  zx_signals_t signals = ZX_USER_SIGNAL_0;
  fsl::MessageLoop::HandlerKey key =
      fsl::MessageLoop::GetCurrent()->AddHandler(this, handle, signals);

  std::unique_ptr<DeviceInfo> info =
      std::make_unique<DeviceInfo>(std::move(interpreter), key);
  devices_.emplace(handle, std::move(info));
}

void InputReader::OnDeviceHandleReady(zx_handle_t handle,
                                      zx_signals_t pending) {
  InputInterpreter* interpreter = devices_[handle]->interpreter();
  if (pending & ZX_USER_SIGNAL_0) {
    bool ret = interpreter->Read(!display_owned_ && !ignore_console_);
    if (!ret) {
      DeviceRemoved(handle);
    }
  }
}

void InputReader::OnDisplayHandleReady(zx_handle_t handle,
                                       zx_signals_t pending) {
  fsl::MessageLoop::GetCurrent()->RemoveHandler(display_ownership_handler_key_);
  if (pending & ZX_USER_SIGNAL_0) {
    display_owned_ = false;
    display_ownership_handler_key_ = fsl::MessageLoop::GetCurrent()->AddHandler(
        this, display_ownership_event_, ZX_USER_SIGNAL_1);
  } else if (pending & ZX_USER_SIGNAL_1) {
    display_owned_ = true;
    display_ownership_handler_key_ = fsl::MessageLoop::GetCurrent()->AddHandler(
        this, display_ownership_event_, ZX_USER_SIGNAL_0);
  }
}

#pragma mark fsl::MessageLoopHandler
// |fsl::MessageLoopHandler|:

void InputReader::OnHandleReady(zx_handle_t handle,
                                zx_signals_t pending,
                                uint64_t count) {
  if (handle == display_ownership_event_) {
    OnDisplayHandleReady(handle, pending);
  } else if (devices_.count(handle)) {
    OnDeviceHandleReady(handle, pending);
  }
}

}  // namespace input
}  // namespace mozart
