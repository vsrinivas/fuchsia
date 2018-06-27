// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/displays/display_watcher.h"

#include <fcntl.h>

#include <lib/fidl/cpp/message.h>
#include <zircon/device/display-controller.h>

#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"

namespace scenic {
namespace gfx {

static const std::string kDisplayDir = "/dev/class/display-controller";

DisplayWatcher::DisplayWatcher() = default;

DisplayWatcher::~DisplayWatcher() = default;

void DisplayWatcher::WaitForDisplay(DisplayReadyCallback callback) {
  FXL_DCHECK(!device_watcher_);
  // See declare_args() in lib/ui/gfx/BUILD.gn
#if SCENIC_VULKAN_SWAPCHAIN == 2
  // This is just for testing, so notify that there's a fake display.
  callback(fxl::UniqueFD(-1), ZX_HANDLE_INVALID, ZX_HANDLE_INVALID);
#else
  device_watcher_ = fsl::DeviceWatcher::Create(
      kDisplayDir, [this, callback = std::move(callback)](
                       int dir_fd, std::string filename) mutable {
        HandleDevice(std::move(callback), dir_fd, filename);
      });
#endif
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
}  // namespace scenic
