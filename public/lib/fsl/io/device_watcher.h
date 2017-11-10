// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_IO_DEVICE_WATCHER_H_
#define LIB_FSL_IO_DEVICE_WATCHER_H_

#include <async/auto_wait.h>
#include <zx/channel.h>

#include <functional>
#include <memory>
#include <string>

#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/fxl_export.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace fsl {

// Watches for devices to be registered in devfs.
//
// TODO(jeffbrown): Generalize to watching arbitrary directories or dealing
// with removal when fdio has a protocol for it.
class FXL_EXPORT DeviceWatcher {
 public:
  // Callback function which is invoked whenever a device is found.
  // |dir_fd| is the file descriptor of the directory (use for openat()).
  // |filename| is the name of the file relative to the directory.
  using Callback = std::function<void(int dir_fd, std::string filename)>;

  ~DeviceWatcher() = default;

  // Creates a device watcher associated with the current message loop.
  //
  // Asynchronously invokes |callback| for all existing devices within the
  // specified directory as well as any subsequently added devices until
  // the device watcher is destroyed.
  static std::unique_ptr<DeviceWatcher> Create(std::string directory_path,
                                               Callback callback);

 private:
  DeviceWatcher(fxl::UniqueFD dir_fd, zx::channel dir_watch, Callback callback);

  static void ListDevices(fxl::WeakPtr<DeviceWatcher> weak, int dir_fd);

  async_wait_result_t Handler(async_t* async, zx_status_t status,
                              const zx_packet_signal* signal);

  fxl::UniqueFD dir_fd_;
  zx::channel dir_watch_;
  Callback callback_;
  async::AutoWait wait_;
  fxl::WeakPtrFactory<DeviceWatcher> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceWatcher);
};

}  // namespace fsl

#endif  // LIB_FSL_IO_DEVICE_WATCHER_H_
