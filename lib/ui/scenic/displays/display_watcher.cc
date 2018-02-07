// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/displays/display_watcher.h"

#include <fcntl.h>

#include <zircon/device/display.h>

#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"

namespace scene_manager {

static const std::string kDisplayDir = "/dev/class/display";

DisplayWatcher::DisplayWatcher() = default;

DisplayWatcher::~DisplayWatcher() = default;

void DisplayWatcher::WaitForDisplay(DisplayReadyCallback callback) {
  FXL_DCHECK(!device_watcher_);

  device_watcher_ = fsl::DeviceWatcher::Create(
      kDisplayDir,
      std::bind(&DisplayWatcher::HandleDevice, this, std::move(callback),
                std::placeholders::_1, std::placeholders::_2));
}

void DisplayWatcher::HandleDevice(DisplayReadyCallback callback,
                                  int dir_fd,
                                  std::string filename) {
  device_watcher_.reset();

  // Get display info.
  std::string path = kDisplayDir + "/" + filename;

  FXL_LOG(INFO) << "SceneManager: Acquired display " << path << ".";
  fxl::UniqueFD fd(open(path.c_str(), O_RDWR));
  if (!fd.is_valid()) {
    FXL_DLOG(ERROR) << "Failed to open " << path << ": errno=" << errno;
    callback(0, 0);
    return;
  }

  // TODO(MZ-386): Use a MagmaConnection instead of ioctl_display_get_fb_t.
  // Perform an ioctl to get display width and height.
  ioctl_display_get_fb_t description;
  ssize_t result = ioctl_display_get_fb(fd.get(), &description);
  if (result < 0) {
    FXL_DLOG(ERROR) << "IOCTL_DISPLAY_GET_FB failed: result=" << result;
    callback(0, 0);
    return;
  }
  zx_handle_close(description.vmo);  // we don't need the vmo

  callback(description.info.width, description.info.height);
}

}  // namespace scene_manager
