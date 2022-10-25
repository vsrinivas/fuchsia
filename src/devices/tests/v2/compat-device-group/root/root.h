// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_V2_COMPAT_DEVICE_GROUP_ROOT_ROOT_H_
#define SRC_DEVICES_TESTS_V2_COMPAT_DEVICE_GROUP_ROOT_ROOT_H_

#include <ddktl/device.h>

namespace root {

class Root;

using DeviceType = ddk::Device<Root>;

class Root : public DeviceType {
 public:
  explicit Root(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~Root() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  void DdkRelease();
};

}  // namespace root

#endif  // SRC_DEVICES_TESTS_V2_COMPAT_DEVICE_GROUP_ROOT_ROOT_H_
