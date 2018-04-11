// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/displays/display_watcher.h"

#include <fcntl.h>

#include <zircon/device/display.h>

#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"

namespace scenic {
namespace gfx {

static const std::string kDisplayDir = "/dev/class/display";
static const std::string kFramebufferDir = "/dev/class/framebuffer";

DisplayWatcher::DisplayWatcher() = default;

DisplayWatcher::~DisplayWatcher() = default;

void DisplayWatcher::WaitForDisplay(DisplayReadyCallback callback) {
  FXL_DCHECK(!display_watcher_);
#if SCENE_MANAGER_VULKAN_SWAPCHAIN == 2
  // This is just for testing, so notify that there's a fake display that's
  // 800x608. Without a display the scene manager won't try to draw anything.
  callback(800, 608, zx::event());
#else
  callback_ = std::move(callback);
  display_watcher_ = fsl::DeviceWatcher::Create(
      kDisplayDir,
      std::bind(&DisplayWatcher::HandleDevice, this, true,
                std::placeholders::_1, std::placeholders::_2));
  framebuffer_watcher_ = fsl::DeviceWatcher::Create(
      kFramebufferDir,
      std::bind(&DisplayWatcher::HandleDevice, this, false,
                std::placeholders::_1, std::placeholders::_2));
#endif
}

void DisplayWatcher::HandleDevice(bool display,
                                  int dir_fd,
                                  std::string filename) {
  if (display) {
    display_watcher_.reset();
  } else {
    framebuffer_watcher_.reset();
  }

  // Get display info.
  std::string path = (display ? kDisplayDir : kFramebufferDir) + "/" + filename;

  FXL_LOG(INFO) << "SceneManager: Acquired " <<
      (display ? "display" : "framebuffer") << " " << path << ".";
  fxl::UniqueFD fd(open(path.c_str(), O_RDWR));
  if (!fd.is_valid()) {
    FXL_DLOG(ERROR) << "Failed to open " << path << ": errno=" << errno;
    callback_(0, 0, zx::event());
    return;
  }

  if (display) {
    display_fd_ = std::move(fd);
  } else {
    framebuffer_fd_ = std::move(fd);
  }

  // Don't proceed until we've acquired both the display and framebuffer.
  if (!display_fd_.is_valid() || !framebuffer_fd_.is_valid()) {
    return;
  }

  // TODO(MZ-386): Use a MagmaConnection instead of ioctl_display_get_fb_t.
  // Perform an ioctl to get display width and height.
  ioctl_display_get_fb_t description;
  ssize_t result = ioctl_display_get_fb(display_fd_.get(), &description);
  if (result < 0) {
    FXL_DLOG(ERROR) << "IOCTL_DISPLAY_GET_FB failed: result=" << result;
    callback_(0, 0, zx::event());
    return;
  }
  zx_handle_close(description.vmo);  // we don't need the vmo

  zx::event ownership_event;
  result = ioctl_display_get_ownership_change_event(
      framebuffer_fd_.get(), ownership_event.reset_and_get_address());
  if (result == sizeof(zx_handle_t)) {
    FXL_DLOG(WARNING) << "IOCTL_DISPLAY_GET_OWNERSHIP_CHANGE_EVENT failed: "
        << "result=" << result;
  }

  callback_(description.info.width, description.info.height,
           std::move(ownership_event));
}

}  // namespace gfx
}  // namespace scenic
