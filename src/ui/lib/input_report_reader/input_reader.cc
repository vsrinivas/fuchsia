// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_report_reader/input_reader.h"

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async/default.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/input_report_reader/fdio_device_watcher.h"
#include "src/ui/lib/input_report_reader/input_interpreter.h"

namespace ui_input {

namespace {

constexpr char kInputDevPath[] = "/dev/class/input-report";

}

struct InputReader::DeviceInfo {
  std::unique_ptr<InputInterpreter> interpreter;
  std::unique_ptr<async::WaitMethod<InputReader, &InputReader::OnDeviceHandleReady>> waiter;
};

InputReader::InputReader(fuchsia::ui::input::InputDeviceRegistry* registry, bool ignore_console)
    : registry_(registry), ignore_console_(ignore_console) {
  FXL_CHECK(registry_);
}

InputReader::~InputReader() = default;

void InputReader::Start() { Start(std::make_unique<FdioDeviceWatcher>(kInputDevPath)); }

void InputReader::Start(std::unique_ptr<DeviceWatcher> device_watcher) {
  device_watcher_ = std::move(device_watcher);
  device_watcher_->Watch([this](zx::channel channel) {
    auto interpreter = std::make_unique<InputInterpreter>(std::move(channel), registry_,
                                                          std::to_string(next_interpreter_id_++));
    if (interpreter->Initialize()) {
      DeviceAdded(std::move(interpreter));
    }
  });
}

// Register to receive notifications that display ownership has changed
void InputReader::SetOwnershipEvent(zx::event event) {
  display_ownership_waiter_.Cancel();

  display_ownership_event_ = std::move(event);

  // Add handler to listen for signals on this event
  zx_signals_t signals =
      fuchsia::ui::scenic::displayOwnedSignal | fuchsia::ui::scenic::displayNotOwnedSignal;
  display_ownership_waiter_.set_object(display_ownership_event_.get());
  display_ownership_waiter_.set_trigger(signals);
  zx_status_t status = display_ownership_waiter_.Begin(async_get_default_dispatcher());
  FXL_DCHECK(status == ZX_OK) << "Status is: " << status;
}

void InputReader::DeviceRemoved(zx_handle_t handle) {
  FXL_VLOG(1) << "Input device " << devices_.at(handle)->interpreter->name() << " removed";
  devices_.erase(handle);
}

void InputReader::DeviceAdded(std::unique_ptr<InputInterpreter> interpreter) {
  FXL_DCHECK(interpreter);

  FXL_VLOG(1) << "Input device " << interpreter->name() << " added ";
  zx_handle_t handle = interpreter->handle();

  auto wait = std::make_unique<async::WaitMethod<InputReader, &InputReader::OnDeviceHandleReady>>(
      this, handle, ZX_USER_SIGNAL_0);

  zx_status_t status = wait->Begin(async_get_default_dispatcher());
  FXL_CHECK(status == ZX_OK);

  devices_.emplace(handle, new DeviceInfo{std::move(interpreter), std::move(wait)});
}

void InputReader::OnDeviceHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                      zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "InputReader::OnDeviceHandleReady received an error status code: " << status;
    return;
  }

  zx_signals_t pending = signal->observed;
  FXL_DCHECK(pending & ZX_USER_SIGNAL_0);

  bool discard = !(display_owned_ || ignore_console_);
  bool ret = devices_[wait->object()]->interpreter->Read(discard);
  if (!ret) {
    // This will destroy the waiter.
    DeviceRemoved(wait->object());
    return;
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "InputReader::OnDeviceHandleReady wait failed: " << status;
  }
}

void InputReader::OnDisplayHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                       zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "InputReader::OnDisplayHandleReady received an error status code: " << status;
    return;
  }

  zx_signals_t pending = signal->observed;
  if (pending & fuchsia::ui::scenic::displayNotOwnedSignal) {
    display_owned_ = false;
    display_ownership_waiter_.set_trigger(fuchsia::ui::scenic::displayOwnedSignal);
    auto waiter_status = display_ownership_waiter_.Begin(dispatcher);
    FXL_CHECK(waiter_status == ZX_OK);
  } else if (pending & fuchsia::ui::scenic::displayOwnedSignal) {
    display_owned_ = true;
    display_ownership_waiter_.set_trigger(fuchsia::ui::scenic::displayNotOwnedSignal);
    auto waiter_status = display_ownership_waiter_.Begin(dispatcher);
    FXL_CHECK(waiter_status == ZX_OK);
  }
}

}  // namespace ui_input
