// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/displays/display_watcher.h"

#include <fcntl.h>

#include <magenta/device/display.h>

#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"

namespace scene_manager {

namespace {
const std::string kDisplayDir = "/dev/class/display";

// TODO(MZ-16): Need to specify different device pixel ratio for NUC vs.
// Acer Switch 12.
constexpr float kHardcodedDevicePixelRatio = 2.f;
}  // namespace

DisplayWatcher::DisplayWatcher() = default;

DisplayWatcher::~DisplayWatcher() = default;

void DisplayWatcher::WaitForDisplay(DisplayReadyCallback callback) {
  FTL_DCHECK(!device_watcher_);

  device_watcher_ = mtl::DeviceWatcher::Create(
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

  FTL_LOG(INFO) << "SceneManager: Acquired display " << path << ".";
  ftl::UniqueFD fd(open(path.c_str(), O_RDWR));
  if (!fd.is_valid()) {
    FTL_DLOG(ERROR) << "Failed to open " << path << ": errno=" << errno;
    callback(false, 0, 0, 0.f);
    return;
  }

  // Perform an ioctl to get display width and height.
  ioctl_display_get_fb_t description;
  ssize_t result = ioctl_display_get_fb(fd.get(), &description);
  if (result < 0) {
    FTL_DLOG(ERROR) << "IOCTL_DISPLAY_GET_FB failed: result=" << result;
    callback(false, 0, 0, 0.f);
    return;
  }
  mx_handle_close(description.vmo);  // we don't need the vmo

  // Invoke the callback, passing the display attributes.
  callback(true, description.info.width, description.info.height,
           kHardcodedDevicePixelRatio);
}

}  // namespace scene_manager
