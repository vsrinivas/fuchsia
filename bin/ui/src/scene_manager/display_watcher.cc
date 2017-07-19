// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/display_watcher.h"

#include <fcntl.h>

#include <magenta/device/display.h>

#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"

namespace mozart {
namespace scene {

namespace {
const std::string kDisplayDir = "/dev/class/display";
constexpr float kHardcodedDevicePixelRatio = 2.f;
}  // namespace

DisplayWatcher::DisplayWatcher(OnDisplayReady callback) : callback_(callback) {}

std::unique_ptr<DisplayWatcher> DisplayWatcher::New(OnDisplayReady callback) {
  DisplayWatcher* display_watcher = new DisplayWatcher(callback);
  display_watcher->WaitForDisplay();
  return std::unique_ptr<DisplayWatcher>(display_watcher);
}

void DisplayWatcher::WaitForDisplay() {
  device_watcher_ = mtl::DeviceWatcher::Create(
      kDisplayDir, [this](int dir_fd, std::string filename) {
        // Get display info.
        std::string path = kDisplayDir + "/" + filename;

        FTL_LOG(INFO) << "SceneManager: Acquired display " << path << ".";
        int display_fd = open(path.c_str(), O_RDWR);
        if (display_fd < 0) {
          FTL_DLOG(ERROR) << "Failed to open " << path << ": errno=" << errno;
          callback_(false, 0, 0, 0.f);
          return;
        }
        ftl::UniqueFD fd(display_fd);

        // Perform an ioctl to get display width and height.
        ssize_t result;
        ioctl_display_get_fb_t description;
        result = ioctl_display_get_fb(fd.get(), &description);
        if (result < 0) {
          FTL_DLOG(ERROR) << "IOCTL_DISPLAY_GET_FB failed: result=" << result;
          callback_(false, 0, 0, 0.f);
          return;
        }

        // Invoke the callback, passing the display attributes.
        mx_display_info_t display_info = description.info;
        callback_(true, display_info.width, display_info.height,
                  kHardcodedDevicePixelRatio);

        // Clean up. Must be done after the callback.
        mx_handle_close(description.vmo);
        callback_ = nullptr;
        device_watcher_.reset();
      });
}

}  // namespace scene
}  // namespace mozart
