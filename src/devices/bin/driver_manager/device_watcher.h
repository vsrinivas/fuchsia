// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_WATCHER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_WATCHER_H_

#include <fcntl.h>
#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <zircon/errors.h>

#include <list>

#include "lib/fidl/llcpp/transaction.h"
#include "src/lib/fsl/io/device_watcher.h"

class DeviceWatcher : public fidl::WireServer<fuchsia_device_manager::DeviceWatcher> {
 public:
  DeviceWatcher(std::string dir_path, async_dispatcher_t* dispatcher) {
    dir_path_ = std::move(dir_path);
    dispatcher_ = dispatcher;
  }

  void NextDevice(NextDeviceRequestView request, NextDeviceCompleter::Sync& completer) override {
    if (watcher_ == nullptr) {
      fbl::unique_fd fd;
      zx_status_t status =
          fdio_open_fd(dir_path_.c_str(),
                       fuchsia_io::wire::kOpenRightWritable | fuchsia_io::wire::kOpenRightReadable,
                       fd.reset_and_get_address());
      if (status != ZX_OK) {
        completer.ReplyError(status);
        return;
      }
      watcher_ = fsl::DeviceWatcher::CreateWithIdleCallback(
          std::move(fd), fit::bind_member<&DeviceWatcher::FdCallback>(this), [] {}, dispatcher_);
    }
    if (request_) {
      completer.ReplyError(ZX_ERR_ALREADY_BOUND);
      return;
    }
    if (channels_list_.empty()) {
      request_ = completer.ToAsync();
      return;
    }
    completer.ReplySuccess(std::move(channels_list_.front()));
    channels_list_.pop_front();
  }

 private:
  void FdCallback(int dir_fd, const std::string& filename) {
    int fd = 0;
    zx_status_t status = fdio_open_fd_at(dir_fd, filename.c_str(), O_RDONLY, &fd);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to open device " << filename;
      return;
    }
    zx::channel channel;
    status = fdio_get_service_handle(fd, channel.reset_and_get_address());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to get service handle " << filename;
      return;
    }

    if (request_) {
      request_->ReplySuccess(std::move(channel));
      request_.reset();
      return;
    }
    channels_list_.push_back(std::move(channel));
  }
  std::optional<NextDeviceCompleter::Async> request_;
  std::unique_ptr<fsl::DeviceWatcher> watcher_;
  std::list<zx::channel> channels_list_;
  std::string dir_path_;
  async_dispatcher_t* dispatcher_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_WATCHER_H_
