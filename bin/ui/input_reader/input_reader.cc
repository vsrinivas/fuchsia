// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/input_reader.h"

#include <fcntl.h>
#include <unistd.h>
#include <zircon/device/display.h>
#include "lib/fsl/tasks/message_loop.h"

#define DEV_INPUT "/dev/class/input"
#define DEV_CONSOLE "/dev/class/framebuffer"
#define DEV_VC "000"

namespace mozart {
namespace input {

InputReader::DeviceInfo::DeviceInfo(
    std::unique_ptr<InputInterpreter> interpreter,
    std::unique_ptr<async::AutoWait> waiter)
    : interpreter_(std::move(interpreter)), waiter_(std::move(waiter)) {}

InputReader::DeviceInfo::~DeviceInfo() {}

InputReader::InputReader(mozart::InputDeviceRegistry* registry,
                         bool ignore_console)
    : registry_(registry), ignore_console_(ignore_console) {}

InputReader::~InputReader() {
  while (!devices_.empty()) {
    DeviceRemoved(devices_.begin()->first);
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
      display_ownership_waiter_ = std::make_unique<async::AutoWait>(
          fsl::MessageLoop::GetCurrent()->async(), display_ownership_event_,
          signals);

      display_ownership_waiter_->set_handler(
          std::bind(&InputReader::OnDisplayHandleReady, this,
                    std::placeholders::_2, std::placeholders::_3));
      zx_status_t status = display_ownership_waiter_->Begin();
      FXL_CHECK(status == ZX_OK);
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
  devices_.erase(handle);
}

void InputReader::DeviceAdded(std::unique_ptr<InputInterpreter> interpreter) {
  FXL_VLOG(1) << "Input device " << interpreter->name() << " added ";
  zx_handle_t handle = interpreter->handle();
  zx_signals_t signals = ZX_USER_SIGNAL_0;

  auto wait = std::make_unique<async::AutoWait>(
      fsl::MessageLoop::GetCurrent()->async(), handle, signals);
  wait->set_handler(std::bind(&InputReader::OnDeviceHandleReady, this, handle,
                              std::placeholders::_2, std::placeholders::_3));

  zx_status_t status = wait->Begin();
  FXL_CHECK(status == ZX_OK);

  std::unique_ptr<DeviceInfo> info =
      std::make_unique<DeviceInfo>(std::move(interpreter), std::move(wait));
  devices_.emplace(handle, std::move(info));
}

async_wait_result_t InputReader::OnDeviceHandleReady(
    zx_handle_t handle,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "InputReader::OnDeviceHandleReady received an error status code: "
        << status;
    return ASYNC_WAIT_FINISHED;
  }

  zx_signals_t pending = signal->observed;
  InputInterpreter* interpreter = devices_[handle]->interpreter();

  FXL_DCHECK(pending & ZX_USER_SIGNAL_0);
  bool ret = interpreter->Read(!display_owned_ && !ignore_console_);
  if (!ret) {
    // This will destroy the waiter.
    DeviceRemoved(handle);
    return ASYNC_WAIT_FINISHED;
  }

  return ASYNC_WAIT_AGAIN;
}

async_wait_result_t InputReader::OnDisplayHandleReady(
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "InputReader::OnDisplayHandleReady received an error status code: "
        << status;
    return ASYNC_WAIT_FINISHED;
  }

  zx_signals_t pending = signal->observed;
  if (pending & ZX_USER_SIGNAL_0) {
    display_owned_ = false;
    display_ownership_waiter_ = std::make_unique<async::AutoWait>(
        fsl::MessageLoop::GetCurrent()->async(), display_ownership_event_,
        ZX_USER_SIGNAL_1);
    display_ownership_waiter_->set_handler(
        std::bind(&InputReader::OnDisplayHandleReady, this,
                  std::placeholders::_2, std::placeholders::_3));
    zx_status_t waiter_status = display_ownership_waiter_->Begin();
    FXL_CHECK(waiter_status == ZX_OK);
  } else if (pending & ZX_USER_SIGNAL_1) {
    display_owned_ = true;
    display_ownership_waiter_ = std::make_unique<async::AutoWait>(
        fsl::MessageLoop::GetCurrent()->async(), display_ownership_event_,
        ZX_USER_SIGNAL_0);
    display_ownership_waiter_->set_handler(
        std::bind(&InputReader::OnDisplayHandleReady, this,
                  std::placeholders::_2, std::placeholders::_3));
    zx_status_t waiter_status = display_ownership_waiter_->Begin();
    FXL_CHECK(waiter_status == ZX_OK);
  }

  return ASYNC_WAIT_FINISHED;
}

}  // namespace input
}  // namespace mozart
