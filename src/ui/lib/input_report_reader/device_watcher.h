// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_INPUT_REPORT_READER_DEVICE_WATCHER_H_
#define SRC_UI_LIB_INPUT_REPORT_READER_DEVICE_WATCHER_H_

#include <lib/fit/function.h>
#include <lib/zx/channel.h>

namespace ui_input {

// This interface wraps a facility that can produce the zx::channels that represent
// the input-devices that are registered.
class DeviceWatcher {
 public:
  // Callback function which is invoked whenever a device is found, producing
  // a zx::channel to connect to the device.
  using ExistsCallback = fit::function<void(zx::channel)>;

  virtual ~DeviceWatcher() = default;

  // Begins watching for devices. This method may be called at most once.
  virtual void Watch(ExistsCallback callback) = 0;
};

}  // namespace ui_input
#endif  // SRC_UI_LIB_INPUT_REPORT_READER_DEVICE_WATCHER_H_
