// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_DRIVER_CHILD_DEVICE_TEST_CHILD_DEVICE_H_
#define SRC_DEVICES_TESTS_DRIVER_CHILD_DEVICE_TEST_CHILD_DEVICE_H_

#include <ddktl/device.h>

namespace child_device {

class ChildDevice;
using DeviceType = ddk::Device<ChildDevice, ddk::Initializable>;
class ChildDevice : public DeviceType {
 public:
  explicit ChildDevice(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~ChildDevice() = default;

  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();
};

}  // namespace child_device

#endif  // SRC_DEVICES_TESTS_DRIVER_CHILD_DEVICE_TEST_CHILD_DEVICE_H_
