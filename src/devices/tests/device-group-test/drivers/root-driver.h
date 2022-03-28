// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_DEVICE_GROUP_TEST_DRIVERS_ROOT_DRIVER_H_
#define SRC_DEVICES_TESTS_DEVICE_GROUP_TEST_DRIVERS_ROOT_DRIVER_H_

#include <ddktl/device.h>

namespace root_driver {

class RootDriver;

using DeviceType = ddk::Device<RootDriver>;

class RootDriver : public DeviceType {
 public:
  explicit RootDriver(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~RootDriver() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  void DdkRelease();
};

}  // namespace root_driver

#endif  // SRC_DEVICES_TESTS_DEVICE_GROUP_TEST_DRIVERS_ROOT_DRIVER_H_
