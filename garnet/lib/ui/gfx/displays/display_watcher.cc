// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/displays/display_watcher.h"

#include <fcntl.h>

#include <lib/fidl/cpp/message.h>
#include <zircon/device/display-controller.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/files/unique_fd.h"

namespace scenic_impl {
namespace gfx {

static const std::string kDisplayDir = "/dev/class/display-controller";

DisplayWatcher::DisplayWatcher() = default;

DisplayWatcher::~DisplayWatcher() = default;

void DisplayWatcher::WaitForDisplay(DisplayReadyCallback callback) {
  FXL_DCHECK(!device_watcher_);
  device_watcher_ = fsl::DeviceWatcher::Create(
      kDisplayDir, [this, callback = std::move(callback)](
                       int dir_fd, std::string filename) mutable {
        HandleDevice(std::move(callback), dir_fd, filename);
      });
}

void DisplayWatcher::HandleDevice(DisplayReadyCallback callback, int dir_fd,
                                  std::string filename) {
  device_watcher_.reset();

  // Get display info.
  std::string path = kDisplayDir + "/" + filename;

  FXL_LOG(INFO) << "Scenic: Acquired display controller " << path << ".("
                << filename << ")";
  fxl::UniqueFD fd(open(path.c_str(), O_RDWR));
  if (!fd.is_valid()) {
    FXL_DLOG(ERROR) << "Failed to open " << path << ": errno=" << errno;
    callback(fxl::UniqueFD(), zx::channel());
    return;
  }

  zx::channel dc_handle;
  if (ioctl_display_controller_get_handle(
          fd.get(), dc_handle.reset_and_get_address()) != sizeof(zx_handle_t)) {
    FXL_DLOG(ERROR) << "Failed to get device channel";
    callback(fxl::UniqueFD(), zx::channel());
    return;
  }

  callback(std::move(fd), std::move(dc_handle));
}

}  // namespace gfx
}  // namespace scenic_impl
