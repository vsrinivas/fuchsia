// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/displays/display_watcher.h"

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
    callback(false, 0, 0, 0.f);
    return;
  }

  // Perform an ioctl to get display width and height.
  ioctl_display_get_fb_t description;
  ssize_t result = ioctl_display_get_fb(fd.get(), &description);
  if (result < 0) {
    FXL_DLOG(ERROR) << "IOCTL_DISPLAY_GET_FB failed: result=" << result;
    callback(false, 0, 0, 0.f);
    return;
  }
  zx_handle_close(description.vmo);  // we don't need the vmo

  // TODO(MZ-16): Need to have a database of ratios for different devices.
  // Given a target of 1 DP = 1/160 inch, we can directly compute this value in
  // cases where we know both the resolution and the physical dimensions of a
  // display, but we often don't know the latter.
  const uint32_t width = description.info.width;
  const uint32_t height = description.info.height;
  float device_pixel_ratio = 2.f;
  if (width == 2400 && height == 1600) {
    // We assume that the device is a Pixel.  Assuming a 12.246 inch screen with
    // square pixels, this gives a device-pixel ratio of 1.472.
    FXL_LOG(INFO) << "SceneManager: treating device as a Pixel.";
    device_pixel_ratio = 1.472134279;
  } else if (width == 2160 && height == 1440) {
    // We assume that the device is an Acer Switch 12 Alpha.  Assuming a 12.246
    // inch screen with square pixels, this gives a device-pixel ratio of 1.330.
    FXL_LOG(INFO) << "SceneManager: treating device as an Acer Switch 12.";
    // TODO(MZ-16): We've been asked to temporarily revert the DP-ratio to 2.0.
    // device_pixel_ratio = 1.329916454;
    device_pixel_ratio = 2.0;
  }

  // Invoke the callback, passing the display attributes.
  callback(true, width, height, device_pixel_ratio);
}

}  // namespace scene_manager
