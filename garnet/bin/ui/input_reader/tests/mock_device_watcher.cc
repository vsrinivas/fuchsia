// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/tests/mock_device_watcher.h"

#include "garnet/bin/ui/input_reader/hid_decoder.h"

namespace ui_input {

MockDeviceWatcher::MockDeviceWatcher() : weak_ptr_factory_(this) {}
MockDeviceWatcher::~MockDeviceWatcher() = default;

fxl::WeakPtr<MockDeviceWatcher> MockDeviceWatcher::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void MockDeviceWatcher::Watch(ExistsCallback callback) {
  callback_ = std::move(callback);
}

void MockDeviceWatcher::AddDevice(std::unique_ptr<HidDecoder> hid_decoder) {
  callback_(std::move(hid_decoder));
}

}  // namespace ui_input