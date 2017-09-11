// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_IO_DEVICE_WATCHER_H_
#define LIB_MTL_IO_DEVICE_WATCHER_H_

#include <mx/channel.h>

#include <functional>
#include <memory>
#include <string>

#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/fxl_export.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

namespace mtl {

// Watches for devices to be registered in devfs.
//
// TODO(jeffbrown): Generalize to watching arbitrary directories or dealing
// with removal when mxio has a protocol for it.
class FXL_EXPORT DeviceWatcher : private mtl::MessageLoopHandler {
 public:
  // Callback function which is invoked whenever a device is found.
  // |dir_fd| is the file descriptor of the directory (use for openat()).
  // |filename| is the name of the file relative to the directory.
  using Callback = std::function<void(int dir_fd, std::string filename)>;

  ~DeviceWatcher();

  // Creates a device watcher associated with the current message loop.
  //
  // Asynchronously invokes |callback| for all existing devices within the
  // specified directory as well as any subsequently added devices until
  // the device watcher is destroyed.
  static std::unique_ptr<DeviceWatcher> Create(std::string directory_path,
                                               Callback callback);

 private:
  DeviceWatcher(fxl::UniqueFD dir_fd, mx::channel dir_watch, Callback callback);

  static void ListDevices(fxl::WeakPtr<DeviceWatcher> weak, int dir_fd);

  // |MessageLoopHandler|:
  void OnHandleReady(mx_handle_t handle,
                     mx_signals_t pending,
                     uint64_t count) override;

  fxl::UniqueFD dir_fd_;
  mx::channel dir_watch_;
  Callback callback_;
  mtl::MessageLoop::HandlerKey handler_key_;
  fxl::WeakPtrFactory<DeviceWatcher> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceWatcher);
};

}  // namespace mtl

#endif  // LIB_MTL_IO_DEVICE_WATCHER_H_
