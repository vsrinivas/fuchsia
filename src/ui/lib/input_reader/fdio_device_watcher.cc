// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_reader/fdio_device_watcher.h"

#include <fcntl.h>

#include "src/lib/files/unique_fd.h"
#include "src/lib/fsl/io/device_watcher.h"
#include "src/ui/lib/input_reader/fdio_hid_decoder.h"

namespace ui_input {

FdioDeviceWatcher::FdioDeviceWatcher(std::string directory_path)
    : directory_path_(std::move(directory_path)) {}
FdioDeviceWatcher::~FdioDeviceWatcher() = default;

void FdioDeviceWatcher::Watch(ExistsCallback callback) {
  FXL_DCHECK(!watch_);
  watch_ = fsl::DeviceWatcher::Create(
      std::move(directory_path_),
      [callback = std::move(callback)](int dir_fd, std::string filename) {
        int fd = openat(dir_fd, filename.c_str(), O_RDONLY);
        if (fd < 0) {
          FXL_LOG(ERROR) << "Failed to open device " << filename;
        } else {
          callback(std::make_unique<FdioHidDecoder>(filename, fxl::UniqueFD(fd)));
        }
      });
}

}  // namespace ui_input
