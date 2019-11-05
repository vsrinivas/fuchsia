// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_INPUT_READER_TESTS_MOCK_DEVICE_WATCHER_H_
#define SRC_UI_LIB_INPUT_READER_TESTS_MOCK_DEVICE_WATCHER_H_

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/lib/input_reader/device_watcher.h"

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

#endif  // SRC_UI_LIB_INPUT_READER_TESTS_MOCK_DEVICE_WATCHER_H_
