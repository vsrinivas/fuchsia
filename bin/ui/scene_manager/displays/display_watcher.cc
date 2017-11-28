// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/displays/display_watcher.h"

#include <fcntl.h>

#include <zircon/device/display.h>

#include "garnet/bin/ui/scene_manager/displays/display_configuration.h"
#include "garnet/bin/ui/scene_manager/displays/display_model.h"
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
    callback(nullptr);
    return;
  }

  // TODO(MZ-386): Use a MagmaConnection instead of ioctl_display_get_fb_t.
  // Perform an ioctl to get display width and height.
  ioctl_display_get_fb_t description;
  ssize_t result = ioctl_display_get_fb(fd.get(), &description);
  if (result < 0) {
    FXL_DLOG(ERROR) << "IOCTL_DISPLAY_GET_FB failed: result=" << result;
    callback(nullptr);
    return;
  }
  zx_handle_close(description.vmo);  // we don't need the vmo

  // Calculate the display metrics.
  DisplayModel model;
  ConfigureDisplay(description.info.width, description.info.height, &model);
  DisplayMetrics metrics = model.GetMetrics();
  FXL_DLOG(INFO) << "SceneManager: Display metrics: "
                 << "width_in_px=" << metrics.width_in_px()
                 << ", height_in_px=" << metrics.height_in_px()
                 << ", width_in_pp=" << metrics.width_in_pp()
                 << ", height_in_pp=" << metrics.height_in_pp()
                 << ", width_in_mm=" << metrics.width_in_mm()
                 << ", height_in_mm=" << metrics.height_in_mm()
                 << ", x_scale_in_px_per_pp=" << metrics.x_scale_in_px_per_pp()
                 << ", y_scale_in_px_per_pp=" << metrics.y_scale_in_px_per_pp()
                 << ", x_scale_in_pp_per_px=" << metrics.x_scale_in_pp_per_px()
                 << ", y_scale_in_pp_per_px=" << metrics.y_scale_in_pp_per_px()
                 << ", density_in_pp_per_mm=" << metrics.density_in_pp_per_mm()
                 << ", density_in_mm_per_pp=" << metrics.density_in_mm_per_pp();

  // TODO(MZ-16): We've been asked to temporarily revert the DP-ratio to 2.0.
  FXL_DLOG(INFO)
      << "SceneManager: Ignoring display metrics, using DP-ratio of 2.0.";
  DisplayMetrics fake_metrics = DisplayMetrics(
      metrics.width_in_px(), metrics.height_in_px(), 2.f, 2.f, 0.f);

  // Invoke the callback, passing the display metrics.
  callback(&fake_metrics);
}

}  // namespace scene_manager
