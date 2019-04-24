// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_TESTS_MOCK_DEVICE_WATCHER_H_
#define GARNET_BIN_UI_INPUT_READER_TESTS_MOCK_DEVICE_WATCHER_H_

#include "garnet/bin/ui/input_reader/device_watcher.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace ui_input {

class MockDeviceWatcher : public DeviceWatcher {
 public:
  MockDeviceWatcher();
  ~MockDeviceWatcher() override;

  fxl::WeakPtr<MockDeviceWatcher> GetWeakPtr();

  void Watch(ExistsCallback callback) override;

  void AddDevice(std::unique_ptr<HidDecoder> hid_decoder);

 private:
  ExistsCallback callback_;
  fxl::WeakPtrFactory<MockDeviceWatcher> weak_ptr_factory_;
};

}  // namespace ui_input

#endif  // GARNET_BIN_UI_INPUT_READER_TESTS_MOCK_DEVICE_WATCHER_H_
