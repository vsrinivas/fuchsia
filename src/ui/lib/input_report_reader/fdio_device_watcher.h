// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_INPUT_REPORT_READER_FDIO_DEVICE_WATCHER_H_
#define SRC_UI_LIB_INPUT_REPORT_READER_FDIO_DEVICE_WATCHER_H_

#include <string>

#include "src/ui/lib/input_report_reader/device_watcher.h"

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
#endif  // SRC_UI_LIB_INPUT_REPORT_READER_FDIO_DEVICE_WATCHER_H_
