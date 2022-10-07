// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_WATCHER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_WATCHER_H_

#include <fcntl.h>
#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/wire/transaction.h>
#include <zircon/errors.h>

#include <list>

#include "src/lib/fsl/io/device_watcher.h"

class DeviceWatcher : public fidl::WireServer<fuchsia_device_manager::DeviceWatcher> {
 public:
  DeviceWatcher(async_dispatcher_t* dispatcher, fbl::unique_fd fd)
      : watcher_(fsl::DeviceWatcher::CreateWithIdleCallback(
            std::move(fd), fit::bind_member<&DeviceWatcher::FdCallback>(this), [] {}, dispatcher)) {
  }

  void NextDevice(NextDeviceCompleter::Sync& completer) override {
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
    zx::channel client, server;
    if (const zx_status_t status = zx::channel::create(0, &client, &server); status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "failed to create channel";
      return;
    }
    if (const zx_status_t status =
            fdio_service_connect_at(fdio_cpp::UnownedFdioCaller(dir_fd).borrow_channel(),
                                    filename.c_str(), server.release());
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "failed to connect to device";
      return;
    }
    if (std::optional request = std::exchange(request_, {}); request.has_value()) {
      request.value().ReplySuccess(std::move(client));
    } else {
      channels_list_.push_back(std::move(client));
    }
  }

  const std::unique_ptr<fsl::DeviceWatcher> watcher_;
  std::optional<NextDeviceCompleter::Async> request_;
  std::list<zx::channel> channels_list_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_WATCHER_H_
