// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/displays/display_watcher.h"

#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <fuchsia/hardware/display/c/fidl.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fzl/fdio.h>
#include <src/lib/fxl/logging.h>
#include <zircon/status.h>

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
  fbl::unique_fd fd(open(path.c_str(), O_RDWR));
  if (!fd.is_valid()) {
    FXL_DLOG(ERROR) << "Failed to open " << path << ": errno=" << errno;
    callback(zx::channel(), zx::channel());
    return;
  }

  zx::channel device_server, device_client;
  zx_status_t status = zx::channel::create(0, &device_server, &device_client);
  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to create device channel: "
                    << zx_status_get_string(status);
    callback(zx::channel(), zx::channel());
    return;
  }

  zx::channel dc_server, dc_client;
  status = zx::channel::create(0, &dc_server, &dc_client);
  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to create controller channel: "
                    << zx_status_get_string(status);
    callback(zx::channel(), zx::channel());
    return;
  }

  fzl::FdioCaller caller(std::move(fd));
  zx_status_t fidl_status = fuchsia_hardware_display_ProviderOpenController(
      caller.borrow_channel(), device_server.release(), dc_server.release(),
      &status);
  if (fidl_status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to call service handle: "
                    << zx_status_get_string(fidl_status);
    callback(zx::channel(), zx::channel());
    return;
  }
  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to open controller : "
                    << zx_status_get_string(status);
    callback(zx::channel(), zx::channel());
    return;
  }

  callback(std::move(device_client), std::move(dc_client));
}

}  // namespace gfx
}  // namespace scenic_impl
