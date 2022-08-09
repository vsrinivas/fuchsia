// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_USB_DEVICE_WATCHER_DEVICE_INSTANCE_H_
#define SRC_CAMERA_BIN_USB_DEVICE_WATCHER_DEVICE_INSTANCE_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/fpromise/result.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>
#include <zircon/status.h>

namespace camera {

// Represents a launched device process.
class DeviceInstance {
 public:
  static fpromise::result<std::unique_ptr<DeviceInstance>, zx_status_t> Create(
      fuchsia::hardware::camera::DeviceHandle camera, const fuchsia::component::RealmPtr& realm,
      async_dispatcher_t* dispatcher, const std::string& collection_name,
      const std::string& child_name, const std::string& url);

  const std::string& name() { return name_; }

 private:
  async_dispatcher_t* dispatcher_;
  std::string name_;
  fuchsia::hardware::camera::DevicePtr camera_;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_USB_DEVICE_WATCHER_DEVICE_INSTANCE_H_
