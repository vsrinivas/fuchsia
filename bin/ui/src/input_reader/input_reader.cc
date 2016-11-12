// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_reader/input_reader.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <magenta/device/console.h>
#include <magenta/device/device.h>
#include <magenta/device/input.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mxio/io.h>
#include <mxio/watcher.h>

#include "apps/mozart/src/input_reader/input_device.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/ftl/time/time_delta.h"

#define DEV_INPUT "/dev/class/input"

namespace mozart {
namespace input {

InputReader::InputReader(InputInterpreter* interpreter)
    : interpreter_(interpreter), main_loop_(mtl::MessageLoop::GetCurrent()) {}

InputReader::~InputReader() {
  if (input_directory_key_) {
    main_loop_->RemoveHandler(input_directory_key_);
  }
  if (input_directory_fd_) {
    close(input_directory_fd_);
  }
  while (!devices_.empty()) {
    DeviceRemoved(devices_.begin()->first);
  }
}

void InputReader::MonitorDirectory() {
  input_directory_fd_ = open(DEV_INPUT, O_DIRECTORY | O_RDONLY);
  if (input_directory_fd_ < 0) {
    FTL_LOG(ERROR) << "Error opening " << DEV_INPUT;
    return;
  }

  // First off, check current content of DEV_INPUT
  DIR* dir;
  int fd;
  if ((fd = openat(input_directory_fd_, ".", O_RDONLY | O_DIRECTORY)) < 0) {
    FTL_LOG(ERROR) << "Error opening directory " << DEV_INPUT;
    return;
  }
  if ((dir = fdopendir(fd)) == NULL) {
    FTL_LOG(ERROR) << "Failed to open directory " << DEV_INPUT;
    return;
  }

  struct dirent* de;
  while ((de = readdir(dir)) != NULL) {
    if (de->d_name[0] == '.') {
      if (de->d_name[1] == 0) {
        continue;
      }
      if ((de->d_name[1] == '.') && (de->d_name[2] == 0)) {
        continue;
      }
    }
    std::unique_ptr<InputDevice> device =
        InputDevice::Open(input_directory_fd_, de->d_name);
    if (device) {
      DeviceAdded(std::move(device));
    }
  }
  closedir(dir);

  // Second, monitor DEV_INPUT for events
  mx_handle_t handle;
  ssize_t r = ioctl_device_watch_dir(input_directory_fd_, &handle);
  if (r < 0) {
    return;
  }
  input_directory_channel_.reset(handle);

  mx_signals_t signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
  input_directory_key_ = main_loop_->AddHandler(
      this, input_directory_channel_.get(), signals, ftl::TimeDelta::Max());
}

void InputReader::Start() {
  // Check content of /dev/input and add handle to monitor changes
  main_loop_->task_runner()->PostTask([this] { MonitorDirectory(); });
}

void InputReader::DeviceRemoved(mx_handle_t handle) {
  FTL_LOG(INFO) << "Input device " << devices_.at(handle).first->name()
                << " removed";
  main_loop_->RemoveHandler(devices_.at(handle).second);
  interpreter_->UnregisterDevice(devices_[handle].first.get());
  devices_.erase(handle);
}

void InputReader::DeviceAdded(std::unique_ptr<InputDevice> device) {
  FTL_LOG(INFO) << "Input device " << device->name() << " added ";
  mx_handle_t handle = device->handle();
  mx_signals_t signals = MX_USER_SIGNAL_0;
  mtl::MessageLoop::HandlerKey key =
      main_loop_->AddHandler(this, handle, signals, ftl::TimeDelta::Max());
  interpreter_->RegisterDevice(device.get());
  devices_[handle] = std::make_pair(std::move(device), key);
}

void InputReader::OnDirectoryHandleReady(mx_handle_t handle,
                                         mx_signals_t pending) {
  if (pending & MX_SIGNAL_READABLE) {
    mx_status_t status;
    uint32_t sz = MXIO_MAX_FILENAME;
    char name[MXIO_MAX_FILENAME + 1];
    if ((status = input_directory_channel_.read(0, name, sz, &sz, nullptr, 0,
                                                nullptr)) < 0) {
      FTL_LOG(ERROR) << "Failed to read from " << DEV_INPUT;
      return;
    }
    name[sz] = 0;
    std::unique_ptr<InputDevice> device =
        InputDevice::Open(input_directory_fd_, name);
    if (device)
      DeviceAdded(std::move(device));
  } else if (pending & MX_SIGNAL_PEER_CLOSED) {
    FTL_CHECK(false) << "Input device directory disappeared; input is broken";
  }
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
  if (input_directory_channel_.get() == handle) {
    OnDirectoryHandleReady(handle, pending);
  } else if (devices_.count(handle)) {
    OnDeviceHandleReady(handle, pending);
  }
}

}  // namespace input
}  // namespace mozart
