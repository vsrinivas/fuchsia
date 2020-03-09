// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_report_reader/tests/mock_device_watcher.h"

namespace ui_input {

MockDeviceWatcher::MockDeviceWatcher() : weak_ptr_factory_(this) {}
MockDeviceWatcher::~MockDeviceWatcher() = default;

fxl::WeakPtr<MockDeviceWatcher> MockDeviceWatcher::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void MockDeviceWatcher::Watch(ExistsCallback callback) { callback_ = std::move(callback); }

void MockDeviceWatcher::AddDevice(zx::channel chan) { callback_(std::move(chan)); }

}  // namespace ui_input
