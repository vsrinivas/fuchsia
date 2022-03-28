// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_DEVICE_GROUP_TEST_DRIVERS_LEAF_DRIVER_H_
#define SRC_DEVICES_TESTS_DEVICE_GROUP_TEST_DRIVERS_LEAF_DRIVER_H_

#include <ddktl/device.h>

namespace leaf_driver {

class LeafDriver;

using DeviceType = ddk::Device<LeafDriver>;

class LeafDriver : public DeviceType {
 public:
  explicit LeafDriver(zx_device_t* parent) : DeviceType(parent) {}

  static zx_status_t Bind(void* ctx, zx_device_t* device);

  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }
};

}  // namespace leaf_driver

#endif  // SRC_DEVICES_TESTS_DEVICE_GROUP_TEST_DRIVERS_LEAF_DRIVER_H_
