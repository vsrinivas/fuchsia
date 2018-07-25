// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/camera/camera_client/camera_client.h"

#include <fcntl.h>
#include <lib/fxl/files/unique_fd.h>
#include "lib/fxl/strings/string_printf.h"

namespace camera {

Client::Client() : Client(component::StartupContext::CreateFromStartupInfo()) {}

Client::Client(std::unique_ptr<component::StartupContext> context)
    : context_(std::move(context)) {}

fuchsia::camera::driver::ControlSyncPtr& Client::camera() {
  return camera_control_;
}

zx_status_t Client::Open(int dev_id) {
  std::string dev_path = fxl::StringPrintf("/dev/class/camera/%03u", dev_id);
  fxl::UniqueFD dev_node(::open(dev_path.c_str(), O_RDONLY));
  if (!dev_node.is_valid()) {
    FXL_LOG(ERROR) << "Client::Open failed to open device node at \""
                   << dev_path << "\". (" << strerror(errno) << " : " << errno
                   << ")";
    return ZX_ERR_IO;
  }

  zx::channel channel;
  ssize_t res =
      ioctl_camera_get_channel(dev_node.get(), channel.reset_and_get_address());
  if (res < 0) {
    FXL_LOG(ERROR) << "Failed to obtain channel (res " << res << ")";
    return static_cast<zx_status_t>(res);
  }

  camera().Bind(std::move(channel));

  return ZX_OK;
}

}  // namespace camera
