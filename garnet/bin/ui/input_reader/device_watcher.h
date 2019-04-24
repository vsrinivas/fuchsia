// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_DEVICE_WATCHER_H_
#define GARNET_BIN_UI_INPUT_READER_DEVICE_WATCHER_H_

#include <lib/fit/function.h>

namespace ui_input {

class HidDecoder;

// This interface wraps a facility that can produce |HidDecoder|s in response
// to device registration.
class DeviceWatcher {
 public:
  // Callback function which is invoked whenever a device is found, producing
  // an |HidDecoder| for the device. This |HidDecoder| must not be null.
  using ExistsCallback = fit::function<void(std::unique_ptr<HidDecoder>)>;

  virtual ~DeviceWatcher() = default;

  // Begins watching for devices. This method may be called at most once.
  virtual void Watch(ExistsCallback callback) = 0;
};

}  // namespace ui_input
#endif  // GARNET_BIN_UI_INPUT_READER_DEVICE_WATCHER_H_
