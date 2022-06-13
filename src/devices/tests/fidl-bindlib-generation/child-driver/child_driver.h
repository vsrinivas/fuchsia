// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_FIDL_BINDLIB_GENERATION_CHILD_DRIVER_CHILD_DRIVER_H_
#define SRC_DEVICES_TESTS_FIDL_BINDLIB_GENERATION_CHILD_DRIVER_CHILD_DRIVER_H_

#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>

namespace child_driver {

class ChildDriver;
using DeviceType = ddk::Device<ChildDriver>;
class ChildDriver : public DeviceType {
 public:
  explicit ChildDriver(zx_device_t* dev) : DeviceType(dev) {}
  virtual ~ChildDriver() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  void DdkRelease();

  // For inspect test.
  zx::vmo inspect_vmo() const { return inspect_.DuplicateVmo(); }

 private:
  inspect::Inspector inspect_;
  inspect::BoolProperty is_bound = inspect_.GetRoot().CreateBool("is_bound", false);
};

}  // namespace child_driver

#endif  // SRC_DEVICES_TESTS_FIDL_BINDLIB_GENERATION_CHILD_DRIVER_CHILD_DRIVER_H_
