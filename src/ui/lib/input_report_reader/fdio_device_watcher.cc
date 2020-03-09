// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_report_reader/fdio_device_watcher.h"

#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>

#include "src/lib/files/unique_fd.h"
#include "src/lib/fsl/io/device_watcher.h"

namespace ui_input {

FdioDeviceWatcher::FdioDeviceWatcher(std::string directory_path)
    : directory_path_(std::move(directory_path)) {}
FdioDeviceWatcher::~FdioDeviceWatcher() = default;

void FdioDeviceWatcher::Watch(ExistsCallback callback) {
  FXL_DCHECK(!watch_);
  watch_ = fsl::DeviceWatcher::Create(
      std::move(directory_path_),
      [callback = std::move(callback)](int dir_fd, std::string filename) {
        int fd = 0;
        zx_status_t status = fdio_open_fd_at(dir_fd, filename.c_str(), O_RDONLY, &fd);
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to open device " << filename;
          return;
        }
        zx::channel chan;
        status = fdio_get_service_handle(fd, chan.reset_and_get_address());
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to get service handle " << filename;
          return;
        }
        callback(std::move(chan));
      });
}

}  // namespace ui_input
