// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_FDIO_DEVICE_WATCHER_H_
#define GARNET_BIN_UI_INPUT_READER_FDIO_DEVICE_WATCHER_H_

#include <string>

#include "garnet/bin/ui/input_reader/device_watcher.h"

namespace fsl {
class DeviceWatcher;
}

namespace ui_input {

// This is the "real" FDIO implementation of |DeviceWatcher|, which uses
// |fsl::DeviceWatcher|.
class FdioDeviceWatcher : public DeviceWatcher {
 public:
  FdioDeviceWatcher(std::string directory_path);
  ~FdioDeviceWatcher() override;

  void Watch(ExistsCallback callback) override;

 private:
  // consumed by |Watch|
  std::string directory_path_;
  std::unique_ptr<fsl::DeviceWatcher> watch_;
};

}  // namespace ui_input
#endif  // GARNET_BIN_UI_INPUT_READER_FDIO_DEVICE_WATCHER_H_
